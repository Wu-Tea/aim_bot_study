# C++ 性能与代码冗余审计报告

- **日期**：2026-08-07
- **范围**：`native/` 下全部 C++ 源码（约 87K 行），含 `controller_native`、`vision_native`、`tracking_native`、`fps_2d_tracker_package`、`control_learning`、`runtime_app`、`common_native`、`pipeline_contract`、`recoil_native`、`shared_fusion`
- **构建**：`b/Release`，MSVC `/O2 /Ob2 /DNDEBUG`，CUDA arch 89，TensorRT 10.15
- **运行参数**：控制器 tick 默认 1000 Hz（实际 ~8ms），视觉采集 ~140 fps

## 热路径地图（用于严重程度标定）

| 路径 | 频率 | 内容 |
|------|------|------|
| 控制器 tick | `runtime_app/runtime_loop.cpp:run_once`，≤1000Hz | 读手柄 → `controller_.build_output` → 视口 → ViGEm 输出 → telemetry |
| 视觉帧 | `vision_native/src/vision_engine.cpp:VisionEngine::poll_once` | grab → 推理 → 同步 → 回读 → 选择器 → 追踪 → 关联 |
| 追踪/关联 | 每视觉帧 | `fps::TargetTracker::ingestVisionFrame` + Kalman + 关联 |
| 学习路径 | 仅 `telemetry_new_vision` 时 | RLS 学习者（19 延迟 × 8 通道）——不在最紧 tick 路径上 |

**架构事实**：
- 视觉管线是**纯同步单线程**（采集与推理零重叠）。
- 学习/追踪/控制器全部跑在同一控制器线程，无锁竞争；telemetry 写入者在线程外。
- `ai_aim.cpp`（`NativeAiAim`）**未接入正式运行时**：仅编译进 benchmark/测试目标（`CMakeLists.txt` 6 处，均非 `cod_native_runtime`），主 tick 使用 `vector_intent_fuser` + ADS/BodyLock 控制器。其内部问题当前不是线上热点，但属于并行实现/潜在死代码。

---

## 高严重度（会实际吃掉帧预算）

### H1. 视觉每帧多次 `cudaStreamSynchronize` 死等 GPU
- **位置**：`native/vision_native/src/tensorrt_engine.cpp:350,482`；`native/vision_native/src/vision_engine.cpp:415,487`
- **问题**：每次推理后立即 `cudaStreamSynchronize` 排干整条流；ego 运动与颜色回读又各发一次全流同步。单线程流水线下，GPU 延迟完全暴露在 CPU 每帧关键路径。
- **影响**：帧间延迟 = GPU 全管线串行时间，无法用异步隐藏。最近 CUDA Graph 提交（`5fe3493`）优化了图回放，但同步模式未变。
- **修复**：depth-2 双缓冲异步流水线——本帧全异步 submit（`enqueue`），下一帧开头取上一帧结果（`cudaStreamWaitEvent` / 环形 host 缓冲）。至少先合并 `poll_once` 内后两次同步（ego、color）为一次。

### H2. 视觉帧多跳拷贝 + 颜色回读重复同步
- **位置**：`native/vision_native/src/tensorrt_engine.cpp:436-454`；`native/vision_native/src/vision_engine.cpp:465-515`
- **问题**：
  - `infer_bgra_array_roi` 先 `cudaMemcpy2DFromArrayAsync`（D2D）把 BGRA ROI 拷入 `device_frame_`，再跑 resize+normalize+CHW/F32 kernel——两遍 GPU 处理。
  - 颜色回读对**已同步过的同一帧**再发 D2H + 又一次同步；pageable 回退路径（`vision_engine.cpp:496`）可能整块 realloc。
- **修复**：用 `cudaTextureObject_t` 单 kernel 从 CUDA array 直读采样到 `device_input_`（`preprocess.cu` 里 ego 路径 `launch_bgra_array_to_gray_u8` 已有 texture→kernel 直读写法可参考），删除 `device_frame_` 中转与一次 D2D 全量拷贝；颜色回读 D2H 并入推理的同一流同一次同步。

### H3. recoil profile 开火时每 tick 磁盘文件 I/O
- **位置**：`native/controller_native/recoil_compensation.cpp:78,130-155` → `native/controller_native/recoil_profile.cpp:288-341`
- **问题**：`compute`（开火时每 tick，由 `native_gamepad_controller.cpp:1099` 调用）无条件调 `select_runtime_profile_for_context` → `load_matching_recoil_profile`：**读识别器状态 JSON → 遍历 profile 目录 → 逐个 `load_recoil_profile` 解析全部 JSON → 构造 `std::set`/`std::vector` → 排序**，随后才在 `recoil_compensation.cpp:146` 用 profile_id 命中缓存。即"每 tick 全量重读重解析，只为判断 profile 是否变化"。
- **影响**：开火时每 tick 的文件打开、JSON 解析、大量堆分配，可显著超出 8ms tick 预算。当前唯一真正的"阻塞型"控制器热路径问题。
- **修复**：用 `std::filesystem::last_write_time`（recognition state / profile 目录 mtime）或 profile_id 做廉价变更检测，仅变化时重载；或将加载移出 tick（限频 ~5Hz）。

### H4. `adapt_committed_capture_observation` 每帧被调两次
- **位置**：`native/runtime_app/runtime_loop.cpp:733` 与 `:1161`
- **问题**：两个分支条件 `viewport_fresh_vision` 与 `telemetry_new_vision` 同源（`:666-667`、`:694-695` 同时置位），在**同一新视觉帧**上用**完全相同参数**（`latest_vision_result_`、`last_target_plan()`、`aim_height_ratio`、`ads_epoch`、`latest_controller_consume_started_ns_`）重复调用 `adapt_committed_capture_observation`。该函数内部遍历全部检测计数候选并调 `resolve_target_geometry`（`vision_controller_adapter.cpp:212-227`）。
- **修复**：每帧构建一次并复用（指针/值），两个消费者共享。

---

## 中严重度（常数量级损耗，量大才明显）

### M1. `target_tier` 字符串每 tick 多次分配 + 小写归一化
- **位置**：`native/tracking_native/tracker_authority.cpp:11-21`（`normalized_tier` + `classify_target_tier`）；调用点 `native/controller_native/assist_authority_policy.cpp:20,25,32,37,124,128,172,185`、`auto_fire_gate.cpp:206,261`、`target_tracker.cpp:43-44`
- **问题**：每次调用构造 `std::string` 并逐字符 `tolower`。`decide_assist_authority` 内对同一 `evidence_tier` 最多归一化 4 次。
- **修复**：tier 改用 `enum class`（下游已普遍使用 `TargetTierClass`）或 `std::string_view ==` 直比（输入本就来自少数已知字符串），彻底去掉每次调用的小写归一化；至少把同一表达式的归一化结果提取为局部变量复用。

### M2. 关联 O(n²) 全对打分 + 全排序 + 每帧 vector 分配
- **位置**：`native/fps_2d_tracker_package/src/association.cpp:67-114`；`native/fps_2d_tracker_package/src/target_tracker.cpp:48-99`
- **问题**：`match()` 对 `tracks × measurements` 全笛卡尔积调 `scorePair` 后 `std::sort` 全部候选对，再贪心匹配；`ingestVisionFrame` 每帧分配 `measurements`/`matchedTrack`/`matchedMeasurement`/`views` 等 vector。当前 track 数 n≈5 绝对量小，但随 ROI 内目标增多会显著。
- **修复**：复用预分配成员 buffer（或小固定数组 + 栈缓冲），pair 数 ≤8 时用插入排序；`snapshots()`/`query()` 结果 vector 复用。

### M3. RLS 学习器防御性全状态拷贝 + Cholesky
- **位置**：`native/control_learning/robust_ew_rls.h:55-122,141-172`；调用点 `native/control_learning/causal_online_response_learner.cpp:173-228`
- **问题**：每次 `update` 按值拷贝整个 `State`（theta 2 + cov 2×2 + info 2×2 + 诊断），并做完整 `positive_definite`（Cholesky）与全字段 `finite` 扫描。学习器每帧跑 **19 延迟 × 8 通道 = 152 次 update**。
- **修复**：2×2 矩阵用闭式判断（`det>0 && trace>0`）替代完整 Cholesky；把 `finite` 检查并入 update 循环而非独立 pass；`std::array` 矩阵改 struct-of-scalars（`a,b,c,d`）减少拷贝。

### M4. `ControlHistory::integrate` 每帧 ~19 次二分搜索
- **位置**：`native/control_learning/control_history.h:189-191,293-299`；调用点 `causal_online_response_learner.cpp:179`、`pending_motion_model.cpp:84-93`
- **问题**：每个延迟候选调一次 `integrate`，每次做 3 次二分（`lower_bound_index`/`floor_index_strict`/`floor_index`）+ 2 次 `cumulative_at`（各自二分 + 分段推进）。学习者每帧 19 次。
- **修复**：按最小 begin 到最大 end 批量二分一次，从前缀值推导各延迟积分。

### M5. 每 tick 构建 telemetry 未按开关门控
- **位置**：`native/runtime_app/runtime_loop.cpp:964-1159`
- **问题**：即使 telemetry 关闭，`run_once` 每 tick 仍构造完整 `TelemetryTickInput`（`last_ai_aim_mode().c_str()` 等十余个字段），`observe_tick` 才短路。
- **修复**：整块 telemetry-tick 构建移到 `telemetry_collectors_.enabled()` 之后。

### M6. Bodylock 重复计算
- **位置**：`native/controller_native/bodylock_follow_controller.cpp:37-42`（`response_horizon_*`）与 `:65-68`（`arrival_horizon_*`）
- **问题**：同一表达式 `feedback_range_px / (max_force_px * fallback_response_px_per_stick_second)` 每个轴各算两遍除法 + max。
- **修复**：算一次 hx/hy 复用到两个结构体字段。

### M7. `bodylock_policy::lead_delta` 每返回重复计算整条 lead 曲线
- **位置**：`native/controller_native/bodylock_policy.cpp:204-224`
- **问题**：`lead_delta()` 在 `:220` 调 `relative_motion_estimate()`（`:174-202`）取 `lead_x_px`，后者重算 `configured_lead_seconds`/`lead_max`/`raw_lead`/`predicted_rate`，且 `lead_seconds` 分支表达式与 `lead_delta()` 前 3 行自算的 `horizontal_lead_seconds` 完全相同。
- **修复**：`lead_delta()` 内联 lead 算法或让 `relative_motion_estimate` 接受缓存；在 `has_sustained_motion()` 与 `motion_frames_` 检查通过前短路。

---

## 低严重度 / 代码冗余（无性能损失，有维护与行为漂移风险）

### L1. 三套并行响应估计器
- `native/controller_native/aim_response_estimator.cpp`（标量 EMA）、`native/controller_native/control_response_estimator.cpp`（标量 EMA）、`native/control_learning/robust_ew_rls.h`+`causal_online_response_learner.cpp`（2×2 矩阵 RLS）。
- 外加 `causal_mix_evaluator.cpp` 与 `control_learning/short_horizon_rollout.cpp` 的前向打分重叠。
- **建议**：抽一个共享"response model"核，标量估计器与 RLS 消费同一 kernel。

### L2. 三套追踪后端并存
- `native/tracking_native/kalman_tracker.cpp`、`native/tracking_native/legacy_projection_tracker.cpp`、`native/fps_2d_tracker_package/src/*`（经 `native/tracking_native/fps_reference_tracker.cpp` 委托）。
- `cod_native_runtime` 同时链接 `../tracking_native/*.cpp` 与 `${FPS_TRACKER_PACKAGE_SOURCES}` 两套源码，靠运行时开关（`tracker_backend.cpp:127-139`）选择。Kalman/投影/ego 运动逻辑重叠。

### L3. Kalman 4×4 通用 Joseph form（当前规模下成本小）
- **位置**：`native/fps_2d_tracker_package/src/kalman_cv2d.cpp:122-138`
- **问题**：`P = (I-KH)P(I-KH)ᵀ + KRKT` 用通用 4×4 矩阵运算（构造 `I_KH`、16 项 `KRKT`、两次 4×4 乘法 + transpose）。因 H 只在首两列非零，本可降为结构化更新。**每匹配 track 每帧 ~200 次乘加，当前 n≈5 时绝对成本可忽略**，属"正确但比需要更通用"。
- **修复**（可选）：利用块结构用 2×2 运算，或标准 `P' = P - K S Kᵀ` 两秩更新。

### L4. 复制粘贴块
- `tensorrt_engine.cpp` `infer_rgb` 与 `infer_bgra_array_roi` 尾部 decode 段完全重复（`:360-381` vs `:492-515`）。
- `KalmanTracker::clamp_velocity` 与 `LegacyProjectionTracker::clamp_velocity` 逐字相同（`kalman_tracker.cpp:118` vs `legacy_projection_tracker.cpp:129`），`decay_velocity_for_weak_observation`/ingest 结构亦复制。
- `vision_engine.cpp` `set_aiming`（`:181-193`）与 `reset`（`:235-245`）body 几乎相同。
- `tracker_authority.cpp` 与 `fps_reference_tracker.cpp` 的 lowercase helper 重复实现。

### L5. 其他小项
- `target_coordinator.cpp:50`：`std::pow(float,float)` 算 alpha，间隔变化缓慢可缓存。
- `aim_response_curve_plugin.h:63`：`interpolate_monotonic` 线性扫描 20 元素 LUT，每 tick `inverse_aim_response_curve` 调用；可二分。
- `pending_motion_model.cpp:15-20` / `short_horizon_rollout.cpp:10-13` / learner：三处各自实现 2×2 矩阵×向量乘法，可合并为共享 inline 头。
- `causal_online_response_learner.cpp:202`、`short_horizon_rollout.cpp:9`：2 元素向量用 `std::hypot`，可改 `sqrt(x²+y²)`。
- `fps_2d_tracker_package/src/ego_motion_buffer.cpp:39-58`：`cumulativeStick` 线性扫描整个 control deque，每 track 快照每次查询调用；kMaxSamples 有界，成本低。
- `tracking_native/tracker_backend.cpp:94`：`estimate.confidence` 硬编码 `1.0f`，丢弃底层追踪器真实置信度（信息/正确性冗余，非性能）。

---

## 构建 / 基础设施

- **B1. CMake 未强制 Release**：`native/vision_native/CMakeLists.txt` 无 `CMAKE_BUILD_TYPE` 兜底。当前 `b/` 为 Release，但 Visual Studio 多配置生成器默认 Debug（`/Od /RTC1`），若有人 `cmake --build b` 而不指定 `--config Release` 会性能断崖。建议脚本/CI 强制 `--config Release`。
- **B2. `shared_fusion/fusion_channel.h:48,63`**：用 `volatile` 做跨进程同步（正确性隐患，应用 `std::atomic` + acquire/release 语义）。双缓冲 + 序列号设计本身无性能问题；每帧拷贝整个含 256 检测槽的 `FusionSlot`（~8KB）在 140-240fps 下带宽可忽略。

---

## 建议优先级（ROI 排序）

1. **H1+H2** 视觉异步流水线（depth-2 双缓冲 + 单 kernel texture 直读）——延迟影响最大的架构项。
2. **H3** recoil profile 变更检测缓存——唯一可能吃满 8ms tick 的阻塞型 I/O。
3. **H4** `adapt_committed_capture_observation` 去重——三行代码的免费提速。
4. **M1** tier 枚举化——清掉每 tick 字符串分配噪音。
5. M2-M7 为常数量级优化；L 组为代码去重（维护收益为主）。

## 验证状态

- 本报告中的 H1-H4、M1（行号）、M6、M7、L3、B1、B2 均经人工二次核对。
- 其余（M2-M5、M1 调用点细节）来自子系统深度阅读，行号可作为起点，改动前请复核。
