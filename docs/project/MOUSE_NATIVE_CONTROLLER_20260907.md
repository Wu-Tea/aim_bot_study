# 原生 Mouse Controller（2026-09-07）

> 2026-09-08 接入更新：正式 `mouse_start.bat` 已默认使用 `virtual-hid`，将物理包经 Interception 捕获，交给现有控制器，再由独立 FakerInput HID 提交唯一结果。设备层、session 与独立进程监督均已加入构建与回归。见 [Mouse 输入替代路线](MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md) 和[本次集成记录](../../runs/mouse_virtual_runtime_integration_20260908/README.md)。下文历史章节中的早期驱动不可用、Win32 性能结论保持原有范围。

用户报告的旧 Win32 双倍偏移来自原物理输入与注入结果同时生效。新 transport 在设备所有权层消费整个原包，移动、按钮和滚轮统一经独立虚拟端点发送。旧 Interception 后端本来也会捕获原 X/Y，但最终仍回送原设备；现在正式入口使用新独立虚拟输出。目标游戏接受和高负载性能仍需实测。

## 当前正式代码的控制路径

```text
所选实体鼠标 -> Interception 全包捕获与有序输入窗口
        -> MouseRateAdapter（counts / 实际 dt）
        -> 当前 NativeGamepadController（目标身份 / ADS / BodyLock / 手动接管 / final-T）
        -> MouseActuatorAdapter（积分、整数余数）
        -> 唯一 FakerInput 报告：最终 X/Y + 物理按钮 / AutoFire 合成状态 + 滚轮
        -> 独立虚拟 HID 鼠标

父进程持有 Ctrl+Alt+F12，监督工作进程完整 tick 的心跳；
工作进程单独监督父进程，异常退出后由有界恢复进程清理虚拟持有。
```

鼠标只使用共享控制器的算法，不创建虚拟手柄。右键对应 LT/ADS，左键对应手动开火语义。Vision 使用共享控制器分类后的 acquire/correct/handover 意图。

鼠标 controller 使用共享 AI 准星控制与 AutoFire，另由鼠标计数层提供固定下压。捕获的左右键状态作为 ADS/手动开火输入；五键和普通 120 单位滚轮由同一虚拟鼠标转写。侧键与水平滚轮已有模拟转换，尚无本次真人验收。无辅助和压枪时最终位移等于原量，存在辅助时提交共享控制器最终 X/Y 加本 tick 获准的压枪计数。

AutoFire 直接消费当前 ControlFrame 的 synthetic fire command，沿用共享控制器的开火授权、准星就绪、脉冲节奏、目标丢失和手动接管规则。每个输入窗口一次合成最终按钮状态，左键为物理持有 OR 当 tick 有效自动授权；真实左键按下后转为用户持有，自动开火停止不得抬起实体按键。虚拟报告不进入物理左键状态。校准、输出失败、消费超时、正常退出和 F12 都释放本程序仍持有的自动按键。

鼠标实例关闭手柄 recoil、标记按键与手柄在线响应学习，使用线性响应坐标；手柄默认行为不变。超出输入适配范围的快速甩动转发原 counts，清空控制器与量化余数，避免限幅吞掉手动输入。

## 启动

完整当前默认配置、日志入口及阅读顺序见 [Mouse Overview](MOUSE_OVERVIEW.md)。下文响应配置示例仅展示响应/手动参数子集；新增的范围 180 px、加减速 40/25 ms、目标点容差 3 px、固定下压和逐 tick 日志均已接入正式 runtime。

启动时从 `config.toml` 的 `[mouse]` 读取响应参数；未配置时保留 1200 DPI、COD sensitivity 5、FOV 104（16:9 水平定义）、ADS multiplier 1.0。

```toml
[mouse]
dpi = 1200.0
sensitivity = 5.0
fov = 104.0
ads_multiplier = 1.0
# AI 控制参数也在同一节；sensitivity 是游戏灵敏度，speed 是 AI 速度倍率。
speed = 2.0
breakaway = 4.0
bodylock_deadzone = 0.5
```

这些值应与鼠标和游戏的实际设置一致；程序不会修改鼠标硬件 DPI 或游戏设置。DPI、灵敏度、ADS 倍率必须为有限正数，FOV 必须大于 0 且小于 180 度。命令行选项逐项覆盖配置，配置缺项才使用内置默认值。配置改动在下次启动生效。配置接线与验证见[本轮记录](../../runs/mouse_response_config_20260908/README.md)。
日志以 `cod_default_estimate` 标识估算参数；校准是可选精调，成功后对应模式改为 `calibrated`。

转换采用 [Sens Converter 的 COD yaw 常数](https://sensconverter.app/cod-sensitivity-converter/)：
`0.0066°/count × 5 = 0.033°/count`，`1200 × 5 = 6000 eDPI`，约 `23.0909 cm/360°`。
投影采用屏幕中心近似：`focal_px = full_view_height × (16/9) / (2 × tan(104°/2))`，
`px_per_count = focal_px × 0.033 × π/180`。例如 1920×1080 时为 `0.43198869 px/count`，
对应共享控制器 500 px/(u·s) 的 `1157.4377 counts/(u·s)`。
程序在交付 Vision 目标前使用 DXGI capture_output_height 更新比例，不能使用裁剪 ROI 或网络 tensor 尺寸。
Raw Input/SendInput 已经是 counts，因此不能再乘一遍 DPI；DPI 用于物理距离与 cm/360 换算。
ADS 默认先采用同一中心屏幕响应乘 1.0，这是估算而非独立瞄具测量；具体瞄具、ADS FOV 和 MDC 差异可通过 F11 校准覆盖。

```powershell
scripts/launch/mouse_start.bat
```

默认使用 `virtual-hid`，支持透传命令行参数。`mouse_start.bat` 与通常的 `debug/mouse_native_debug.bat` 均进入当前 C++ mouse controller。默认按已实测 Razer 接口的完整硬件 ID 选择，编号不写死；其他鼠标可用 `--mouse-hardware "完整硬件 ID"` 或 `--mouse-device 11..20` 明确选择。重复身份、虚拟源、输出端不可用时停止，不自动退回软件注入。`--check-transport` 只枚举并核验源 / 虚拟设备，不捕获、不发送报告。`interception`、`win32-debug` 和 `kmdf-vhf` 保留为显式旧后端；本程序不安装驱动。

1. 启动前松开所有鼠标按钮。加载 config.toml 与 Vision 模型期间不接管，随后核验按钮释放并启用捕获。
2. 默认参数立即运行共享 ADS/BodyLock。需要精调时，对准 Vision 能识别的静止假人，松开右键，按 Ctrl+Alt+F11 校准 Hipfire。
3. 如需精调 ADS，保持按住右键，对准静止假人，再按 Ctrl+Alt+F11。校准模式读取本 tick 捕获的物理右键状态。
4. 每个模式优先使用本次运行中的实测校准，没有实测时使用配置计算的 COD 响应估算。重启重新读取配置，仍然可以直接运行。
5. Ctrl+Alt+F12 请求解除接管；独立监督进程负责超时终止与恢复，阻塞的 Vision/controller 不负责接收紧急键。

无 Vision 的同 session 三模式入口：`scripts/launch/mouse_start.bat --relay-test passthrough --duration-seconds 5`。将模式改为 `block` 或 `invert` 分别测试零移动与反向；两种模式会改变选定鼠标的移动。

校准发出 +40 水平 counts，使用同一目标的视觉位移测量比例，然后发出 -40 检查回位。X/Y 暂共用水平测量比例，Y 轴比例相同是设计假设。探测期间改变右键、换目标、明显移动鼠标、输出失败或超时都会拒绝该次校准。

可用 `--mouse-dpi`、`--mouse-sensitivity`、`--mouse-fov`、`--mouse-ads-multiplier` 覆盖配置中的四项设置，所有入口透传这些参数。`--check-config` 会显示最终值、default/user/cli 来源、cm/360 和 Hipfire/ADS 的响应估算，不启用设备；其响应估算使用捕获开始前的初始 1080 高度，正式运行仍以 DXGI 输出高度更新。校准期间暂时只输出探测量与手动输入，防止辅助污染测量；校准结束后使用已有实测或配置估算参数。

## 历史：原生位移叠加调查与首次 Interception 接线（2026-09-08）

- 已确认：用户实测旧链路发生叠加。旧代码在 WH_MOUSE_LL 对 WM_MOUSEMOVE 返回 1，Raw Input 仅以 RIDEV_INPUTSINK 订阅；没有设备过滤所有权。SendInput 成功仅表示插入系统输入流。旧 1000 Hz / 接收 hook 计数不是游戏端独占证明。
- 三角洲：用户反馈仅原生鼠标生效。目前没有同步控制器输出日志或游戏接收证据，无法区分未产生 AI 输出、权限限制及游戏拒绝软件输入；不把反作弊过滤推断写成已证实原因。
- 修正层：MouseInterceptionTransport 通过已有设备包接口接管所选鼠标，截住原始移动；即使包里同时含按键，也只转发零位移的按键部分。controller 的 final counts 独立下发一次。AutoFire 通过同一接口，沿用实体按钮优先规则。
- 调度：controller 仍是 1 ms 目标周期，AI 按实际 dt；接收线程每次排空当前设备包，未人为设为 250 Hz。新驱动后端尚无本机 1000 Hz 实测，旧 Win32 数据不能替代它。
- 生命周期：同一锁序列化消费、最终输出与释放；消费者超过 100 ms 未续期则释放过滤、自动按键和驱动上下文。F12 继续使用独立键盘线程。完整进程挂起或驱动调用卡住不能由该进程内看门狗保证恢复，尚未实测。
- 验证：模拟驱动的独立下游 oracle 检查零输出无位移、反向只收到反向量、原样 counts、混合按键/滚轮、输出不回灌、实体接管 AutoFire、退出恢复和超时释放。负对照故意让原始包穿透，应以“physical movement escaped before replacement”失败。它验证回归判据，不伪称旧二进制实测或驱动实测。
- 本机状态：--check-transport 返回 2（Interception DLL/driver unavailable）；没有安装系统驱动。当前默认启动会明确停止。安装是系统输入栈变更，需单独安排后才可能进行实体设备测试；本轮未修改 Windows 签名、保护设置或游戏文件。

验收必须先证明所选鼠标在 block 模式有非零源输入而接收端位移为零，再证明 invert 只产生反向位移，最后才运行 ADS/BodyLock 和调速。目标三角洲还需单独验证它是否接受输出，设备拦截方案不承诺所有游戏兼容。

资料：[Microsoft LowLevelMouseProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)、[RAWINPUTDEVICE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice)、[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[Interception 上游说明](https://github.com/oblitum/Interception)。

## Mouse 速度、手动脱离与 BodyLock 抖动死区（2026-09-08）

用户要求默认 AI 速度提高到 2 倍，并通过类似手柄死区的方式，减少 BodyLock 期间手抖对瞄点与 AI 索敌的干扰。正式参数现在从 `config.toml` 的 `[mouse]` 读取，命令行可覆盖；校准仍可选，控制周期仍为 1 ms。

```toml
[mouse]
speed = 2.0
breakaway = 4.0
bodylock_deadzone = 0.5
```

`bodylock_deadzone` 是各轴物理速度归一化后的死区，范围 0..0.9，设为 0 关闭。默认响应、breakaway=4 时，0.5 约为 2.3 counts/ms，因而短暂往返的正负 1～2 counts/ms 被视为抖动。2026-09-08 后续修正增加有界空间容差：宽度为此速度阈值乘 4 ms，默认约 9.3 counts；持续同向慢拖超过该范围便恢复手动修正，不依赖识别帧率，也不补发被挡掉的位移。实际阈值随响应校准与 breakaway 换算，不是每个输入包固定丢弃 2 counts。改完配置后重新启动生效。实现、RED/GREEN 与回归记录见[本轮记录](../../runs/mouse_bodylock_deadzone_20260908/README.md)。

```powershell
# 默认：更快的 AI、较强的 BodyLock 保持
scripts/launch/mouse_start.bat

# 进一步加速，保留配置里的手动脱离设置
scripts/launch/mouse_start.bat --mouse-speed 2.5

# 增强保持，需要更明显的甩动才能直接脱离
scripts/launch/mouse_start.bat --mouse-speed 2 --mouse-breakaway 6

# 恢复原有速度和手动范围；仍使用直接裁决与转写
scripts/launch/mouse_start.bat --mouse-speed 1 --mouse-breakaway 1 --mouse-bodylock-deadzone 0
```

| 参数 | 默认 / 范围 | 行为 |
| --- | --- | --- |
| mouse.speed / --mouse-speed | 2.0 / 0.5–3 | 调整 ADS 到达时间和 ADS/BodyLock 的 AI 速度预算；不会整体乘大手动物理位移。倍率是控制参数，不保证任意游戏轨迹严格按同比例变化。 |
| mouse.breakaway / --mouse-breakaway | 4 / 1–8，且不小于 speed | 原有手动速度阈值的倍数；任一轴超过阈值即原量透传。也影响归一化死区对应的物理速度。 |
| mouse.bodylock_deadzone / --mouse-bodylock-deadzone | 0.5 / 0–0.9 | BodyLock 时抑制小幅往返抖动；累计同向移动超过空间容差后恢复手动修正，避免无限吞掉慢拖。0 关闭。 |

参数由 `[mouse]` 读取；同名命令行选项优先，可用 `scripts/launch/mouse_start.bat --check-config` 查看生效值及来源，此检查不启动设备或 Vision。配置样例也已加入 `config.native.example.toml`。

BodyLock 死区在两个职责层保持一致：IntentFilter 在手势起始、瞄点 D 编辑与 Vision 意图发布之前排除微小轴输入；唯一 final-T 裁决也将这些轴的手动量视为零，保留现有 AI 目标量。否则上游会把抖动判为合法 D 编辑，或下游又把原 counts 带回来。各轴独立，明确的水平微调不替垂直抖动取得权限。

死区只用于 BodyLock。ADS 获取、校准、无目标、目标失效、松开瞄准与明确交接维持现有规则；超过物理速率适配范围的甩动最先原量透传。高于死区的有效手动 D 编辑仍保留原量。输出仍是同一控制链的唯一 T，不发送抵消包。

实现采用一致的单位换算：breakaway 扩大一个内部 u 对应的 counts/s，同时将共享控制器的线性响应估计和 AI force budget 换算到相同单位。speed 缩短 ADS 控制到达时间，并提高两种模式的 AI 速度预算；没有在最终输出上叠加第二路 AI，也没有把手动 counts 乘以 speed。BodyLock 的手动权重下限为 1/breakaway，并由当 tick 的 visual_authority 插值；证据弱时压制较轻。gamepad 的新增适配字段默认保持原值，TOML 不开放这些适配内部字段。

视频为 3.033 秒、60 FPS 的手机录屏，可见两次开镜/靠近目标/开火。没有与视频同步的 runtime 身份、counts 和状态日志，所以无法精确归因或量出游戏内速度。本机代码回归证实旧归一化上界会让 2 counts/ms 脱离，不将其宣称为视频每一次动作的原因。当前设备路线已经完成正式 runtime 的唯一虚拟输出接线，实物与游戏验收仍需推进；本节倍率不用于补偿原物理输入叠加。

以下速度比例是早期 speed=1.5、breakaway=4、未启用本次死区时的历史记录，不代表当前 speed=2 的实测比例。首次 fixture 只有弱证据（visual_authority=0.3325）且 35 ms 水平输出只有 3 counts，不足以验证强锁定和细分倍率。保留其 RED 与失败记录；修正 fixture 为显式敌方 cue/身份（authority>0.8）和 100 ms 观测窗后，使用同一生产控制器的 1/1 原设置作反事实对照。对照得到速度比 1、保持轴数 0；新设置得到 ADS 比例 1.57944/1.57944、BodyLock 1.5/1.53333、保持轴数 2；20 counts/ms 脱离、无目标及松开瞄准均保持原始 counts。此为合成命令层回归，不是视频重放或真实游戏 A/B 验收。

证据文件位于 runs/mouse_tuning_20260907；原始视频和截图不进入普通源码提交。默认与 --mouse-speed 2 的游戏内比较须在新设备后端接线、物理排除和正式 controller 门禁通过后进行；这不是当前下一项设备工作。

直接裁决版本通过 19/19 Native suites 和 20/20 启动脚本测试；新增同向/反向/零手动输入输出一致、空闲轴直接降权、同 tick 释放，以及 AutoFire 物理速度门槛单位不变的断言。合法参数的透明注册/释放与四组非法参数拒绝均已实跑。原生 mouse 可执行文件已重新生成。

## 同一程序的链路测试

不加载模型、不拦截和不移动鼠标即可枚举驱动设备（Win32 历史注册检查须显式加 --transport win32-debug）：

```powershell
native/vision_native/build/Release/cod_native_mouse_runtime.exe --check-transport
```

下列模式均使用正式 session 的输入、输出、超时释放与紧急热键，默认运行 10 秒后自动恢复；可用 --duration-seconds 1..60 指定时长。测试期间移动实体鼠标，按钮和滚轮仍然可用。F11 校准在链路测试中禁用。

```powershell
scripts/launch/mouse_start.bat --relay-test passthrough
scripts/launch/mouse_start.bat --relay-test block
scripts/launch/mouse_start.bat --relay-test invert
```

| 模式 | 输出 | 接收端应表现为 |
| --- | --- | --- |
| passthrough | 原始相对 X/Y | 跟随实体鼠标移动 |
| block | X/Y 始终为零 | 实体鼠标移动时接收端保持不动 |
| invert | X/Y 取反 | 接收端只产生反向移动 |

每秒显示 source_packets、blocked_moves、replacement_reports、passed_replacements、input_sum/output_sum、controller_ticks、transparent_ticks、RMB 和控制模式。它们分别定位源输入、hook 屏蔽、最终输出、输出放行和控制器状态；这些本地计数不能单独证明目标游戏收到什么。

正式控制模式也显示上述计数，并记录 Vision 首帧/目标、校准成功/失败与首次 AI 输出。

## 生命周期和校准回归

本次修复均在职责所属层完成：

- Transport：释放后仍可提交最终位移的 OS 回归先失败；现在拒绝释放后的最终/校准输出，hook 同时拒绝排队的本程序迟到位移。
- Transport：消费线程每 tick 续期；停顿超过 100 ms 后，hook 线程的 20 ms 定时检查释放实体移动并报告失败。该保护覆盖消费线程停顿，完整进程挂起的释放时延没有实测。
- Calibrator：探测输出前已经拍摄、之后才交付的画面不能测量该输出。按实际输出时间建立边界，在原有 750 ms 总时限内等待可测响应和回位；不会因为正常渲染延迟而立即拒绝。
- RuntimeCore：校准回位 tick 不得吞掉允许范围内的实体位移。现在将该 tick 的输入与回位量合并成一个最终报告。
- Facade/RuntimeCore：无有效参数时不得输出 AI；按用户后续要求，明确允许 COD 估算参数，无须伪造 calibrated 标志。目标丢失撤销旧校准点；RMB 改变取消跨模式校准；切换配置后输入和输出共用控制器的首 tick 时间。

## 验证与边界

### 历史 Win32 后端的 1000 Hz 调度与时间积分证据

正式入口与 relay-test 均采用独立的 1 ms 绝对截止时间调度，不再继承 gamepad 的 controller_tick_hz / scheduler.mode。保留已在本机测量的 deadline/yield 等待方式、AboveNormal 控制线程和 1 ms timer period。短周期内主要 yield 等待，会占用 CPU；这不是 Windows 硬实时保证。超期跳到下一截止时间，不突发补发过时报告。

同一 tick 内仍完整执行共享 controller 的 intent、ADS/BodyLock、authority、AutoFire 和最终输出。Vision 异步提供新观测，不能把 Vision 帧率当作鼠标输出频率。AI 使用实际 dt，将速度积分成 counts，保留小数余量；首 tick 与共享 controller 对齐为 1 ms，正常 dt 的既有范围为 0.1–50 ms。没有引入异步 AI proposal 或按帧固定挪动。

Win32 最终位移设置 MOUSEEVENTF_MOVE_NOCOALESCE，按微软 [MOUSEINPUT 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput) 请求不合并 WM_MOUSEMOVE。这只描述此输出标志的语义，不证明任意游戏的 Raw Input 接收频率或兼容性。非前台测试窗口的队列探测未能建立逐包接收证明，不作为通过项。

每秒日志区分：

- tick_hz / avg_tick_hz：本窗口及启动后的实际循环频率，排除模型启动时间。
- tick_p99_ms / tick_max_ms / skipped_deadlines：间隔抖动和累计跳过的调度周期。
- source_event_hz：实际收到的源事件数/秒，可能包含按键事件；不是 USB polling rate。
- nonzero_output_hz：实际提交的非零移动报告数/秒；静止、低速小数累积或屏蔽模式可能为零，不能据此认定循环降频。

新增 adapter 回归验证相同一秒速度在 1000 Hz、250 Hz 及不均匀 dt 下得到相同 X/Y counts，并验证手动物理 counts 在这些 dt 下转换后保持原值。显式桌面探测另发出 2000 个报告，由位于生产拦截 hook 后的独立接收 hook 全数接收并计时，频率门槛为 950–1050 Hz；接收 hook 截住该探测位移，避免连续移动桌面光标。单独运行该项：

```powershell
native/vision_native/build/Release/cod_native_mouse_win32_debug_transport_tests.exe --movement-only
```

本机初测：改动前带计时的正式入口约 1000.01 Hz，P99 约 1.05 ms；改动后一次独立输出探测为 2000/2000、992.075 Hz，正式入口平均 989.448 Hz，记录到 32 个超期周期。两次没有固定后台负载，不能拿来推断性能提升或下降；它们确认本机接近 1 kHz，而非证明游戏端或实体鼠标稳定 1 kHz。

完整回归中的独立接收端再次得到 2000/2000、1000.17 Hz、0 个超期；18/18 CTest、20/20 启动脚本测试及桌面 AutoFire/释放测试通过。另用 runs 下的配置副本将 controller_tick_hz 设为 250，正式 mouse 仍实测平均 995.041 Hz，证实不再跟随 gamepad 降频。用户原配置未修改。证据：runs/mouse_hz_verification_20260907.log、runs/mouse_hz_250_config_20260907.log。

```powershell
scripts/verify/mouse_native.ps1
D:/env/python/python.exe -m unittest tests.test_startup_scripts
```

离线验证包含 8 个 Mouse、6 个 Base 和 4 个 Feature suites。Mouse facade 对照当前 controller 的相同输入，明确触发 ADS 与 BodyLock，并逐帧比较 final X/Y、目标身份和控制模式；另有共享 AutoFire 开火/间歇、手动接管和目标丢失断言。Mouse_auto_fire_button 检查自动按键持有、实体接管和排队事件交错。Base/Feature 覆盖共享控制器的产品回归。

显式桌面探测可用 scripts/verify/mouse_native.ps1 -DesktopProbe；该探测短暂移动光标并恢复，检查最终输出可通过、不会回灌、释放后拒绝迟到输出、消费者停顿释放。AutoFire 探测使用独立 hook 接收并截住测试点击，不将点击送到桌面应用；检查 Down/Up 边沿、重复持有、注入不伪装实体按键，以及退出/超时抬键。它是 OS 注入及生命周期测试，不是实体鼠标或游戏输入验收。

完整游戏验收仍需在目标游戏内执行透传、屏蔽、反向三种模式，确认源 X/Y 不丢失、游戏只收到最终位移，再验证实际 Hipfire/ADS、BodyLock 与手动接管手感。WH_MOUSE_LL 接口的消息拦截范围见微软 [LowLevelMouseProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)。不能把桌面成功等同于所有 Raw Input 游戏兼容。

本轮没有安装驱动、修改系统签名设置或修改用户配置。

## 本机运行记录

AutoFire 接线与入口修正后：18/18 CTest、20/20 启动脚本测试通过，含独立接收端的 OS AutoFire/释放探测。旧 debug/mouse_native_debug.bat 已改到同一 C++ controller，实跑 1000 ticks 正常退出；无目标、无实体按键时 auto_fire_edges=0/0，不会自行点击。

默认参数改动后的验证：17/17 CTest、20/20 启动脚本测试通过；默认与实测 profile 分别逐帧对照共享 controller，均触发 ADS/BodyLock。未校准的正式入口运行 1000 ticks，识别 full_view=2560×1440，默认比例 0.575985 px/count；controller_ticks=987、transparent_ticks=13，实体输入与输出累计均为 (12,11)。链路测试模式仍显式关闭默认辅助，controller_ticks=0。

- 17/17 CTest suites、20/20 启动脚本测试通过。
- 正式入口加载当前 Vision，运行 1000 ticks 后正常释放退出。
- 5 秒实体透传记录：source_packets=88，blocked_moves=207，replacement_reports=63，passed_replacements=63，输入与输出累计均为 (-278,28)。这证明该次运行中收到了实体源数据并提交了等量最终位移。
- 一次桌面注入探测同时收到实体位移 (7,-7)，physical_blocked=13；该环境不满足静止探测条件，不能将这些源数据归因为输出回灌。探测现会单独报告 INCONCLUSIVE，而不会把受干扰样本当作通过。
- 独立桌面光标采样的零输出检查中 source_packets=0、光标累计位移=0；因为没有实体移动，该次只确认空闲运行与自动释放，不构成实体移动被排除的证明。
- 目标游戏的零输出排除、反向替代和实际辅助手感尚未验收。


## 2026-09-08：运行诊断、慢拖修正与持续下压

最新用法与限制见 [鼠标诊断与压枪](MOUSE_DIAGNOSTICS_RECOIL_20260908.md)。正式程序已接入逐 tick JSONL、异步有界写入、输入/最终 HID 窗口对账，以及可配置的固定下压。此前速度死区会无限吞掉慢速同向拖动；当前版本保留往返抖动抑制，累计位移超出空间容差后恢复修正。该修正有离线 RED/GREEN；未记录的游戏抖动成因仍待新日志确认。

后续新增 `[mouse] bodylock_range_px=180`、`bodylock_accel_ms=40`、`bodylock_decel_ms=25`，详见 [BodyLock 范围与加减速](MOUSE_BODYLOCK_RANGE_CURVE_20260908.md)。它们独立于速度/死区命令行选项；三项均设为 0 才恢复本轮前的范围/整形设置。当前采用连续 BodyLock，未将 ADS Snap 改成常态循环。
