# 当前项目模型与任务进度

更新：2026-10-04。事实依据是隔离分支 `codex/cognition-refactor-20261004` 的现行源码。用户最新确认：项目只保留 C++ 运行、构建和工具路径；有用能力迁到 C++，其余 Python 实现退役。旧文档/handoff 不能恢复已删除路径或旧验收框架。

## 项目 → 业务组成 → 能力

项目把物理设备输入与屏幕识别结果合成为辅助输出，并提供原生配置助手、可视化、诊断与数值仿真。

| 组成/能力 | 用途、入口与输入输出 | 所有者、依赖与调用方 | 验证和未知 |
| --- | --- | --- | --- |
| 原生助手 | [desktop_main](../../native/desktop_native/desktop_main.cpp)：游戏选择、TOML 编辑/校验/保存、启停、热更、学习展示 | [DesktopSession](../../native/desktop_native/desktop_session.cpp) 拥有文件/进程/IPC adapter；Win32 窗口拥有草稿，异步执行慢操作；启动 VBS/后台 PS 共用 C++ session | BaseContracts 保存/冲突/Unicode/参数边界；FeatureDesktopWindow；完整人工 GUI 体验未验证 |
| 启动/配置 | gamepad_start → [main](../../native/runtime_app/main.cpp)；mouse_start → [mouse main](../../native/runtime_app/mouse_runtime_main.cpp)；TOML/profile/game/environment/CLI → RuntimeConfig | [runtime_config](../../native/controller_native/runtime_config.cpp) 解析/派生/验证；main 施加 CLI 覆盖；Loop 保存生效配置 | BaseContracts；显式无效标量/损坏行拒绝，缺省项默认；支持当前配置子集，非完整 TOML 实现 |
| 感知 | [VisionEngine](../../native/vision_native/src/vision_engine.cpp)：DXGI → CUDA 预处理 → TensorRT → selector → VisionResult | Engine 拥有捕获/推理/selector；[共享 Adapter](../../native/runtime_app/vision_engine_service_poller.h) 唯一持有 Engine，Service 拥有线程/latest-only mailbox；手柄/鼠标共用 | BaseVisionSelection、BaseRuntimeFreshness；真实引擎/policy 热更已运行；游戏识别效果未测 |
| 物理手柄输入 | [Loop](../../native/runtime_app/runtime_loop.cpp) 读取 SDL/XInput → PhysicalGamepadState | Loop 拥有 reader/选定设备/重连节流；[IO recovery](../../native/controller_native/io_recovery_policy.cpp) 处理设备边界 | 原生协议/reader 测试；设备插拔未重测 |
| 目标与控制 | [NativeGamepadController](../../native/controller_native/native_gamepad_controller.cpp)：物理输入/视觉快照 → TargetPlan/ControlFrame | reducers → TargetCoordinator → ADS/BodyLock → Dynamics → AssistControlStateMachine → AutoFire/recoil；阶段有不同状态语义，不合并为“当前目标” | BaseAds、BaseBodyLock、BaseEndToEnd；保留零死区、15%–30% intent、身份/交接及完整 lockstep |
| 手柄输出 | [OutputComposer](../../native/controller_native/output_composer.cpp) 一次合成 → [VirtualGamepad](../../native/controller_native/virtual_gamepad.cpp) ViGEm DS4 → delivery observation | Composer 拥有最终合成；设备 adapter 拥有资源；Loop 按启动 output.enabled 创建 unique_ptr，禁用不打开 ViGEm | 输出/AutoFire/recoil 基础与功能测试；真实接收端未验证 |
| 配置热更/学习 | native client → 命名事件/共享内存 → [Bridge](../../native/runtime_app/runtime_control_bridge.cpp) → tick commit → client 状态 | Bridge 后台 loader/immutable candidate；[reload policy](../../native/runtime_app/runtime_reload_policy.cpp) 决定 restart/学习保留；Loop 等当前 policy fresh snapshot 后提交；protocol=1，snapshot=640 bytes | IPC 功能/真实 C++ client 联合运行；pending 不等于 applied |
| 鼠标辅助 | [session](../../native/mouse_native/mouse_controller_session.cpp) 处理 counts/buttons/wheel → 共用控制器 → 单一输出 | session 拥有 transport、转换/校准与释放；[supervisor](../../native/mouse_native/mouse_runtime_supervisor.cpp) 拥有停顿/失联恢复；Interception/VirtualHid/Win32 debug 均仍实际存在 | Mouse 契约/negative controls；硬件安装/接收端未知 |
| 展示/诊断 | [Fusion publisher](../../native/runtime_app/fusion_channel_publisher.cpp) → shared channel → [Canvas](../../native/overlay_canvas/fusion_canvas.cpp)；[Loop diagnostics](../../native/runtime_app/runtime_loop_diagnostics.cpp) → [Collectors](../../native/runtime_app/telemetry_collectors.cpp) → writer | publisher 独自转换显示坐标/维护可用性；Collectors 拥有采样/事件状态；Telemetry/Perf/Session 各管线程/文件；均不拥有控制权 | FeatureTelemetry、Fusion；共享 channel 已实测，Canvas 捕获排除未重测 |
| 原生 recoil 工具 | controller recoil 与隔离 recoil_profile_tools | 当前阶段及已有原生资料读取保留；Python sidecar/识别/采集工具已删 | FeatureRecoilAndWeapon；不声称替代旧采集/训练能力 |
| 仿真/benchmark | [OfflineBenchmarks](../../native/cmake/OfflineBenchmarks.cmake)：scenario/seed/plant → 数值结果；SDK/GPU/scheduler 测量工具显式运行 | 仿真拥有 plant/场景，复用生产控制器；不持有生产设备 | 可选测试保留；不恢复 SHA256 manifest、比较门禁或发布裁决；仿真不等于现场验收 |
| 模型准备 | 已有 engine；[build_engine.ps1](../../tools/build_engine.ps1) 调用 SDK trtexec 将已有 ONNX → 新 engine | 外部 SDK 原生工具；捕获与 tensor 尺寸由 loader/Engine 校验 | SDK help 参数已核对；无可用 ONNX，未执行新模型构建 |
| 研究/历史/资产 | 声音设计包、archive、models、runs、artifacts、training_data | 声音包不在主构建；旧报告只解释历史；个人模型/数据/配置/曲线预设未删除 | 不推断资产均可删除，也不把未接入的研究当主线依赖 |

统一 [CMake](../../native/CMakeLists.txt) 依赖 MSVC、CUDA、TensorRT 与 Windows SDK；无 Python/pybind11。SDL2/ViGEmClient 从明确的本机原生 DLL 输入定位，不再查脚本包目录。[runtime_deps](../../runtime_deps/README.md) 说明新机器准备方式。

## 生命周期与有效约定

初始化：main 严格加载/覆盖配置，dump/probe 在设备之前返回。Loop 创建输入、日志/会话、按启用状态创建输出，再创建 Engine；GPU service 路径将 Engine 转给 Adapter/Service，direct poll 仍是实际替代路径。停止 listener 的 RAII scope 在 Loop 销毁前 join。

正常运行：物理输入 begin_tick → request/intent → epoch/policy 匹配 → DeliveryGate identity/source age → submit_vision_result → resolve → compose → device delivery → observation/learning → diagnostics。没有新 frame 不伪造观测；新 frame 无目标是另一种事件。Service 和 DeliveryGate 保护不同不变量。

配置变更：助手保存文件不改变运行状态；Bridge 后台读文件/验证/diff → rejected/restart/pending。Loop tick 消费 immutable candidate，视觉 policy 变化先申请 revision，等该 revision fresh snapshot 后才更新 config/controller 并 complete；Bridge ACK 后发布 applied。direct poll 不支持相同视觉 policy 热更，鼠标没有手柄 IPC 热更入口。

停止/故障：reset 控制权，启用输出时发送 neutral，停止 Vision，排空 collectors/writer/会话；Bridge 先 join 再 unmap/CloseHandle。鼠标由 session/supervisor 保有物理路径和按钮释放责任。助手只对匹配 PID+创建时间+规范化可执行路径的实例发送 stop event；不按进程名终止。挂起子进程在归属记录成功前不会进入控制循环。

手动原始 passthrough 保持零死区，AI intent 权重在 15%–30% 连续变化。目标搜索/取得/身份/交接、同 tick 全链、实际交付学习配对及释放行为保持。退役 ai_proposal knobs 继续 unknown/inert。

## 结构判断与修改位置

已有控制器各阶段、VisionService/DeliveryGate、鼠标 session/supervisor 有独立不变量，保留边界。此次简化集中在 runtime 编排、IPC 和诊断重复状态：共享 Engine Adapter；reload policy 共用有序差异遍历；Bridge 实现移出 header、kernel 资源 RAII；Loop 控制/诊断分开；93 项遥测标量使用现有 payload，AcquisitionTrace 不再有平行 DTO；Fusion 转换由 publisher 持有。没有空接口、深继承或常驻治理服务。

新增配置先查 loader/派生/CLI/reload policy 各自输入责任；新增诊断标量从现有 payload 和生产者开始，再改 serializer/reader，避免平行 DTO。新增控制行为沿真正的状态所有者和交付边界修改，不能用开关/延迟/吞错补偿重复状态。

两个前轮边界修复有失败证据：显式坏标量被静默接受；output disabled 仍创建 ViGEm。本轮迁移再确认并修复损坏配置行被跳过，以及 Windows 混合分隔符导致 owned process 被误判路径不符。都在生产者/所有者修正，未改控制算法。

## 测试与删除取舍

C++ Base 覆盖配置/协议、视觉、freshness、ADS、BodyLock 和端到端；Feature/Mouse/Fusion 保留当前功能、采样与设备生命周期。当前产品 authority/manual-correction 断言已从旧 incident 的 CLI/报告驱动归入基础契约。高频观测/学习、近期 zero crossing/motion anchor 等独特触发仍保留。

历史测试精简提交 `cf2da5c` 删除 117 个文件（74 Python tests、29 native 历史驱动、14 test-only fixtures），并删 13 个源码形状断言。随后用户授权全量 Python 退役：删除剩余 Python 源码/测试/配置依赖、桥接扩展及工具入口，原来的 Python 基础/可选分组也不再存在。旧详细 gameplay/研究/incident 覆盖有意退出，不声称剩余集合等价覆盖。

离线数值工具保留为 opt-in，分组数不等于场景数或效果证明。损坏配置输入、原生助手文件冲突/Unicode/身份参数及隐藏窗口创建使用少量必要原生检查。旧 SHA256 校验和 benchmark 发布裁决继续退役。

## 本次任务记录与实证

工作路径 `yolo-study-001-refactor`，分支 `codex/cognition-refactor-20261004`。继承基线 `1251273` 保留原工作区当时 tracked 变更与未跟踪源码/文档；`1e7e149` 是 runtime 职责重构，`cf2da5c` 是测试精简。本轮 C++-only 迁移另行提交。原工作区没有覆盖或合并；未把目标项目资料写入 project-cognition。

已完成：原生助手选定能力迁移；Python 项目路径/构建依赖/启动入口退役；共享 IPC 协议提取；原生 DLL 输入；Vision debug 支持显式 engine；训练/旧 benchmark/桥接/分析工具停止维护。曲线绘图与预设库界面没有迁移，原生配置语义和个人资产保留。

实测：fresh CMake 检测只发现 MSVC/CUDA，cache 无 Python/pybind 条目，全目标 Release 构建通过。真实原生助手 C++ client 启动 TensorRT runtime，纯控制项热更 revision 0→1 保留学习，视觉 policy 热更 1→2 清空学习，在命名事件停止后 status=stopped。设备输出关闭，无 ViGEm 创建。隐藏窗口配置控件检查和保存/冲突/Unicode 基础检查通过；损坏配置行回归由失败转为通过。最终 CTest 51/51 组通过（含 opt-in 数值场景）；独立 Vision debug 真实 DXGI/CUDA/TensorRT 三帧完成；保留 PowerShell 脚本 AST 解析和后台 start/stop/Fusion 的 PrintOnly 检查通过。

边界：未重测真实手柄/鼠标接收端、Canvas 窗口捕获排除、现场游戏表现、独立协调 review 或完整人工 GUI 易用性；不能宣称手感/速度改善。没有现成 ONNX，SDK engine 构建脚本未跑真实导出。旧 ignored Python/pytest 缓存清理被自动审批以 blocked by policy 拒绝；没有绕过，源码与运行/构建路径已移除，缓存不参与现行流程。