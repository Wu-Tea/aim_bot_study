# 当前项目模型与重构进度

更新：2026-10-04。范围：`codex/cognition-refactor-20261004` 隔离 worktree，以 C++ 原生运行链路为主。

本文帮助定位能力、状态和修改影响。**事实**来自下列源码；**判断/提案**在末尾单列。原工作区的未提交修改已在隔离 worktree 中形成继承基线 `1251273`；本轮改动在其后单独提交。本文描述该重构分支，不代表已发布版本。旧 handoff 与历史测量只解释当时的决定，不能覆盖现行源码和本次退役决定。

## 项目与业务组成

项目把实际设备输入和屏幕识别结果合成为辅助控制输出，并提供桌面配置、可视化、训练和诊断工具。默认手柄及鼠标入口都是 C++；Python GUI 是实际使用方，Python gameplay 是显式选择的替代路径。

| 业务组成 / 能力 | 入口、用途与输入输出 | 状态所有者及依赖 | 验证与调查边界 |
|---|---|---|---|
| 启动与配置 | [手柄启动](../../scripts/launch/gamepad_start.bat) → [main.cpp](../../native/runtime_app/main.cpp)；TOML、profile、game、环境与 CLI → RuntimeConfig → RuntimeLoop | [runtime_config.cpp](../../native/controller_native/runtime_config.cpp) 负责解析/派生；main 负责 CLI 覆盖；Loop 保留生效配置。依赖模型、设备 DLL、Windows 与 SDK | BaseContracts；具体速率/尺寸取当前有效配置，示例值不等于本机生效值 |
| 屏幕感知 | [VisionEngine](../../native/vision_native/src/vision_engine.cpp)：DXGI 画面 → CUDA 预处理/resize → TensorRT → selector → VisionResult | Engine 拥有捕获/推理/selector；[VisionService](../../native/runtime_app/vision_service.cpp) 拥有线程和 latest-only mailbox；共享 [Adapter](../../native/runtime_app/vision_engine_service_poller.h) 唯一持有 Engine | BaseVisionSelection、BaseRuntimeFreshness；本次真实 GPU/桌面捕获及 policy 热更通过，游戏目标表现未验证 |
| 物理手柄输入 | [RuntimeLoop](../../native/runtime_app/runtime_loop.cpp) 读取 SDL 或 XInput → PhysicalGamepadState → begin_tick | Loop 拥有输入 reader、选择的设备和重连策略；[IO recovery](../../native/controller_native/io_recovery_policy.cpp) 处理真实设备故障 | 输入协议/reader 功能测试；设备插拔和排除虚拟设备的现场结果未在本次重测 |
| 目标与控制 | [NativeGamepadController](../../native/controller_native/native_gamepad_controller.cpp)：物理输入 + 视觉快照 → ControlFrame / TargetPlan / learning snapshot | 输入 reducers → TargetCoordinator → ADS 或 BodyLock → dynamics → authority → AutoFire/recoil；各状态所有者见下节 | BaseAds、BaseBodyLock、BaseEndToEnd；数值仿真有独立工具，不能替代真实使用 |
| 手柄输出 | [OutputComposer](../../native/controller_native/output_composer.cpp) 将已归属的 ControlFrame 合成一次；[VirtualGamepad](../../native/controller_native/virtual_gamepad.cpp) → ViGEm DS4 | Composer 拥有合成结果；设备适配器拥有实际输出资源；成功交付再回传学习/诊断信息 | FeatureAutoFireAndMarker、输出协议测试；真实接收端未在本次检查 |
| 配置变更与学习展示 | [Python control](../../python/desktop_app/control.py) → 命名事件/共享内存 → [RuntimeControlBridge](../../native/runtime_app/runtime_control_bridge.cpp) → tick 提交 | Bridge 后台负责读文件、不可变候选与 GUI 状态；[reload policy](../../native/runtime_app/runtime_reload_policy.cpp) 决定可热更范围/学习保留；Loop 是生效边界 | BaseContracts 的 IPC/配置测试与 desktop_app 测试；ABI protocol=1、snapshot=640 bytes、offset=16 |
| 鼠标辅助 | [mouse_start.bat](../../scripts/launch/mouse_start.bat) → [mouse_runtime_main.cpp](../../native/runtime_app/mouse_runtime_main.cpp)；捕获 counts/buttons/wheel → 共享控制器/鼠标转换 → 一次最终输出 | [MouseControllerSession](../../native/mouse_native/mouse_controller_session.cpp) 拥有 transport、校准和按钮释放；[Supervisor](../../native/mouse_native/mouse_runtime_supervisor.cpp) 负责 worker 停顿/退出恢复。共用 VisionService、Engine Adapter 与控制算法 | Mouse 系列功能测试；VirtualHid、Interception、KMDF、Win32 debug 都仍有代码/入口，具体设备安装情况未知 |
| 桌面操作 | [desktop_app GUI](../../python/desktop_app/gui.py) 提供编辑、保存、启动、停止、热更；[settings](../../python/desktop_app/settings.py) 负责配置保存冲突 | Python RuntimeManager 管进程身份/状态，ConfigStore 管文件；原生运行不依赖 Python 控制算法；GUI 的实际依赖必须保留 | Python desktop/startup 测试；本次不重写正在修改的 UI/配置曲线代码 |
| 可视化与诊断 | [Fusion publisher](../../native/runtime_app/fusion_channel_publisher.cpp) → [shared channel](../../native/shared_fusion/fusion_channel.cpp) → [fusion_canvas](../../native/overlay_canvas/fusion_canvas.cpp)；日志收集 → 有界流/会话文件 | overlay 只展示；Loop 组织日志、PerfLogger/Telemetry/LogSessionManager 管各自线程或资源，不取得目标/输出控制权 | Fusion、FeatureTelemetryAndDiagnostics；真实 publisher→共享内存已检查，实际窗口/捕获隔离未验证 |
| Python 替代运行与桥接 | [python/main.py](../../python/main.py)、controllers/vision；`GAMEPAD_RUNTIME=python` 显式选择；vision_native_cpp 为 pybind 模块 | Python 包与原生扩展仍真实存在；不是默认 C++ 控制器的依赖。桌面/构建/工具依赖需逐项区分 | Python 旧 gameplay 测试占全量较大部分；本次不据数量整批删除 |
| recoil 独立工具、训练与研究 | python/recoil_app、runtime/recoil_sidecar、training、tools/training；数据 → 训练/导出模型 → 原生 Engine 加载 | 工具拥有各自状态与产物；不是每 tick 执行依赖。原生 recoil 是共享控制器内的独立阶段 | FeatureRecoilAndWeapon；训练效果与当前模型准确率未重测 |
| 数值仿真与离线分析 | [OfflineBenchmarks.cmake](../../native/cmake/OfflineBenchmarks.cmake)、scripts/verify 的矩阵入口：场景/seed → 数值结果 | 仿真拥有 plant/场景/计时；复用生产控制器，另有合成输入；不持有生产设备 | 保留普通功能测试和数值仿真；旧 SHA256 溯源、比较门禁、发布裁决已退役 |
| 声音方向研究包 | native/voice/SoundDirectionAssist_Codex_Pack | 当前是独立需求/设计包，未列入主 CMake 构建 | 未检查音频实现或实际运行；不能当作主线依赖，也不因未接入就删除 |

构建依据：[native/CMakeLists.txt](../../native/CMakeLists.txt) 及 controller/runtime/mouse/overlay 的 targets 文件。runtime 消费 controller_native_core 与 vision_native_core；设备和桌面 IPC 在编排边界；pipeline_contract 表达跨模块数据/身份/时间约定。Vision 不依赖 runtime 或控制器实现。

## 主链路与状态归属

```text
实际输入 → begin_tick：输入边沿、scope、manual intent
                               ↓ 视觉请求、viewport、user intent
DXGI → CUDA/TensorRT → selector → VisionService mailbox
                               ↓ 唯一/递增/及时的 capture 与当前 request/policy epoch
VisionDeliveryGate → adapt_vision_result → controller vision snapshot
  → TargetCoordinator → ADS acquisition 或 BodyLock follow
  → AimDynamicsShaper → AssistControlStateMachine
  → AutoFire / recoil → ControlFrame → OutputComposer
  → 设备交付 → delivered-output observation / response learning
```

- Selector 拥有视觉关联/generation；TargetCoordinator 拥有控制侧目标身份、ADS epoch、生命周期与 TargetPlan。跨边界关系由 observation/generation/epoch 明确表达，不应合并成一个模糊的“当前目标”。
- ADS 与 BodyLock 按当前模式产生提案；Dynamics 只塑形，AssistControlStateMachine 拥有手动/AI 控制权。AutoFire 只拥有合成射击，物理射击走原生约定；recoil 不拥有目标身份。
- VisionService 的 freshness、request transition、policy revision 与 DeliveryGate 的 capture identity/source age 是不同不变量。不能因都像“新鲜度检查”就删掉其中一层。
- 无新 Vision frame 的控制 tick 不伪造观测，不靠旧时间戳续期。新帧没有目标与没有新帧是两种输入；cue 连续性受 selector generation 与权限约定限制。
- 学习依赖实际交付和观测配对；旧配置的 ACK/frame/激励锚不能作为新配置样本。仅现有学习安全控制变化允许保留估计，见 reload policy 和 apply_hot_config。
- 手动零死区 passthrough、15%–30% 平滑 intent 权重、目标搜索/取得/交接和生命周期是有效约定；本轮结构整理不改变它们。

## 初始化、正常运行、变更和退出

**初始化。** main 解析选项、加载配置并施加覆盖；显式无效标量在 parser 报错，缺省项仍使用默认值；dump/probe 提前返回。创建停止信号，再创建 Loop；Loop 准备会话/日志、输入设备和 Vision；仅 `output.enabled=true` 时创建虚拟输出。关闭输出不打开 ViGEm。启用 GPU service 时 Engine 所有权转给 Adapter/Service，否则 Loop 直接 poll Engine。随后建立 control bridge、绑定 Loop，启动停止 listener；listener 的局部 RAII scope 保证在 Loop 销毁前 join。

**正常运行。** Loop 按绝对 deadline 调度。读取物理状态、begin_tick，再更新视觉请求/intent；消费 mailbox 时匹配 request transition 与 policy revision，接受 capture 后提交视觉输入。随后 resolve、compose、设备 update，记录交付反馈与诊断。Fusion 发布是展示支路。鼠标使用相同视觉服务但有独立的 counts 转换、报告交付和 physical-path 恢复链路。

**配置变更。** GUI 保存文件并发送 request id。Bridge 后台 loader → 严格 hot boundary validation → restart/rejected 或 immutable pending。Loop 在 tick 边界消费；视觉策略变化先发 revision，并等该 revision 的 fresh snapshot，再应用控制配置、更新生效配置并 complete。GUI 的 applied 只能来自完成消息，pending 不代表新配置已生效。直接 poll Vision 路径不能热改上述视觉策略，需重启。鼠标入口没有这条手柄热更新 IPC；不能推断它支持同样热更。

**退出与故障。** Loop 正常停止或捕获异常后先 reset 控制权、输出 neutral，再停 Vision worker、排空日志并关闭会话，最后重抛错误。Bridge 先唤醒/join worker，再自动 unmap/CloseHandle。鼠标 normal exit 先 session.shutdown、标记 supervisor 完成，再停 Vision；异常/失联恢复由 session 与 supervisor 保有物理输入释放责任。本次没有删除这些设备边界处理。

## 当前、替代、历史与产物

当前：两个原生 runtime、桌面配置 GUI、原生 Vision、共享控制器、Fusion、会话日志、功能测试和仿真。实际替代：显式 Python fallback、非 GPU-service direct poll、SDL/XInput 和鼠标多个 transport。实验/工具：离线场景、GPU contention、训练、分析、声音研究包，是否常用需另证。历史：archive 与旧手册/测量，不能当作当前依赖或验收规范。生成产物：native/build、runs、output、artifacts、模型/训练数据；不能按目录名推断均可删除，其中也可能有人工保存数据。

978 是 Python 收集的测试用例数；144 是上一轮两批验证的执行次数，存在 11 个重复，实际 133 个不同用例。它们不是 144 个 Python 生产文件。本次只运行受影响的配置/启动/桥接测试；默认 C++ 不是删除所有 Python 的授权。

## 结构判断与实施顺序

**判断：** 共享控制器已经以输入 reducers、TargetCoordinator、ADS/BodyLock、Dynamics、控制权状态机和输出合成为主要职责边界。其身份、权限和交付状态有不同语义，本次保留这些边界。主要结构问题在 runtime 编排与诊断混合、重复 Engine Adapter、IPC 实现和配置规则放在 header、遥测并行字段清单，以及通用 parser 的静默默认值。

实施顺序是先集中配置/IPC/视觉边界，再拆开控制调度与诊断、消除重复数据形状，最后检查资源创建和真实初始化—热更—停止链路。第一批已继承；本次完成剩余已确认必做项：

1. 热更新规则集中于 [runtime_reload_policy](../../native/runtime_app/runtime_reload_policy.cpp)，Bridge 与 Loop 共用有序差异遍历。Bridge 的实现迁到 [cpp](../../native/runtime_app/runtime_control_bridge.cpp)，header 只保留通道协议、接口和资源成员；状态枚举明确 idle/pending/applied/restart/rejected，仍为 protocol=1、640-byte snapshot，Python 无 ABI 迁移。
2. 两入口共用 [VisionEngineServicePoller](../../native/runtime_app/vision_engine_service_poller.h)。Adapter 隔离 Engine 与异步 Service；unique_ptr 链唯一持有 Engine。Bridge 的 mapping/events/view 用 RAII 自动释放，析构先 join worker。没有新的资源框架。
3. [Loop](../../native/runtime_app/runtime_loop.cpp) 明确按输入、视觉请求/交付、控制、输出、诊断执行；[runtime_loop_diagnostics](../../native/runtime_app/runtime_loop_diagnostics.cpp) 仍是同一 Loop 的私有实现。合并新视觉标志、派生时间/发布时间可用性，移除重复 Fusion enabled 状态；一次 committed observation 同时给 viewport 与 telemetry 使用。视觉 epoch/policy/source age 边界保留。
4. [TelemetryTickInput](../../native/runtime_app/telemetry_collectors.h) 组合现有 ControllerSamplePayload，消除 93 项标量的第二套定义和逐项转抄；字符串仍延后到采样时复制。AcquisitionTraceInput 直接使用既有 payload，删除重复定义/映射。schema=19 与已有 JSON 名称、单位、采样节奏保持。诊断边界只负责从控制观测和实际 delivery receipt 转成记录，Collectors 拥有事件采样/目标事件状态，writer 拥有序列化和文件。
5. VisionResult 的显示坐标/检测框转换集中于 [Fusion publisher](../../native/runtime_app/fusion_channel_publisher.cpp)，Loop 仅发布结果，publisher 自己维护通道可用性。展示不取得控制权。
6. [配置 parser](../../native/controller_native/runtime_config.cpp) 修复显式无效值被接受：原程序对无效 bool、带尾缀数字和 NaN 均返回成功。回归先失败，改为完整 token、有限数及 unsigned 非负检查，并删除 117 个 fallback 参数。缺省默认值及已支持 bool 别名保留。任意 loader 回调的热更边界仍需独立检查，不因默认 loader 严格就删除。
7. 输出禁用的资源归属缺陷另行修复：原程序即使 `output.enabled=false` 仍创建 ViGEm。启动回归先失败；Loop 现在按启动配置用 unique_ptr 创建输出资源，关闭输出时根本不创建。此配置热更仍要求重启，指针有效性由资源所有者保证，无新增补丁开关。

**保留理由：** NativeGamepadController 的阶段与已有控制权状态机具有独立不变量；VisionService 的线程/mailbox 与 DeliveryGate 的身份/年龄检查保护不同边界；鼠标 session/supervisor 保证物理路径、按钮释放和故障恢复，不能合并成普通手柄输出。GUI/ConfigStore/RuntimeManager 分别拥有用户配置、文件冲突和进程身份；Python fallback、训练与声音研究包没有获准功能退役。本次不改控制算法、不删除这些功能、不扩大为桌面 UI 重写。有效范围/派生配置与 CLI 覆盖有不同输入边界，未证明冗余的设备检查及跨线程处理保留。

本次结构改动与两个有 RED→GREEN 证据的边界缺陷修复分别记录；SHA256 校验/benchmark 验收框架删除属于继承基线，不能计作本次结构简化收益。普通功能测试与数值仿真继续保留。

## 本轮重构怎样验收

用户要求先制定验收标准，再持续推进重构。以下标准由协调会话于 2026-10-04 制定；**执行会话已读取并据此完成源码/影响自查。** 标准属于本次结构重构的完成条件，不恢复已退役的 SHA256 溯源、benchmark 比较门禁或发布裁决框架。

**AI 友好型结构应使不了解本轮实施过程的 Agent，能够从仓库入口找到能力和源码，解释输入到实际结果的链路，判断状态归属、修改位置及受影响的使用方，并找到验证方法。** 文件变少、文件变短、目录变整齐或采用更多设计模式，都不能单独证明达到这一目标。

本轮以当前 C++ 原生运行路径为主，覆盖手柄和鼠标入口、共享感知与控制能力、输出和资源生命周期、配置，以及这些链路实际依赖的 GUI/IPC 边界。Python 替代运行、训练、桌面视觉重设计和算法效果优化不自动扩入范围。先明确主要能力中哪些结构问题必须处理、哪些部分已有合理边界无需修改；不能把第一批局部整理当作全部完成，也不要求为追求统一而重写每个模块。

| 判断方面 | 满足条件 | 怎样核对 |
| --- | --- | --- |
| 能找到实现 | 现行入口能引到主要 C++ 能力、生产入口、构建与测试方法；每项能力有源码依据和使用关系；当前路径、替代路径和历史内容能够区分 | 从 README 和本文抽查到源码与测试；修正断链和仍被当作当前要求的退役说明；不要求复制整个源码目录 |
| 能解释行为与状态 | 初始化、正常运行、变更、失败和退出形成可追踪的完整链路；关键状态和资源有明确的维护者、修改入口与生命周期；跨线程/进程交接、数据单位、时间及失效条件能查证 | 沿生产者、传递边界、消费者和实际交付检查。多个对象看似维护同一事实时，合并重复所有权，或证明它们具有不同语义 |
| 能定位修改影响 | 模块按实际职责组织，构建、include 和调用关系与文档中的依赖方向吻合；一次局部变化的修改点、使用方和必要检查可确定 | 抽查配置、视觉交付、输出/退出三类变化；漏掉必要使用方或无依据排除影响均不通过。本轮不新增循环依赖和无用途的转发层 |
| 复杂度确实降低 | 每批消除已证实的重复规则、重复状态、重复转换、混合职责或多余分支；没有把复杂度搬到更多接口、隐式行为或重复文档 | 给出前后代码依据与保留理由。统计单独计算本轮结构改动，不把之前删除旧验收框架或历史测试的行数计为本轮收益 |
| 行为保持且可验证 | 现行算法、完整控制链 lockstep、手动零死区与 15%–30% 权重、目标生命周期和 IPC 生效语义等有效约定保持；受影响生产目标能构建，相关功能和邻接路径得到核验 | 结构调整记录前后行为与测试；缺陷修复另需能够暴露原问题的回归用例。构建通过不代替运行结果，仿真通过不代替真实设备表现 |
| 知识能够接续 | 入口、能力关系、关键约定与当前代码一致；长期事实与当前任务进度分别维护；新会话能够分清已经完成、待处理与未验证内容 | 检查链接、源码依据及被替代说明的状态。旧 handoff 只说明历史，不能覆盖现行 AGENTS 与本文；上下文写入遵循既有授权 |

减少防护代码需要说明它保护的具体不变量以及负责维护不变量的位置。重复校验、无依据默认值、静默吞错和补偿错误状态归属的分支，是调查对象；外部输入、设备故障、资源释放与跨线程一致性仍需要真实边界处理。不能先删检查，再依靠测试恰好未失败认定它冗余。

设计模式用于解决实际变化点。资源所有权可用 RAII，设备差异可用 Adapter，确有替换需求的算法可用 Strategy，分散的合法转换可集中为状态机。每次采用应说明减少了什么耦合或分支；简单函数与组合足够时保持简单。不以模式数、类数、目录层数或统一文件行数设通过阈值。

### 用代表性工作检验结构是否可理解

协调方从现行入口自行查源码，执行方提供文档入口与实际检查方法，不以执行方摘要自证正确。首轮采用以下五项阅读和影响判断任务：

1. 找到默认原生程序从启动脚本到构建目标、程序入口的关系，说明运行依赖与有效配置来源。
2. 追踪一个现有配置项从 GUI 保存到原生实际生效，找出重启、拒绝、等待和已应用的依据，以及新增同类配置时需修改的位置。
3. 追踪一次 Vision 请求到结果被控制器消费，区分无新帧、无目标、过期或不匹配结果，并指出相关调用方与检查入口。
4. 追踪目标释放或程序退出到实际输出中立与资源释放，说明状态由谁改变、设备由谁释放，以及故障路径的必要检查。
5. 定位新增一个诊断字段从生产到记录的修改位置，判断是否需要跨越控制、运行编排、序列化和读取边界，区分真实依赖与无关模块。

每项先保留依据入口得到的判断，再沿源码核对完整性。漏掉已确认的关键使用关系、错误归属状态、误认已生效或找不到必要验证，均作为待修正项。记录实际遇到的误导、遗漏、无关阅读与纠偏；缺少可比基线或隔离的新会话时，不宣称冷启动效率提高了某个比例。这是本次结构检查，不要求建立新评测系统。

### 收口条件与当前状态

每批在现有任务记录中说明问题、修改、源码依据、验证和下一动作，并将相关条件标为“满足”“不满足”或“尚未验证”。执行者自查、协调方源码复核、实际运行和用户接受分别说明。第一批先由执行者报告，再由协调方复现下述构建与测试；两次检查不能当作两套不同场景，也不能据此扩大覆盖范围。

只有主要能力均已检查，本轮确认必须处理的结构问题已经解决或经用户明确调整范围，五项代表性任务没有未解决的关键遗漏，必要构建与行为核验通过，知识与代码一致，才可判断本轮约定范围完成。保留合理原结构的部分需要有依据，不能因未改动而自动视为遗漏。未触及的历史现场验证缺口不无限扩大本轮范围；改动触及而未验证的关键设备、GPU 或时序行为，仍须标为尚未验证，不能由离线结果代替。

当前约定范围已完成执行者自查与本地构建/功能/必要集成验证，结果见末节。下述协调方记录是第一批的历史复核；协调方对新 worktree 的独立复核、真实设备与用户体验确认并未发生。完成判断限于本次结构与边界修复，不能扩大为现场验收。

## 第一批历史进度与验证

范围已确认：C++ 主线的整体理解与逐链路重构；避开工作区正在修改的桌面 UI、曲线和训练实现；没有向方法仓库写目标项目资料。

第一批完成了模型、配置热更新规则与控制通道资源、双入口视觉适配、统一视觉结果提交。重构前 Base/Feature 10 组通过。Release 构建通过：cod_native_runtime、cod_native_mouse_runtime、cod_native_base_tests、cod_native_functional_tests。CTest Base/Feature/Mouse/Fusion 35/35 组通过（含两项新增事务/退出功能检查）；Python desktop_app/startup/repository_layout 72/72 通过。两个 runtime 的非设备配置检查返回 0；最终头文件整理后 BaseContracts 再测通过。文档源码链接已检查，git diff --check 通过。原生设备运行、实时停止、GPU 策略切换和游戏手感尚未验证。整体重构仍有上述后续提案，不能把第一批完成写成全部完成。

### 协调方独立复核：2026-10-04（第一批，以下为历史状态）

协调方重新构建当前工作区的 `cod_native_runtime`、`cod_native_mouse_runtime`、`cod_native_base_tests`、`cod_native_functional_tests`，Release 均通过。随后执行 `ctest --test-dir native/build -C Release -R '^(Base|Feature|Mouse|Fusion)' --output-on-failure`，35/35 组通过；`python -B -m pytest -q python/tests/test_desktop_app.py python/tests/test_startup_scripts.py python/tests/test_repository_layout.py`，72/72 通过。未启动生产设备或游戏。README、native/README、docs/README 和本文的 Markdown 本地文件链接未发现失效项；这不证明正文中的所有历史说法均有效。

对照改动前源码，热更新允许项、无变化时重置学习的约定和严格输入检查仍保留；[配置规则](../../native/runtime_app/runtime_reload_policy.cpp) 将重复的 key union 与查找改成一次有序差异遍历。[Bridge](../../native/runtime_app/runtime_control_bridge.h) 仍先唤醒并 join worker，再由 RAII 释放 view 和 handles；新增用例实际覆盖 pending 事务退出后的命名资源释放。共享 [Engine Adapter](../../native/runtime_app/vision_engine_service_poller.h) 消除了双入口重复实现；[结果提交](../../native/runtime_app/runtime_loop.cpp) 仍在各路径原有 epoch、policy 和 capture 检查之后调用，直接 poll 路径仍将 mailbox 发布时间标为不可用。此范围未发现首批结构调整引入的行为差异。

| 代表性任务 | 源码核查结果与剩余范围 |
| --- | --- |
| 默认启动与构建 | 启动脚本默认 native；main 加载配置并施加 CLI 覆盖，构建清单能够定位两个产品目标。已核查且产品构建通过；未执行真实设备启动 |
| 配置保存到生效 | GUI 保存/校验 → ControlChannel → Bridge 不可变候选 → tick 应用 → complete → GUI 状态可追踪。现有 IPC 测试直接调用 complete，不能证明 RuntimeLoop 在真实 GPU policy 边界的完整行为 |
| Vision 请求到消费 | Service 与 DeliveryGate 的职责不同；共享提交函数保留在检查之后。Service 的 no-update、epoch 和 in-flight policy 用例通过；真实 Engine 和生产 Loop 的联合执行尚未核验 |
| 目标释放或退出 | 程序退出中 reset 控制器、发送 neutral、停止 Vision、排空日志的顺序可查；pending Bridge 退出已测试。目标释放的全部控制器内部路径与真实输出接收端尚未独立复核 |
| 诊断字段到记录 | 字段经过 Loop 装配、TelemetryTickInput、Collectors、ControllerSamplePayload 和 serializer。可定位，但一次字段修改存在多处平行映射；不能据“有入口”认定职责已足够集中 |

**后续优先项：** 先调查 [Loop 的诊断装配](../../native/runtime_app/runtime_loop.cpp) 与 [Collectors](../../native/runtime_app/telemetry_collectors.cpp) 之间哪些转换可以只维护一次。重构应让调度代码只提供当前观察和实际交付结果，由诊断边界完成必要映射；避免把整段代码搬到新文件后保留同样的多处字段清单，或新增重复状态。补充能发现漏映射、错误单位和实际交付状态丢失的检查，再决定具体拆分。配置 parser 中 `parse_bool_value`、`parse_int_value`、`parse_float_value` 的 fallback 与 hot reload 严格校验具有不同现行行为，后续需先明确兼容约定与失败证据，不能将边界检查直接删掉。

首批结构简化有源码依据，构建与现有功能测试已独立复现；整体结构覆盖、生产编排的关键集成行为和接续入口仍有未完成项。协调方未声称完成隔离的新会话对照，也未测得阅读成本下降比例。跨会话通信接口当前不可用，以上标准及下一批建议已存入本文，执行会话是否读取仍未确认。

长期有效信息留在本文；.agent-context/handoff.md 仍是历史上下文。建议确认后简短更新 handoff：当前 C++ 主线、已退役验证框架、本文入口、第一批结果和下一条链路，不恢复旧 manifest 或比较门禁。


## 隔离 worktree 完成记录：2026-10-04

**范围与身份。** 工作路径为 `yolo-study-001-refactor`，分支 `codex/cognition-refactor-20261004`，继承基线 `1251273` 保存原工作区现行 tracked 修改及未跟踪源码/文档；未复制 output 媒体，不修改原工作区，也不将项目资料写回 project-cognition。本次 diff 单独可审查，不能把继承 UI/曲线等工作归作本轮新增实现。

**五项代表性任务自查。**

| 任务 | 当前源码依据与实证 | 状态 |
| --- | --- | --- |
| 默认入口/构建 | gamepad_start → main → runtime_targets；mouse_start → mouse main/session；当前 native 默认及显式 Python fallback 的脚本用例通过，fresh CMake Release 全目标构建通过 | 满足 |
| 保存到实际生效 | ConfigStore → ControlChannel → Bridge accept_reload_request → Loop apply_pending_config → Bridge complete/acknowledge_commit；真实进程从 revision 0→1，纯控制项保留学习，视觉策略变化清空学习且在 fresh policy 边界提交 | 满足；未重测完整 GUI 人工操作 |
| Vision 请求/消费 | Engine → Adapter/Service → epoch/policy → DeliveryGate → submit_vision_result → controller；真实 TensorRT/CUDA/桌面捕获执行并完成 policy 热更，落盘有 committed capture，既有无新帧/无目标/in-flight policy 测试通过 | 满足；现场目标/设备输入未验证 |
| 释放/退出 | Controller reset → enabled 输出 neutral → Vision stop → collectors/writer/session close；真实命名停止事件使进程返回 0；禁用输出不创建 ViGEm 的回归由失败转为通过；鼠标释放与故障 tests 通过 | 满足；真实接收端 neutral 与设备插拔未验证 |
| 诊断字段/记录 | Loop diagnostics 生产现有 payload → Collectors 采样/事件 → runtime_telemetry serialize；常规标量只定义/映射一次，新增 schema 字段需生产者及 serializer/reader 适配。Acquisition 已无第二套字段清单。失败 delivery、符号/幅值和 ns 单位有落盘断言 | 满足 |

**验证。** `cmake --build native/build --config Release -j 6` 全目标通过。`ctest --test-dir native/build -C Release --output-on-failure` 为 50/50 组通过，包含功能契约和保留的数值仿真测试，不构成 benchmark 发布裁决。受影响的 desktop_app、startup_scripts、repository_layout、desktop_profiles_curves、native_vision_runner、native_vision_targeting_bridge、native_vision_image_ops_bridge 共 134/134 Python 用例通过，未运行全量 978。一个过时 selector 桥接用例改为验证当前“扁平人物仍可瞄准且保持身份”契约，生产 selector 未改；native marker-loss 用例是该契约的依据。

**集成实测。** 输出关闭、无物理手柄，真实引擎 `640x512 → 480x384`：一次控制项热更确认 applied/保留学习，一次 friendly_filter policy 热更确认 applied/清空学习，均以命名停止事件正常退出。第二次开启临时 telemetry 得到 session metadata 1、delivered control 340、acquisition trace 52、controller sample 136、target event 2、committed capture 1，临时输出随检查结束清理；另一次实际 Fusion publisher→共享通道检查读到 frame=3、640x512、protocol=2，并正常停止；未打开 Canvas。以上是记录与链路检查，不是目标成功率。`delivered` 记录在输出关闭时标识 output_disabled，不能视为真实设备交付。

**保留限制。** 未测真实手柄/鼠标接收端、Fusion 真实窗口排除捕获、实时游戏表现、训练效果、独立协调复核或冷启动阅读效率。无 matched live A/B，不能推断帧率、延迟或手感改善。本次没有新算法候选或新的验收平台。

**接续。** 本文是长期能力/关系入口；handoff 只保留当前约定、完成状态和未验证边界，session-log 保留任务里程碑。后续新增同类配置先查 parser/派生/CLI/reload policy 的各自输入责任；新增诊断标量从现有 payload 与生产者开始，避免重新建立平行 DTO 或校验框架。
