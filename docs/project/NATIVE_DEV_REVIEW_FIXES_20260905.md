**dev native 审查修复记录 · 2026-09-05**

基线为 `dev / 30014470af9ea09a673ffa29d8972a9da149ae93`，对应 [修复前审查报告](D:/work/AI/yolo-study-001/docs/project/NATIVE_DEV_REVIEW_20260905.md)。用户授权关闭仍可影响主线的旧 recoil profile 路径，并自主处理其余审查项。本轮已完成代码修改、独立构建、产品门禁及启动脚本所用程序的重编译；未启动游戏控制或改变显示设置。

**Recoil 的处理决定**

原来的本机配置和示例配置确实是 `profile_playback_enabled=false`，所以当前常用配置没有每 tick 扫描 profile 的开销。但 C++ 默认值仍为 true，控制器构造函数会加载目录，生产 RecoilReducer 也仍可进入旧播放模块。只保留配置中的 false，不能保证以后不会重新进入这条路径。

现在已经在生产依赖边界关闭：

- [RecoilReducer](D:/work/AI/yolo-study-001/native/controller_native/recoil_reducer.cpp:7) 只保存 enabled 和初始化时计算的压枪量，开火 tick 不读取文件、识别状态、profile 或 calibration，也没有 profile 选择和播放状态。
- NativeGamepadController 的启动加载入口已移除；即使调用者直接传入 `profile_playback_enabled=true`，也不能让生产 reducer 访问目录。
- 旧 profile、calibration 和播放实现移入单独的 [recoil_profile_tools 库](D:/work/AI/yolo-study-001/native/vision_native/CMakeLists.txt:172)，生产 controller/runtime 不链接该库；runtime 源集中的旧 weapon recognizer 也已移出。离线工具与其测试仍保留。
- TOML 中两个旧开关继续兼容读取，但生效值固定为 false；`RECOIL_NATIVE_RECOGNIZER` 环境变量也不能启用它们。[C++ 默认值](D:/work/AI/yolo-study-001/native/controller_native/runtime_config.h:93) 同时改为 false。
- 本机 `config.toml`、已有 profile/校准数据和其他用户文件没有修改或删除。实时压枪仍使用原先 `feedback_amount` 与 min/max 的规则，AutoFire 后再合成 recoil 的顺序保持不变。

验证采用了两条独立证据：把生产 controller 的 profile 路径故意指向一个普通源码文件，旧实现会在构造时抛出目录枚举错误，修复后可以正常构造、持续开火并在停止开火后归零；用实际启动程序读取同时开启旧开关和环境变量的配置，结果仍为 `profile_playback_enabled=0`、`native_recognizer_enabled=0`，`feedback_amount=0.23` 得到保留。

证据：[生产配置验证](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/retired-recoil-effective-config.log)、[生产控制库对象列表](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/production-core-members.txt)。后者中与 recoil 相关的对象只剩 `recoil_reducer.obj`。

**其余审查项的处理**

| 项目 | 根因修复 | 验证与边界 |
| --- | --- | --- |
| F1 决策时钟 | [resolve_control_frame](D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:532) 在实际决策时读取时钟；输入边沿继续保留 begin_tick 的采样时刻。移除不再使用的旧采样时间副本。 | 新帧晚于输入采样时仍获得当 tick 修正；真正过期/未来帧被拒绝；LT 释放后权限结束。 |
| F2 图像 freshness | [capture](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:287) 忽略仅光标更新，不生成新的 frame_id 或推理。门禁与 adapter 共用 [图像来源时间](D:/work/AI/yolo-study-001/native/vision_native/include/vision_native/observation_time.h:9)，使用校准后的 present 时间；复制完成字段继续用于原阶段 telemetry。 | 已覆盖旧图像、未来时间、重复 QPC、校准抖动、校准不确定度和无法映射的 native present 时间。真实 DXGI 光标事件到最终控制的整条链尚未做现场注入。 |
| F3 DXGI 恢复几何 | [rebuild_duplication](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:228) 重新选择输出并刷新描述、位置和 ROI。复制前核对桌面纹理尺寸；遇到变化只重建并返回未更新。固定大小的 ROI texture/device 保持存活，CUDA 注册不会指向被替换的纹理。 | 中心重算、负桌面坐标和 ROI 越界用例通过。指定输出失效时不会静默换成另一个输出，未支持的旋转会拒绝捕获。真实分辨率/拓扑切换仍待实测。 |
| F4 CUDA 互操作同步 | [CudaGraphicsMapping](D:/work/AI/yolo-study-001/native/vision_native/include/vision_native/cuda_graphics_mapping.h:11) 让 map、数组读取和 unmap 使用同一 engine stream，并管理作用域退出时的 unmap。固定形状 TensorRT CUDA Graph 保留。 | 真正的 D3D11→CUDA 内容测试通过 512 帧，每帧 65,536 像素，内容不匹配为 0。未声称旧驱动错帧已复现，也未把这个测试当成完整 TensorRT/游戏延迟 A/B。 |
| F5 Worker 异常 | [VisionService](D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:306) 保存原异常、清除 mailbox，随后由消费线程接收；stop 即使遇到已失败的 worker 也会 join。[RuntimeLoop](D:/work/AI/yolo-study-001/native/runtime_app/runtime_loop.cpp:575) 在故障/停止时先撤销 controller 状态并尝试发送中性输出，再 join、收尾日志并报告原错误。 | 覆盖启动即失败、已发布有权限帧后失败、重复 stop、失败后拒绝直接重启同一 service。未连接实体 ViGEm 验证中性报告最终交付。 |
| F6 2000 Hz 积分 | TargetCoordinator 不再把真实 0.5 ms 更新放大成 1 ms；保留原来的长间隔上限。 | 500/1000/2000 Hz 的相同墙钟时间 D 轨迹一致，边界退出时间差处于一个 500 Hz tick 的容差内。 |
| F7 Fusion 快照 | [reader 快照](D:/work/AI/yolo-study-001/native/shared_fusion/fusion_snapshot.h:23) 在同一序号校验区间内复制完整 payload，拥有固定容量的本地 detections。读取有次数上限，奇数序号或不稳定槽不会成功返回。 | 原生产 reader 探针确认：奇数槽返回 false；writer 复用槽后，reader 中的 detection.x1 仍为 11，没有变成另一帧的 33。IPC 布局/版本和 writer 非阻塞行为保持不变。 |

F2 在真正提供 DXGI present 信息但无法映射时拒绝控制；无 DXGI present 元数据的离线/其他生产者沿用其原 capture 时间约定，避免把复制阶段字段静默改成另一种语义。新鲜度门限没有放宽。

另完成一项有明确所有者的小型性能改动：[mailbox API](D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:189) 在复制之前检查消费者 sequence，无新帧时返回空更新；发布和消费过程中的部分重复 result 复制改为 move。没有根据这项改动声称已经消除了真实运行时的毫秒级瓶颈。

**验证结果**

- 独立 Release 全量构建通过：[最终构建日志](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/final-build.log)。沿用 MSVC 19.44、CUDA 13.1、TensorRT 10.15.1.29、CUDA architecture 75；独立验证目录关闭 offline benchmark 注册，保留已有 SDK 字符编码警告。
- **11/11 组 CTest 通过，两个产品 runner 的 357 个命名用例全部通过**，比审查基线新增 12 个命名用例，另有 Fusion 契约测试：[最终 CTest 摘要](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/final-tests.log)、[完整命名用例记录](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/final-LastTest.log)。涵盖 ADS、BodyLock、身份、手动意图、生命周期、AutoFire、Recoil 和输出契约。
- 新增回归在修改生产行为前观察到预期失败：[RED 记录](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/red-tests.log)。worker 的 terminate 与 Fusion 共享槽缺陷另外沿用修复前生产代码探针证据。F3 的实际显示变更、F4 的旧驱动内容错误没有现场 RED，不把它们写成已有实机故障闭环。
- [GPU 内容一致性日志](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/final-cuda-d3d11-interop.log)：512 帧、33,554,432 像素比较，内容错误为 0。测试只创建测试纹理和 CUDA 资源。
- [ADS near-target slowdown 独立回归](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/final-ads-near-target-incident.json) 为 GREEN，冻结 trigger/counterfactual 保持有效；它没有被改写成新的优化评分。
- [Fusion 原始探针修复后输出](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/fusion-reader-fixed.log)。该旧探针的退出码 4 表示其“两个旧缺陷仍然存在”的负向条件不再成立；正式 Fusion 产品测试返回 0。
- 源文件、测试与实际启动程序的 SHA-256 见 [fix-manifest.json](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/fix-manifest.json)。

**使用与验收**

现有启动脚本所使用的两个程序已经从当前源码重建：[launcher 构建日志](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/final-launcher-build.log)。继续使用原来的 [Fusion 启动脚本](D:/work/AI/yolo-study-001/scripts/launch/gamepad_fusion_background_start.ps1) 或 [native 启动脚本](D:/work/AI/yolo-study-001/scripts/launch/gamepad_native_background_start.ps1)，不需要添加新开关。本轮没有自动运行它们。

本轮没有调整 ADS/BodyLock 的控制增益、延迟参数和准入门限。采用真实图像来源时刻后，观测时间的参照发生了修正；现有响应延迟参数没有根据真实游戏重新拟合。因此仍需匹配配置、武器、FOV、输出路径的实机 A/B，检查瞄准、连续跟随、开火和手感，再做生产验收。真实显示模式切换、实体设备故障和完整运行时 P95/P99 也仍是验收缺口。

没有新增低频 proposal 控制链、模拟器分数优化或对旧 Direct 控制实验的恢复。未建立根因的响应命令交付记账与 telemetry 锁竞争，仅保留为后续验证事项。

建议将“旧 recoil profile/recognizer 已从生产路径退役”、本报告链接及剩余实机验收缺口同步到 `.agent-context/`。本轮未修改这些项目上下文文件。
