**dev 分支 native 模块审查报告 · 2026-09-05**

后续处理见 [修复与验证记录](D:/work/AI/yolo-study-001/docs/project/NATIVE_DEV_REVIEW_FIXES_20260905.md)。以下保留修复前 `3001447` 的审查结论与源码行号；当前工作区源码已包含后续修复。

审查快照：`dev`，`30014470af9ea09a673ffa29d8972a9da149ae93`。本轮审查覆盖当前实现，同时重点检查最近的 `0d827b1`（ADS/BodyLock）和 `3001447`（Fusion 小窗口）修改。报告、独立构建和验证探针是本轮新增内容；生产代码和本机配置未修改。

**结论**

当前架构已经具备值得保留的基础：selector 负责目标身份，controller 保持同 tick 的完整控制链，OutputComposer 统一生成最终输出，Fusion 保持显示用途，TensorRT 固定形状推理使用 CUDA Graph。当前最值得投入的工作，是修正观测时间、跨线程/跨流交付、捕获恢复和控制频率边界，然后再评估控制参数与性能优化。

本轮发现 **8 项需要处理的问题：4 项 P1、4 项 P2**。其中 5 项有调用生产代码的功能复现，1 项有性能实测，另外 2 项由静态数据流或官方 API 契约建立。没有把未实测的 GPU 错帧、真实屏幕切换后果或游戏手感描述成已经发生的事实。

独立 Release 全量构建成功；**11/11 组 CTest 通过，两个产品测试 runner 合计 345 个命名用例通过，另有 Fusion 契约测试通过**。最近新增、未纳入默认 CTest 的 ADS near-target slowdown 独立回归也通过。现有测试通过与下面的问题复现并存，说明测试在运行边界上仍有缺口。

| 编号 | 级别 | 问题 | 证据强度 / 触发范围 |
| --- | --- | --- | --- |
| F1 | P1 | 控制决策沿用输入采样时间，直接轮询的新帧被误判过期 | 生产 controller + delivery gate 可复现；直接轮询稳定触发，异步路径在特定调度交错下可触发 |
| F2 | P1 | 图像新鲜度按复制时间计算，旧图像与仅光标更新可被重新授予 freshness | 生产 gate 可复现；DXGI 生产路径和 Microsoft 文档支持输入可达性 |
| F3 | P1 | DXGI 恢复不刷新输出尺寸、位置和 ROI | 静态根因已定位；真实分辨率/拓扑变更尚未执行 |
| F4 | P1 | D3D→CUDA map 与非阻塞推理流之间缺少明确同步 | 生产调用链违反可依赖的 API 同步契约；未实测 GPU 错帧 |
| F5 | P2 | Vision worker 的异常直接终止进程 | 注入 poller 异常可复现；影响运行时故障处理和退出完整性 |
| F6 | P2 | 2000 Hz 下目标内手动修正和边界计时加速 | 生产 TargetCoordinator 可复现；默认 1000 Hz 不触发倍速问题 |
| F7 | P2 | Fusion seqlock 可接受写入中的槽，检测数组也不属于稳定快照 | 直接编译生产 reader 可复现；窗口显示一致性受影响 |
| F8 | P2 | recoil profile 开启时每个开火 tick 都读取和解析目录 | 生产 compute 性能实测；当前本机及示例配置关闭此功能 |

P1 表示应优先修复、可能破坏核心输入/输出正确性的缺陷；P2 表示条件触发、局部功能或性能问题。级别表示影响和修复顺序，不表示所有后果均已在实机观测。

**F1 · [P1] 控制决策的时间早于本 tick 接收到的图像**

位置：[输入采样与 begin_tick](D:/work/AI/yolo-study-001/native/runtime_app/runtime_loop.cpp:628)、[直接轮询](D:/work/AI/yolo-study-001/native/runtime_app/runtime_loop.cpp:737)、[保存采样时间](D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:502)、[resolve 沿用旧时间](D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:543)、[TargetCoordinator 的时间门禁](D:/work/AI/yolo-study-001/native/controller_native/target_coordinator.cpp:258)。

RuntimeLoop 先调用 `begin_tick()`，再获取图像，最后执行 `resolve_control_frame()`。但是 resolve 的 `now` 仍是 `sampled_now_seconds_`。当 `gpu_service_enabled=false` 时，这一帧的 capture 时间自然晚于 begin_tick；delivery gate 用真正的消费时间接受该帧，TargetCoordinator 随后却用更早的采样时间得到负的 source age，将它标记为 `StaleCapture`。该帧已经从 pending snapshot 消费掉，不会在下个 tick 自动重试。

固定相同目标、相同 LT 输入及相同消费时间，生产代码探针结果：

| 输入采样时间 | 图像 capture 时间 | 消费时间 | Delivery gate | Controller 结果 |
| --- | --- | --- | --- | --- |
| 1.000 s | 0.998 s | 1.006 s | 接受 | `target_id=1`，`right_x=0.064` |
| 1.000 s | 1.002 s | 1.006 s | 接受 | `target_id=0`，`StaleCapture`，`right_x=0` |

这是两个边界对“当前时间”采用不同语义的问题。异步 Vision 默认路径通常取得更早完成的帧，但如果主线程在 begin_tick 后被抢占、随后读到更新的 mailbox，也具有相同触发条件。`plan_decision_ns` 同样被记录为 1.000 s，即决策时间早于消费时间，影响阶段延迟解释。

建议由 runtime/controller 的时钟边界分别保留输入事件时间与实际决策时间；位置新鲜度和 plan 决策使用后者，输入边沿及 ADS 起始时间仍保留真实采样时间。不要通过放宽未来时间容忍值来掩盖该问题。回归必须覆盖直接轮询、异步交错、正常旧于采样时刻的帧及实际来自未来的非法帧。

证据：[复现日志](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/post-sample-capture.log)。

**F2 · [P1] 复制完成不等于图像内容刚刚更新**

位置：[DXGI 将成功获取直接记为 updated](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:305)、[capture 时间赋值](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:309)、[VisionDeliveryGate](D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:72)、[controller adapter 时间投影](D:/work/AI/yolo-study-001/native/runtime_app/vision_controller_adapter.cpp:117)。

每次 `AcquireNextFrame` 成功后，代码生成新 frame_id，并把 `captured_at_ns` 设置为本次复制/ReleaseFrame 的完成时间。门禁只检查这个时间，没有使用已经记录的 `source_present_steady_ns`。因此，一张来源时间已经很老、刚刚复制完成的图像仍被看作新鲜输入。

另一个明确可达的输入是硬件光标更新：DXGI 可以仅因为指针位置或形状变化而返回成功，此时 `LastPresentTime` 和 `AccumulatedFrames` 都为 0，桌面图像并没有更新。生产代码没有排除这种帧。依据：[Microsoft AcquireNextFrame](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe)、[Microsoft DXGI_OUTDUPL_FRAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info)。

探针将门限固定为 50 ms，输入 `source image age=200 ms`、`copy age=5 ms`，结果仍为接受；随后输入 `AccumulatedFrames=0`、没有 present 时间、但复制时间更新的帧，也被接受。adapter 继续把复制时间作为 controller 的 capture 时间。

这会额外执行重复推理，并允许重复像素延长目标观测的 freshness；对连续观测计数、响应估计、BodyLock 和 AutoFire 的保护都构成输入层风险。本轮没有进一步声称已复现真实游戏中的误开火。

建议由 capture 层区分“桌面内容更新”与“仅光标更新”，后者不生成新的目标观测。保留复制阶段的兼容字段，同时为有效图像来源提供单独时间语义，已知 present 时间应进入年龄与单调性门禁；来源时间未知时需要明确的能力/保守策略。这样才能在数据生产层修复，而不是依赖 controller 再猜一次图像是否变了。

证据：[门禁复现日志](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/freshness.log)。仍需真实 DXGI 光标更新与桌面停帧 fixture，验证生产 capture 到最终控制的完整传播。

**F3 · [P1] 重建 duplication 后仍沿用旧屏幕中心**

位置：[初始化时计算输出和 ROI](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:162)、[rebuild_duplication](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:219)、[失效恢复入口](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:266)、[继续使用旧 source_box](D:/work/AI/yolo-study-001/native/vision_native/src/dxgi_capture.cpp:278)。

`rebuild_duplication()` 只重建 duplication/output1，没有重新获取 `DXGI_OUTPUT_DESC`，也没有更新输出尺寸、桌面位置或 ROI。分辨率或显示器位置改变后，成功恢复捕获仍会沿用旧几何。

以 capture=480×416 为例，2560×1440 时 ROI 左上角为 `(1040,512)`；切换到 1920×1080 后应为 `(720,332)`。继续使用旧值会让捕获中心相对新的准星偏移 **320×180 px**，而 controller 仍把 crop 中心当作准星中心。更大的缩小可能使 source_box 越界。

最近的 Fusion 修复会刷新 Canvas 的虚拟屏幕几何并新建探针 capture，但它不能刷新已经运行的 VisionEngine 内部 capture。因而 Canvas 恢复通过不能证明 controller 的输入几何已经恢复。

建议把输出描述、ROI、资源有效性及必要的 CUDA 注册生命周期纳入 capture 恢复的一次完整验证；验证失败时不发布可控制观测。需要真实分辨率变更/拓扑变更或可替换 DXGI backend 的回归。本轮未改变用户显示设置，**这里是静态证明的恢复缺口，320×180 px 是几何推导，不是实机测量**。

**F4 · [P1] CUDA 非阻塞推理流没有取得 map 的同步保证**

位置：[默认流上 map](D:/work/AI/yolo-study-001/native/vision_native/src/vision_engine.cpp:304)、[创建非阻塞 stream](D:/work/AI/yolo-study-001/native/vision_native/src/tensorrt_engine.cpp:186)、[在该 stream 上读取映射数组](D:/work/AI/yolo-study-001/native/vision_native/src/tensorrt_engine.cpp:440)。

D3D 提交 ROI 复制后，`cudaGraphicsMapResources(..., nullptr)` 将映射同步安排在默认流上；后续 array→device 复制及推理却运行在 `cudaStreamNonBlocking` 的 engine stream 上，代码没有建立两个流之间的 event/wait 依赖。

CUDA 13.1 文档只保证先前 graphics 工作在 **传入 map 的那个 stream** 的后续 CUDA 工作之前完成；非阻塞流不参与默认流的隐式同步。依据：[CUDA 13.1 Graphics Interoperability](https://docs.nvidia.com/cuda/archive/13.1.0/cuda-runtime-api/group__CUDART__INTEROP.html)、[CUDA 13.1 Stream synchronization](https://docs.nvidia.com/cuda/archive/13.1.0/cuda-runtime-api/stream-sync-behavior.html)。

因此，当前代码没有明确取得读取 ROI 之前的跨 API 顺序保证。推理末尾已有 `cudaStreamSynchronize`，它有助于成功路径的后续 unmap 安全，但不能倒过来建立读取发生之前的依赖。

建议让 map 与实际读取使用同一 engine stream，或显式传递同步事件；保留固定形状 CUDA Graph，不需要因此退回低性能推理实现。应增加 D3D 逐帧写入可识别图案、CUDA 验证帧内容的负载交错测试，再量测改动的 GPU/CPU 尾延迟。

**证据边界：确认的是 API 契约缺口，本轮没有实测出错帧，也不能断言当前驱动每次都会异步返回 map。** 某些实现上的隐式等待可以掩盖问题，但不应成为准确性的前提。

**F5 · [P2] Vision worker 异常绕过 runtime 的故障处理**

位置：[poll_once 异常出口](D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:208)、[worker 入口没有异常传递](D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:274)、[正常停止与中性输出](D:/work/AI/yolo-study-001/native/runtime_app/runtime_loop.cpp:598)。

DXGI/CUDA/TensorRT 运行期失败会抛异常，`VisionService::run_loop()` 没有捕获/转交异常，线程边界使它进入 `std::terminate`。main 上的 try/catch 无法接到 worker 的异常，正常的停止、telemetry 收尾和中性报告路径也不会执行。

测试只替换了 `IVisionServicePoller`，在 worker 中注入异常。实际进入 terminate handler，探针以特征退出码 **86** 结束。测试 handler 只负责记录证据、避免 crash dialog，没有改变生产 VisionService。

建议在 worker 边界保存失败状态/错误并唤醒 runtime，立即撤销 Vision 权限，再让运行循环明确选择有序退出或由 capture owner 执行其已定义的恢复策略。不能只 catch 后继续发布上一帧。测试应验证 worker 失败之后的输入权限、中性输出、join 和日志结束；本轮只证明了异常传播缺陷，没有验证实体 ViGEm 设备的故障退出行为。

证据：[worker 异常日志](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/worker-exception-observed.log)。

**F6 · [P2] 配置允许 2000 Hz，但目标协调器按至少 1 ms 积分**

位置：[合法 tick 频率范围](D:/work/AI/yolo-study-001/native/controller_native/runtime_config.cpp:860)、[dt 下限](D:/work/AI/yolo-study-001/native/controller_native/target_coordinator.cpp:210)、[目标内手动修正积分](D:/work/AI/yolo-study-001/native/controller_native/target_state_reducers.cpp:128)、[边界保持计时](D:/work/AI/yolo-study-001/native/controller_native/target_state_reducers.cpp:144)。

配置接受 100–2000 Hz；TargetCoordinator 却把真实的 0.5 ms 更新时间强制放大到 1 ms。DesiredPointReducer 用这个 dt 累积 D 的移动与边界停留时间，于是合法地提高 tick 频率会改变控制行为。

固定同一目标、向下修正输入 -0.8、默认 180 ms traversal、同样 30 ms 的真实经过时间：

| tick 频率 | D 的纵向移动 | 归一化终点 |
| --- | ---: | ---: |
| 500 Hz | 10.6667 px | 0.633334 |
| 1000 Hz | 10.6666 px | 0.633333 |
| 2000 Hz | 21.3332 px | 0.766665 |

前两档一致，2000 Hz 几乎正好翻倍。边界退出的时间累计共享同一错误 dt，也会提前。默认 1000 Hz 不出现这个倍速问题，不能据此否定它当前的全部手感表现。

建议在协调器保留真实的正时间差，对真正异常时钟单独处理；将“改变控制 tick 不改变相同墙钟时间内 D 的轨迹和退出时间”纳入 500/1000/2000 Hz 契约。

证据：[频率复现日志](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/cadence.log)。

**F7 · [P2] Fusion 的一次读取不保证是同一帧快照**

位置：[seqlock 循环](D:/work/AI/yolo-study-001/native/overlay_canvas/fusion_canvas.cpp:330)、[返回共享数组指针](D:/work/AI/yolo-study-001/native/overlay_canvas/fusion_canvas.cpp:343)、[循环外才复制 detections](D:/work/AI/yolo-study-001/native/overlay_canvas/fusion_canvas.cpp:2157)。

同一读取边界有两个具体问题。其一，`do { ... if (seq & 1) continue; ... } while (seq != slot.write_sequence)` 中的 continue 会进入循环条件；如果奇数序号没变，循环直接结束，返回成功，却没有读取有效 payload。其二，序号校验只覆盖 target/geometry 等拷贝，detections 返回的是共享内存指针。reader 被抢占、writer 再发布两帧复用槽后，调用者复制的是另一帧的数组。

探针直接 include 生产 `fusion_canvas.cpp` 并改名其 main，只运行 reader，不创建覆盖窗口：

- `sequence=1` 时，`try_read()` 返回成功，但 `returned_frame=0`、实际槽 frame=1。
- 成功读取 frame=1 时 detection.x1=11；模拟 writer 经过另一槽并回到原槽后，返回的 frame 仍为 1，而同一 detection 指针的 x1 已变成 33（frame=3）。

第一个问题会影响普通 marker 更新；第二个问题在显示全部检测框时可表现为检测框与目标/几何错帧。Fusion 没有控制权限，因此不把这个问题描述成直接产生错误摇杆输出。

建议由 reader 拥有完整本地快照，在同一奇偶序号校验区间内复制全部需要的字段和有界 detection 数组。读取重试应有上限，读不到稳定帧时返回未更新，保留已有陈旧数据超时策略；不能阻塞 writer，也不能让 Canvas 无限等待写槽。

证据：[Fusion 复现日志](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/fusion-reader-probe.log)、[生产 reader 探针](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/probes/fusion_reader_probe.cpp)。

**F8 · [P2] recoil profile 播放把文件处理放进每个开火 tick**

位置：[compute 每次选择 profile](D:/work/AI/yolo-study-001/native/controller_native/recoil_compensation.cpp:84)、[读取发生在 active profile 比较之前](D:/work/AI/yolo-study-001/native/controller_native/recoil_compensation.cpp:142)、[目录扫描与逐文件解析](D:/work/AI/yolo-study-001/native/controller_native/recoil_profile.cpp:295)。

开启 `profile_playback_enabled` 后，每次开火 compute 都读取识别状态、扫描全部 JSON、解析数组、复制候选并排序，最后才比较是否仍是同一个 profile。因此“profile 没变”并不省去 I/O 或解析工作。这条路径在 RecoilReducer 内、最终 ViGEm 提交之前执行。

在 i5-13600KF、Release 下，使用本轮新建的固定小型 JSON、预热 20 次、每档测量 500 次，结果如下。各档始终保持相同 weapon/aim mode 和识别状态：

| 目录内 profile 数 | 单次 P50 | 单次 P95 | 500 次 heap allocations |
| --- | ---: | ---: | ---: |
| 1 | 0.1648 ms | 0.2929 ms | 24,000 |
| 16 | 1.0102 ms | 2.3427 ms | 282,000 |
| 64 | 4.0683 ms | 7.0162 ms | 1,080,000 |

16 个很小的 profile 已经耗尽默认 1 kHz 的 1 ms tick 预算。数字是本机、预热文件缓存条件下的 CPU 微基准，不是完整游戏性能测试，真实大文件或文件系统竞争可能具有不同尾延迟。

当前 [本机配置](D:/work/AI/yolo-study-001/config.toml:127) 和 [示例配置](D:/work/AI/yolo-study-001/config.native.example.toml:142) 均关闭 profile 播放，因此不能把这项开销归因于当前默认运行的每个 tick。它是仍受支持的功能开关下的性能缺陷。

建议由低频配置/识别 owner 读取、验证并发布不可变 profile；实时 recoil 只读取已选定的数值表。profile 选择和替换需要一个明确版本，不能在每个 tick 重新发现全部文件。验证应断言相同 profile 连续开火时没有文件访问，且切换武器/ADS/hipfire 后的时间线正确。

证据：[性能日志](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/recoil-io.log)。

**性能优先级：已测量但不应夸大的开销**

[VisionService::latest_snapshot](D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:159) 在锁内按值复制整个 result。RuntimeLoop 在获得完整副本后才检查 sequence，所以 1 kHz controller 即便没有新帧，也会重复复制 detections。单线程、无 worker 竞争、每档 100,000 次的探针中，只要 detections 非空，基本每次都发生一次堆分配；16/64/256 detections 的平均调用时间约为 **0.064/0.075/0.195 μs**。

这证明存在可以消除的重复分配，**并不证明它是当前毫秒级瓶颈**。探针没有测锁竞争和被抢占后的 P99。建议在 mailbox API 内先比较消费者 sequence，仅新帧才复制；固定容量缓冲是否值得采用，应再由真实分配计数与尾延迟决定。不能仅因发现 mutex 就宣称需要重写全部并发架构。

证据：[mailbox 微基准](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/mailbox.log)。

**模块覆盖与未建立的结论**

| 模块 | 本轮检查内容 | 仍然缺少的证据 |
| --- | --- | --- |
| Vision | DXGI、CUDA map/readback、TensorRT binding/Graph、resize、selector 身份/颜色路径、adapter | 真实图像集准确率 A/B，GPU 内容一致性压力测试，显示变化恢复 |
| Controller / tracking | TargetCoordinator、ADS/BodyLock、响应估计器、命令历史、D 修正、shaper、意图和生命周期 | 有实体输出反馈的时序实验、目标运动/后坐力/FOV/噪声匹配实机 A/B |
| Fusion | IPC reader/publisher、marker 布局、小窗口渲染、隔离状态和恢复路径 | DWM/显示模式切换后的帧时间、实际 resize、真实捕获排除验证 |
| AutoFire / output | 新鲜度消费、fire gate、recoil 边界、OutputComposer、ViGEm 恢复和输入读取路径 | 实体设备断连/重连、异常退出时最终中性状态 |
| Runtime / telemetry | tick 调度、mailbox、阶段时间、队列及收尾路径 | 游戏并发条件下的 P95/P99、日志竞争和 I/O 故障注入 |
| Voice / legacy | 检查构建源集和目录边界 | voice 当前为资料/设计包，不在本轮生产 CMake 链路；Python 旧路径未做逐模块审查 |

已检查的代码保持了 recoil 不读取目标误差、单一最终输出 owner、selector generation 的持久身份边界以及退役低频 proposal 配置的限制。未发现需要撤销这些边界的证据。此次是针对当前生产主链及相邻组件的审查，不是对所有 native 文件的逐行形式化证明。

两个适合下一轮专门验证的问题：响应估计命令历史在 ViGEm 实际成功交付之前记账，异常交付后的估计应如何失效；critical telemetry 仍可能等待互斥锁，调度竞争下的 tail budget 是否满足目标。这里仅列为验证任务，没有追加为已复现缺陷，也没有宣称最新 ADS estimator 已经通过真实游戏验收。

**验证记录与使用方式**

- 独立构建：[build.log](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/build.log)。MSVC 19.44.35215.0、CUDA 13.1.115、TensorRT 10.15.1.29、Release、CUDA architectures=75，CUDA 架构与当前缓存配置保持一致。本轮设置 `NATIVE_ENABLE_VIGEM=OFF`，未连接实体输出设备；有 CUDA SDK 字符编码 C4819 警告。
- 产品门禁：[基线 CTest 完整记录](D:/work/AI/yolo-study-001/artifacts/native-review-fixes-20260905/baseline-LastTest.log)。`NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS=OFF`，11 组全部通过；不是对历史 26 组结果的复用。
- 最近 ADS 独立回归：[JSON](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/ads-near-target-incident.json)；trigger/counterfactual 有效，`overall_pass=true`。只代表该冻结 fixture。
- 复现实现：[CMakeLists](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/probes/CMakeLists.txt)、[runtime 探针](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/probes/runtime_review_probe.cpp)、[Fusion 探针](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/probes/fusion_reader_probe.cpp)。
- 证据清单及源文件 SHA-256：[review-manifest.json](D:/work/AI/yolo-study-001/artifacts/native-review-20260905/review-manifest.json)。

探针的 `freshness`、`post_sample_capture`、`fusion_reader_probe` 返回 0 表示按预期重现当前问题，**不表示产品通过回归**。`worker_exception` 的 86 是专门记录 terminate 的预期退出码。它们是审查证据，后续修复时应转成断言正确行为、旧代码失败的新回归用例。

下面的命令从仓库根目录执行；先用已安装的 CMake 设置 `$cmake`，同目录的 `ctest.exe` 可运行产品门禁。所有路径指向本轮独立构建：

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build artifacts/native-review-20260905/build --config Release
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir artifacts/native-review-20260905/build -C Release --output-on-failure
& $cmake --build artifacts/native-review-20260905/probe-build --config Release
& artifacts/native-review-20260905/probe-build/Release/runtime_review_probe.exe post_sample_capture
& artifacts/native-review-20260905/probe-build/Release/runtime_review_probe.exe freshness
& artifacts/native-review-20260905/probe-build/Release/runtime_review_probe.exe cadence
& artifacts/native-review-20260905/probe-build/Release/runtime_review_probe.exe worker_exception
& artifacts/native-review-20260905/probe-build/Release/fusion_reader_probe.exe
& artifacts/native-review-20260905/probe-build/Release/runtime_review_probe.exe mailbox
# recoil_io 要求全新的目录，避免覆盖既有 fixture。
$recoilFixture = 'artifacts/native-review-20260905/recoil-fixture-' + [guid]::NewGuid().ToString('N')
& artifacts/native-review-20260905/probe-build/Release/runtime_review_probe.exe recoil_io $recoilFixture
```

**建议修复顺序**

先处理 F1/F2 的时间和 freshness 根因，以及 F3/F4 的 capture/interop 正确性；然后处理 worker 故障边界、2000 Hz 时间积分和 Fusion 快照。recoil profile I/O 在启用该功能之前必须移出实时链。mailbox 的重复分配可以作为低风险、可量测的后续优化。

每个修复先固定当前复现输入、身份和门限，再做失败到通过的回归；离线门禁通过后，仍需同配置、同武器、同 FOV 和输出路径的实机 A/B 与用户手感确认。此次没有运行 AimLab 排名或调参，没有生成“整体性能已提升”或“实战已验收”的结论。

建议将本报告链接与 8 项问题的待修复状态同步到 `.agent-context/`，以免现有“测试 GREEN”交接掩盖本轮新增的覆盖缺口。本轮未修改项目上下文文件。
