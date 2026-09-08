# Mouse 路线：接管物理输入，提交唯一虚拟鼠标结果

更新：2026-09-08。**正式 C++ AI runtime 已接入 Interception 全包捕获 → 现有 controller → FakerInput 独立虚拟 HID；`mouse_start.bat` 默认使用 `virtual-hid`。** 本文同时区分代码实现、离线回归、独立工具桌面证据和仍需真人验证的范围。

研究来源：Codex 任务 `01a07ec2-6ae6-7012-9eb0-98e35a1b2602`「实现虚拟鼠标信号转写」，及其[最终桌面验收记录](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)。任务读取接口的最后两轮没有返回内容，因此最终状态还按工作区报告、原始 CSV 和当前源码交叉核验。

## 1. 产品目标与旧问题

Mouse 面向使用实体鼠标、键盘的用户。指定鼠标的移动、左右键和上下滚轮由应用接管；无辅助时保持手动输入，有辅助时复用现有 Vision、目标身份、ADS、BodyLock、手动接管和 final-T 裁决。Windows / 目标程序从独立虚拟 HID 鼠标收到最终结果。程序不创建虚拟手柄。

```text
指定物理鼠标
    ↓ Interception 捕获原包；接管期间原包不继续送达普通鼠标消费者
唯一设备工作进程
    ├─ 物理 counts / 按键边沿 → MouseRateAdapter
    │       → 现有 NativeGamepadController 的完整控制链
    │       → MouseActuatorAdapter → 最终 X/Y counts T
    └─ 物理按钮、滚轮 + 既有 AutoFire 授权 → 最终按钮状态与滚轮
    ↓ 唯一报告提交者
FakerInput 独立虚拟相对鼠标 → Windows / 目标程序

独立监督进程：紧急键、工作进程存活与恢复
独立 Raw Input 接收端：用于实测验收，另行采集
```

控制器能在数学上覆盖手动量，前提是**原物理输入没有绕过这个裁决继续生效**。设一个输入窗口的手动位移为 `M`、控制器最终位移为 `T`：

- 旧叠加路径的下游可能收到 `M + T`。例如 `M=10, T=10` 得到 20；`M=10, T=-3` 得到 7，连方向都错。“双倍偏移”是其中一种表现，不能假定所有场景都是严格两倍。
- 正确接管路径只收到 `T`。透明控制时 `T=M`，零移动测试时 `T=0`，反向测试时 `T=-M`；三个条件都必须在非零源输入下成立。
- 不能用减半灵敏度、发送 `-M` 抵消包、`T-M` 软件修正、提高 BodyLock 压制或增加延迟掩盖设备所有权缺失。输入隔离和最终提交属于 transport；目标裁决属于现有 controller。

已建立的失败证据是用户对旧 Win32 路径的叠加反馈，以及代码中 Raw Input 订阅、`WH_MOUSE_LL` 消息拦截和 `SendInput` 的组合，没有设备级排除证明。目标游戏仅原生输入有效的具体原因仍缺同步证据，不能写成已证实的游戏过滤原因。

要区分当前另一个后端：`MouseInterceptionTransport` **确实捕获并清零原 X/Y**，并用 `interception_send` 向原设备通道提交最终移动。不能把它描述为原理上不会拦截；它与本次已验收的独立 FakerInput 输出方案不同，尚未建立相同的正式 runtime 端到端证据。

## 2. 本轮研究已经推进到哪里

下表区分 2026-09-08 独立工具的留存实验和本轮正式接入的完成范围；正式 runtime 不自动继承独立工具的真人验收结论。

| 能力 | 已有证据 | 当前判定 |
| --- | --- | --- |
| Interception 安装、重启后枚举；FakerInput 可用 | [重启后记录](MOUSE_VIRTUAL_RELAY_POSTREBOOT_20260908.md) | 已完成，不再以“尚未安装/待重启”为下一步 |
| 新 C++ FakerOutput 独立虚拟输出 | 两个接收进程各 64/64 个微位移，各自顺序与值正确 | Windows 输出已验收 |
| 实体原样 / 零移动 / 反向 | 4,793 / 1,096 / 4,988 个源包；绝对源量 25,365 / 3,537 / 17,769，输出 25,365 / 0 / 17,769 | 三模式通过，有实际源移动 |
| 左右键、上下滚轮 | 三模式及补测的各类接收序列通过；补测 5,610 包、6 上 / 11 下滚轮 | 用户要求的桌面功能范围通过 |
| 原设备排除 | 两次合计 16,487 个源包，接管区间独立接收端原设备有效输入泄漏均为 0 | 已证明这两次桌面记录的隔离 |
| 正常定时退出后恢复 | 释放时间戳后分别收到 119 / 148 个原设备有效事件，研究记录含用户恢复确认 | 已验收这两次正常退出 |
| 真人 F12、拔插、真实崩溃/挂起恢复 | 有热键与模拟生命周期测试，真人采集均由定时结束 | 尚未实测这些故障/退出触发 |
| 完整混合事件交错顺序 | 现有分析器分别校验移动、按钮、滚轮序列 | 尚缺跨事件种类的总体时序 oracle |
| 正式 AI runtime 接入 | `MouseVirtualHidTransport` + `MouseControllerSession` + 独立进程监督，正式入口默认已切换 | 已落地；验证记录见[本次集成记录](../../runs/mouse_virtual_runtime_integration_20260908/README.md) |
| 目标游戏、1000 Hz 物理输入与完整延迟 | 尚无正式 AI 组合的对应实测 | 待真人验证 |

原始证据：

- [三模式分析](../../runs/mouse_virtual_relay/physical-acceptance-query-fixed-20260908/analysis.json)、[会话](../../runs/mouse_virtual_relay/physical-acceptance-query-fixed-20260908/session.json)、[事件](../../runs/mouse_virtual_relay/physical-acceptance-query-fixed-20260908/events.csv)。
- [按钮滚轮分析](../../runs/mouse_virtual_relay/buttons-wheel-query-fixed-20260908/analysis.json)、[会话](../../runs/mouse_virtual_relay/buttons-wheel-query-fixed-20260908/session.json)、[事件](../../runs/mouse_virtual_relay/buttons-wheel-query-fixed-20260908/events.csv)。
- [C++ 虚拟输出探针](../../runs/mouse_virtual_relay/native-output-postreboot-20260908/summary.json)。

侧键不属于用户这次真人验收要求；代码已有五键转换，中键在三模式记录中有活动，不扩大为侧键完整验收。水平滚轮和高分辨率滚轮也不在已完成范围内。

## 3. 保留的目标与需要替换的旧假设

继续遵循[最小 Mouse 适配决策](../../.agent-context/decisions/DEC-2026-08-19-001-minimal-mouse-controller-adaptation.md)的模块分工，以及[唯一 final-T 决策](../../.agent-context/decisions/DEC-2026-08-07-001-target-first-final-output.md)的输出所有权：

- 复用现有 controller，设备适配负责 counts、方向、实际 `dt` 和整数余数；不另写 Mouse PID、TargetPlan 或第二次 final-T 裁决。
- 手动量是意图证据，辅助拥有目标时可被降低、取消或覆盖。无目标、松开 ADS、有效手动脱离按既有规则回到手动控制；仍经虚拟设备提交该窗口的 `M`。
- 保留当前 AutoFire 授权及物理优先语义。手柄 recoil 在 mouse 实例中关闭；后续已加入独立的鼠标固定下压，默认 ADS 开火时 30 counts/秒，详见[日志与压枪](MOUSE_DIAGNOSTICS_RECOIL_20260908.md)。
- 保留当前内存校准与明确标注的 COD 估算参数。设备接线本身没有更改 speed/breakaway、校准策略或共享算法；后续用户明确要求的更新已将速度默认值设为 2，并加入 `[mouse].bodylock_deadzone=0.5`，详见[原生参数说明](MOUSE_NATIVE_CONTROLLER_20260907.md)。估算值不能冒充目标游戏实测。
- 键盘不加入应用拦截；紧急释放独立于 Vision/controller。Interception 安装包已经装了键鼠类过滤驱动，不等于应用应启用键盘过滤。

8 月 V2.3 历史方案位于 `codex/mouse-controller-foundation-20260815` 的 `docs/project/MOUSE_CONTROLLER_REUSE_FIRST_ARCHITECTURE_V2_3_20260814.md`。它本来就禁止 `physical M + independent AI correction`，要求唯一 final-T；此次落实缺失的设备连接与证明，不把旧实现的叠加问题归为 V2.3 的设计主张。下表区分更早的实现、V2.3 设备选型和后续接线/验证缺口：

| 来源与差异 | 当前路线 |
| --- | --- |
| 4 月 Python additive 实现：保留物理输入，再注入额外修正 | 接管选定物理输入，只提交最终结果 |
| V2.3 设备选型：自有 KMDF/VHF | 复用已实测的 Interception + FakerInput；自有驱动留作另有证据触发的备选 |
| V2.3 分路：移动虚拟输出，按钮和滚轮走原设备 | 接管期间选定设备的移动、按钮、滚轮全部经同一虚拟鼠标 |
| 接线验证缺口：枚举/提交成功未证明最终接收 | 源输入、提交台账、独立接收、恢复四端证据共同验收 |
| 9 月旧 hook 性能证据的适用范围 | 新设备后端和正式 controller 组合重新测量 |

物理鼠标保持启用，继续给过滤器提供数据。“排除”指原始正常鼠标事件被消费，不要求设备从 PnP 列表消失。HidHide 官方明确不支持鼠标/键盘；本路线使用已经验证的鼠标过滤与虚拟输出组合。[HidHide FAQ](https://docs.nefarius.at/projects/HidHide/FAQ/)

## 4. 按职责实施的接入工作

以下约束现已落实为正式模块；真实设备和游戏验收范围仍按第 2、5 节区分。

| 模块 | 复用位置 | 必须完成的接线 |
| --- | --- | --- |
| 设备身份、输入捕获 | `mouse_virtual_hid_transport.*` 复用 Interception SDK | 完整硬件 ID 唯一匹配、真实物理祖先及不同 Raw Input 端点校验；只捕获指定源，全包消费 |
| 独立进程监督 | `mouse_runtime_supervisor.*` | 父进程持有 F12、监督 controller 心跳；worker 独立监视父进程；实际旧进程结束后才允许恢复 helper 发送中性报告 |
| controller 组合 | `mouse_controller_session.*` | 本 tick 消费输入窗口、运行完整共享控制链，再按 epoch/window token 提交唯一 final counts |
| HID 报告提交 | `fakerinput_output.*`、`virtual_mouse_packets.h` | 唯一 FakerInput 提交者；按描述符拆包；记录成功前缀与取消尾部，不重发部分已提交结果 |
| 按钮合成 | `mouse_virtual_hid_transport.*` | 捕获源维护五键状态；虚拟左键为物理持有与当 tick AutoFire 授权合成；有事件的包形成有序窗口 |
| 校准 | `mouse_controller_session.*` | F11 请求在读取源包后决定 Hipfire/ADS；探测、回位和允许的传感器微动使用同一提交者 |
| 启动与构建 | `mouse_runtime_main.cpp`、`mouse_targets.cmake`、mouse launch scripts | `virtual-hid` 为正式默认；`--check-transport` 只预检，不捕获、不发送报告 |
| 验收与证据 | 新 transport/session/supervisor tests、现有 Base/Feature、启动测试 | 实际 session 使用模拟设备与独立接收 oracle，含故意泄漏原包的负对照；进程恢复测试使用真实子进程、模拟设备 |

采用现有两进程职责：controller 的完整 tick 与设备输入/输出留在工作进程，监督者不计算 AI，也不另开一个原样转发者。若需要工作进程内独立接收线程，它只采集和排队，不抢先发原始位移。不要把两套现成启动器并行运行来“组合”功能。

### 4.1 输入窗口与唯一输出

每次捕获赋予源序号与单调时间。一个 controller 窗口只消费一次源 counts，并记录 `source_begin/end`、物理按钮边沿/状态、滚轮、实际 `dt`。final 命令关联会话代际、tick/报告序号、所消费窗口、最终 counts、授权按钮意图及提交时间。

同一窗口先发 `M` 再发 `T` 禁止；等待 AI 时也不能先透传该窗口，随后补发 `T`。正常手动路径在当前控制链中求出 `T=M`，其计数必须精确保留。AI 无实体移动时可以产生新的最终位移，但不能重消费上一窗口的 `M`。

一个逻辑最终结果可以因 HID 位移范围拆为多个报告，也可能因按钮边沿产生零移动报告。“只输出一次”指每份位移工作只提交一次，不要求每个源包、controller tick 与 HID 包数量总是 1:1。拆包总量守恒，滚轮不得在每个分片重复，持续按钮状态重复不产生额外点击。部分分片已提交后若输出失败，记录已提交量与取消量，不能重发整个源包或整个最终结果。

按钮 Down/Up 或 ADS 释放不能因窗口内只保留最后快照而消失；需要保留顺序，并定义移动窗口与关键按钮边沿的边界。混合包不得把原 X/Y 随按键原通道送出。旧 `MouseRelayContract` 的“清零 X/Y，按钮/滚轮原生”属于历史分路契约，不能直接作为新全包接管的 oracle。

### 4.2 按钮、滚轮和校准

旧 `MouseAutoFireButton` 依赖物理边沿已从原通道到达 OS，仍用于旧后端。新 transport 中，物理状态只由捕获源包更新；虚拟输出和系统合并按键状态不能反过来冒充物理来源。F11 选择 Hipfire/ADS 也必须读取捕获的物理右键状态。

正常接管中，虚拟左键状态由 `物理左键持有 OR 当下有效的 AutoFire 持有` 合成；AutoFire 持有仍受现有手动优先和生命周期规则撤销。物理按下可以接管 AI 持有，撤销 AI 不得抬起仍被用户按住的左键；物理松开后必须按当前授权重新裁决，不能恢复旧 AI 持有。右键及其他被支持的按钮来自物理边沿，滚轮按源序列转换。

独立 relay 与正式 transport 都在 capture 前后检查按钮释放，启动边界已有模拟回归。不能沿用旧 session 的状态清零，就把它当作完整物理快照；以后若支持按住按钮启动，需要另外实现有证据的状态交接。不得靠虚拟/物理混合的轮询状态猜测来源。

复用当前已验证的 120 单位垂直滚轮转换。绝对坐标、非整格高分辨率滚轮或不支持标志维持显式失败与恢复，不静默截断或改走原通道。扩展设备格式需要单独的源样本和转换验收。

校准探测和回位也只能通过同一提交者。保留同目标代际、实际提交后的画面时间边界、按键变化取消以及输出失败取消；校准 tick 的手动量按既有规则合入唯一结果，不另开 `SendInput` 通道。

### 4.3 恢复、时钟与证据

输出端、设备身份和紧急键就绪后才接管。无目标/无 AI 是控制模式变化，仍保持接管并虚拟透传；F12、关闭、失去 owner、超时或设备错误才退出设备接管。失败后不自动反复重新接管。

退出时先撤销旧代际的提交权，清理虚拟持有并解除物理过滤；一条清理失败不能阻止另一条恢复尝试。记录两端结果。未提交的过时 AI/量化余量不能在恢复后追发；已经提交给系统的报告不能假定可撤回，必须实测恢复边界的在途报告。真实输入恢复以释放后的原设备事件证明，不能以进程退出码代替。

保留研究中对异常 `GET_FILTER` 查询的处理与负对照：本机查询曾返回零字节，不能再以读回零直接断言捕获失败，也不能以 setter 成功直接断言隔离成功。启动状态与后续实际源包、独立接收要分开判定。第三方查询的底层原因仍未确定；`kernel_exit_pending` 的异常会话继续判失败。

controller 保持 1 ms 目标周期，并按实际 `dt` 积分，同 tick 完成身份、ADS/BodyLock、authority、AutoFire 与 final-T。当前每个 controller tick 非阻塞排空源包到有界 FIFO，再按事件边界消费；Vision 仍异步提供观测，不按 Vision 帧率发鼠标量，也不重启已退休的异步 AI proposal 旋钮。

新后端记录源接收、控制窗口截止、最终求解、HID 提交、独立接收的单调时间，区分源事件率、controller tick 率、非零 HID 输出率和游戏帧率。报告 P50/P95/P99/max、队列积压、丢弃/重复、超期和退出尾部；没有硬件时间戳时“source→receive”只表示应用观察到的链路，不冒充 USB 到画面的完整延迟。性能阈值与 matched 条件在候选测量前冻结，目前没有新后端 1000 Hz 或延迟通过结论。

## 5. 完成判据

先保存能暴露旧问题的 RED 对照，再接线。已有泄漏负对照和原始桌面记录可复用；新增 session 接入必须另有能发现双发、错误代际和按钮状态错误的测试。

| 门禁 | 触发 | 必须成立的 oracle |
| --- | --- | --- |
| 物理排除与来源 | 所选设备有非零输入，混合移动/按钮/滚轮；另有非选定鼠标 | 接管窗口原设备有效事件为 0；非选定设备不被接管；虚拟报告不回灌 |
| 原样 / 零 / 反向 | 正式 runtime 的三种 relay-test，AI 明确关闭 | 各窗口分别 `V=M / 0 / -M`，有足够源运动；不能用净和 0 或零源活动通过 |
| final-T 替代 | 冻结 `M=(10,0), T=(3,0)`，再测 `T=(-3,0)` 及零手动 AI | 下游分别仅 3、-3 和当前 AI 最终量；不能出现 13、7、重复消耗或两个发送者 |
| 计数与事件时序 | 同 tick 多包、按钮 Down/Up、move+wheel、HID 大量拆包、不同 dt | 手动总 counts 守恒；事件无丢失/重复；混合语义顺序与冻结源一致 |
| AutoFire 与物理优先 | 自动持有中物理按下/松开、撤销 AI、ADS 释放、目标丢失 | 物理输入不被误抬；旧自动持有不复活；虚拟按钮不回写物理状态 |
| 输出与恢复边界 | 真人 F12、窗口关闭、controller 停顿、worker/监督者失效、设备拔插 | 旧代际拒绝新提交，虚拟持有清理有证据，物理恢复可观察；各真实/模拟覆盖分别报告 |
| controller 产品行为 | 正式共享链的 ADS、BodyLock、身份切换、手动接管、校准、AutoFire | 使用相关现有 Base/Feature 和 Mouse 回归；接线前后固定相同算法输入与参数，不借接线改变 final-T |
| 新链路性能 | 同鼠标、polling 设置、主机负载、controller 与输出版本 | 独立接收序列完整，报告频率/延迟分位与超期；旧 Win32 数据不代替本项 |
| 目标游戏接受 | 目标游戏内三模式，再运行 ADS/BodyLock 与手动脱离 | 游戏行为与指定虚拟结果一致，原输入没有叠加；matched 实测和用户手感确认后才谈游戏改善 |

前三种纯转写模式可复用现有分析器；正式 AI 模式下输出本来就可能不同于源量，应关联 controller 最终命令检查，不能删除源输入校验来强行让旧分析器通过。跨种类事件交错已有模拟 transport 顺序断言，controller 最终命令关联已有真实 session 的独立模拟接收断言；这两项仍需正式入口实物采集。真实故障恢复仍待真人覆盖。

当前可复用的离线入口是 `scripts/verify/mouse_native.ps1`、`tests.test_startup_scripts`、`native/mouse_link` 的 CTest、`tests.mouse.test_virtual_relay_oracle` 和 `tests.mouse.test_virtual_relay_parent_exit`。已有独立 relay 的 8/8 CTest、7/7 Python 是研究交付记录，不是当前整个未提交工作区或未来新接线版本全部通过的证明。

## 6. 当前使用入口与下一项交付

| 入口 | 当前真实职责 |
| --- | --- |
| `scripts/launch/mouse_virtual_relay.bat` | 已验收的独立设备转写工具，不运行 Vision/AI |
| `scripts/launch/mouse_start.bat` | 正式 AI 入口；默认 `virtual-hid`，Interception 全包捕获后由 FakerInput 独立输出 |
| `scripts/launch/mouse_input_monitor.bat` | 被动观察输入来源，不拥有输入排除权 |

正式 runtime 与独立转写工具均默认按完整硬件 ID 定位 Razer Viper V3 HyperSpeed 活动接口；编号与 Raw Input 句柄可能重启后变化，不能把 13 或历史句柄写死为身份。独立工具热键为 Ctrl+Alt+F6 原样、F7 零移动、F8 反向、F12 退出。正式 runtime 使用 `--relay-test passthrough|block|invert --duration-seconds 5` 选择无 Vision 的同 session 测试；F11 校准、F12 退出。完整操作见[桌面验收记录](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)。两个接管入口不得同时运行。

**接入代码已完成。** 后续正式运行已有完整内部日志，并据此加入[目标点纠偏与容差](MOUSE_TARGET_POINT_CONTROL_20260908.md)。内部日志不替代独立接收端证明；下一项实测仍包括正式组合的三模式接收对账、真人 F12、故障恢复及新版目标游戏 A/B。当前配置和文档阅读顺序以 [Mouse Overview](MOUSE_OVERVIEW.md) 为准。

FakerInput 上游已于 2026-08-19 归档，当前路线基于留存版本与本机证据；系统/驱动/设备变更后要重新验证相应链路，不承诺未测版本兼容。[FakerInput 仓库](https://github.com/Ryochan7/FakerInput)
