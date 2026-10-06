# 当前项目模型与任务进度

> 2026-10-06 更新：保留的 Tk GUI 已完成重新排版及参数语义统一；原生加入 32 ms 有界漏检续跟、Vision 高精度等待和连续近点收尾／短历史速度估计。最终验证为 Release、50 组 CTest、111 项桌面检查通过；最新 Apex 实机手感未确认。详见 [配置与检查](CONFIGURATION_AND_CHECKS.md)、[工作记录](../../.agent-context/handoff.md) 和 [产物索引](../../output/README.md)。以下 10 月 4 日的合并叙述是历史结构基线，“未改 GUI／控制算法”仅描述当次合并。


更新：2026-10-04。现行路径为 dev 的 C++ runtime 与 Python/Tk 配置工作室。用户最新要求合并原生重构，但排除新 C++ GUI、保留 dev GUI。Python 仅保留桌面配置、曲线、进程管理、IPC 和界面检查；控制器、视觉 fallback、训练及分析路径继续退役。

## 项目 → 业务组成 → 能力

项目把物理设备输入与屏幕识别结果合成为辅助输出，并提供配置工作室、可视化、诊断与数值仿真。

| 组成/能力 | 用途、入口与输入输出 | 所有者、依赖与调用方 | 验证和未知 |
| --- | --- | --- | --- |
| 桌面配置工作室 | [GUI](../../python/desktop_app/gui.py)：独立配置、游戏/模型、曲线、运行反馈 | 保留 dev 的 [workspace](../../python/desktop_app/workspace.py)、[RuntimeManager](../../python/desktop_app/runtime.py) 及 [IPC](../../python/desktop_app/control.py)；Python 仅在 UI/管理层使用，Tk/标准库；不参与控制 tick | 保留 test_desktop 三个模块；当前界面沿用 dev，新 C++ GUI 不合入 |
| 启动/配置 | gamepad_start → [main](../../native/runtime_app/main.cpp)；mouse_start → [mouse main](../../native/runtime_app/mouse_runtime_main.cpp)；TOML/profile/game/environment/CLI → RuntimeConfig | [runtime_config](../../native/controller_native/runtime_config.cpp) 解析/派生/验证；main 施加 CLI 覆盖；Loop 保存生效配置 | BaseContracts；显式无效标量/损坏行拒绝，缺省项默认；支持当前配置子集，非完整 TOML 实现 |
| 感知 | [VisionEngine](../../native/vision_native/src/vision_engine.cpp)：DXGI → CUDA 预处理 → TensorRT → selector → VisionResult | Engine 拥有捕获/推理/selector；[共享 Adapter](../../native/runtime_app/vision_engine_service_poller.h) 唯一持有 Engine，Service 拥有线程/latest-only mailbox；手柄/鼠标共用 | BaseVisionSelection、BaseRuntimeFreshness；真实引擎/policy 热更已运行；游戏识别效果未测 |
| 物理手柄输入 | [Loop](../../native/runtime_app/runtime_loop.cpp) 读取 SDL/XInput → PhysicalGamepadState | Loop 拥有 reader/选定设备/重连节流；[IO recovery](../../native/controller_native/io_recovery_policy.cpp) 处理设备边界 | 原生协议/reader 测试；设备插拔未重测 |
| 目标与控制 | [NativeGamepadController](../../native/controller_native/native_gamepad_controller.cpp)：物理输入/视觉快照 → TargetPlan/ControlFrame | reducers → TargetCoordinator → ADS/BodyLock → Dynamics → AssistControlStateMachine → AutoFire/recoil；阶段有不同状态语义，不合并为“当前目标” | BaseAds、BaseBodyLock、BaseEndToEnd；保留零死区、15%–30% intent、身份/交接及完整 lockstep |
| 手柄输出 | [OutputComposer](../../native/controller_native/output_composer.cpp) 一次合成 → [VirtualGamepad](../../native/controller_native/virtual_gamepad.cpp) ViGEm DS4 → delivery observation | Composer 拥有最终合成；设备 adapter 拥有资源；Loop 按启动 output.enabled 创建 unique_ptr，禁用不打开 ViGEm | 输出/AutoFire/recoil 基础与功能测试；真实接收端未验证 |
| 配置热更/学习 | GUI IPC client → 命名事件/共享内存 → [Bridge](../../native/runtime_app/runtime_control_bridge.cpp) → tick commit → client 状态 | Bridge 后台 loader/immutable candidate；[reload policy](../../native/runtime_app/runtime_reload_policy.cpp) 决定 restart/学习保留；Loop 等当前 policy fresh snapshot 后提交；protocol=1，snapshot=640 bytes | IPC 功能及先前隔离分支真实 client 联合运行；pending 不等于 applied |
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

停止/故障：reset 控制权，启用输出时发送 neutral，停止 Vision，排空 collectors/writer/会话；Bridge 先 join 再 unmap/CloseHandle。鼠标由 session/supervisor 保有物理路径和按钮释放责任。保留的 RuntimeManager 使用 PID、创建时间和可执行路径核验归属，记录在 runs/runtime/background；GUI IPC 使用相同 protocol=1。独立游戏配置无需 games 继承块，但拒绝 CLI 游戏身份不匹配。

手动原始 passthrough 保持零死区，AI intent 权重在 15%–30% 连续变化。目标搜索/取得/身份/交接、同 tick 全链、实际交付学习配对及释放行为保持。退役 ai_proposal knobs 继续 unknown/inert。

## 结构判断与修改位置

已有控制器各阶段、VisionService/DeliveryGate、鼠标 session/supervisor 有独立不变量，保留边界。此次简化集中在 runtime 编排、IPC 和诊断重复状态：共享 Engine Adapter；reload policy 共用有序差异遍历；Bridge 实现移出 header、kernel 资源 RAII；Loop 控制/诊断分开；93 项遥测标量使用现有 payload，AcquisitionTrace 不再有平行 DTO；Fusion 转换由 publisher 持有。没有空接口、深继承或常驻治理服务。

新增配置先查 loader/派生/CLI/reload policy 各自输入责任；新增诊断标量从现有 payload 和生产者开始，再改 serializer/reader，避免平行 DTO。新增控制行为沿真正的状态所有者和交付边界修改，不能用开关/延迟/吞错补偿重复状态。

两个前轮边界修复有失败证据：显式坏标量被静默接受；output disabled 仍创建 ViGEm。本轮迁移再确认并修复损坏配置行被跳过，以及 Windows 混合分隔符导致 owned process 被误判路径不符。都在生产者/所有者修正，未改控制算法。

## 测试与删除取舍

C++ Base 覆盖配置/协议、视觉、freshness、ADS、BodyLock 和端到端；Feature/Mouse/Fusion 保留当前功能、采样与设备生命周期。当前产品 authority/manual-correction 断言已从旧 incident 的 CLI/报告驱动归入基础契约。高频观测/学习、近期 zero crossing/motion anchor 等独特触发仍保留。

历史测试精简提交 `cf2da5c` 删除 117 个文件（74 Python tests、29 native 历史驱动、14 test-only fixtures），并删 13 个源码形状断言。随后用户授权全量 Python 退役：删除剩余 Python 源码/测试/配置依赖、桥接扩展及工具入口，原来的 Python 基础/可选分组也不再存在。旧详细 gameplay/研究/incident 覆盖有意退出，不声称剩余集合等价覆盖。

离线数值工具保留为 opt-in，分组数不等于场景数或效果证明。损坏配置输入等边界使用原生基础检查；GUI 功能保留 dev 的三个测试模块。新 C++ 桌面代码/测试/目标已排除。旧 SHA256 校验和 benchmark 发布裁决继续退役。

## 本次任务记录与实证

先前隔离分支的 runtime 职责重构为 1e7e149，测试精简为 cf2da5c，Python 退役及原生 GUI 迁移为 d1494f5。新 C++ GUI 的启动隐藏问题已修复，但用户随后明确选择沿用 dev GUI，故整个新 GUI 及其测试/入口不合入。

dev GUI 更新基线为 0ab4020；合并前未提交清理与源码在 05d1c83 保存。保留 GUI 源码、VBS、后台/Fusion 启动和 GUI 文档；保留独立游戏配置解析及其回归。合入原生结构重构、严格配置解析、按需输出资源、测试精简、pybind/非 GUI Python 路径退役以及原生 DLL 构建输入。未改 GUI 实现或控制算法，个人资产及 output 下未跟踪资料保留。

历史实测：隔离分支全目标 Release 构建与 CTest 51/51 组通过（包含后来排除的窗口测试）。真实输出关闭的 TensorRT 启动、控制/视觉 policy 热更、停止和独立 Vision debug 通过。合并后 dev 的全目标 Release 构建通过，原生 CTest 50/50 组、GUI unittest 66/66 通过；保留的 Python RuntimeManager/ControlChannel 对合并后的真实 TensorRT runtime 完成 output-disabled 启动、热更 revision 0→1 和正常停止。GUI、VBS、三个后台/Fusion 启动脚本与 05d1c83 无差异。

边界：真实手柄/鼠标接收端、Canvas 捕获排除及现场游戏感受没有新验证；无现成 ONNX，未真实执行新 engine 导出。原分支 ignored Python/pytest 缓存递归清理曾被自动审批 blocked by policy 拒绝，未绕过；本次不清理这些历史残留。原生构建无 Python 依赖，但 GUI 依赖标准 Python/Tk 环境。
