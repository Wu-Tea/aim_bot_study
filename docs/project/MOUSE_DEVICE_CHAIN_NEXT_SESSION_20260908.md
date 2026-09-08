# 下次接手：先交付鼠标设备链路

## 当前接手范围（2026-09-08 桌面验收之后）

先读 [Mouse 输入替代路线](MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md)，再读[最终桌面验收结果](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)。Interception 已安装、用户已重启，独立转写已完成移动、左右键和上下滚轮的真实桌面验收；两次合计 16,487 源包，接管区间原设备泄漏 0，定时退出后输入恢复。不能继续按“驱动未安装/等待重启”接手。

正式 `MouseControllerSession` 现已接到 Interception 全包捕获与 FakerInput 唯一虚拟提交者，`mouse_start.bat` 默认 `virtual-hid`。代码与验证见[集成记录](../../runs/mouse_virtual_runtime_integration_20260908/README.md)。`mouse_virtual_relay.bat` 仍是不含 AI 的独立设备程序；不要同时运行两套接管入口。

代码和离线回归已覆盖整个源包、输入窗口唯一消费、AutoFire 与物理按钮的状态合成、旧代际输出拒绝、独立紧急恢复。之后按用户反馈完成了配置、日志、压枪、BodyLock 范围/加减速及目标点容差，最新门禁为原生 34/34、Python 30/30；接手先读 [Mouse Overview](MOUSE_OVERVIEW.md) 和[目标点修正记录](MOUSE_TARGET_POINT_CONTROL_20260908.md)。已有正式运行内部日志，但新增控制策略还需匹配游戏 A/B；设备接收和故障恢复未测项保留。

仍缺：正式 AI 组合的实物接收证据、目标游戏接受、新链路 1000 Hz/完整延迟、跨事件种类的实物交错顺序采集、真人 F12/拔插/崩溃恢复。侧键不在用户本次真人验收范围。已有模拟和桌面结果不扩大到这些未覆盖项。

## 以下为早期调查记录

下文保存设备方案确定与安装之前的事实、工作区注意事项及当时的交付顺序；其中“最新”“当前”“未安装”均指早期调查时点，后续执行以本文顶部和新路线为准。

早期更新：已完成跨进程用户态可行性实验、驱动签名检查和现有 KMDF/VHF 源码构建。优先阅读 `docs/project/MOUSE_SOFTWARE_FEASIBILITY_20260908.md` 与其原始报告。用户态三阶段各发 64 包，监听与 NOLEGACY 均未排除另一接收进程，设备集合未变化。驱动源码能编译，但自有包缺签名和 INF/CAT；当时 Interception 未安装。全系统键鼠驱动变更在当时尚待明确确认。

## 用户最新优先级

当前主要困难是物理鼠标输入的接管与转写输出。下一次工作应集中解决这条链路，不再以控制算法调整、监听器完成或离线测试通过作为整体交付。

目标行为：指定物理鼠标的输入进入应用，默认原样转发；既有 controller 判断需要时才修改位移；下游只收到一次最终位移。按钮和滚轮保持原有行为，AutoFire 按既有所有权处理。目标 1000 Hz，需要实际测量。ADS/BodyLock 算法不是本轮优先工作。

“新建鼠标设备”是用户提出的实现方向；必要验收是消除原始与转写输入叠加，不能仅以系统中多出一个鼠标设备判断成功。

## 技术边界

用户态 Raw Input 可以监听设备输入，但监听本身不会让其他应用失去原始输入。SendInput 是事件注入，不负责枚举一个新的 HID 鼠标。微软 VHF 的 HID 源驱动目前是内核模式组件。因此，软件实现这类功能并非原理上不可行，但完整的设备方案需要驱动支持，不能只靠监听回调加一条发送 API。

新建虚拟鼠标与截住物理鼠标是两个独立能力；必须共同验证。操作系统收到转写也不证明目标游戏收到。不要假设设备签名或虚拟设备枚举等同于游戏认可，更不能用来源伪装或关闭保护来替代正常兼容性验证。

依据：
- https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input
- https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-

## 已确认事实与入口

1. 用户启动入口是 `scripts/launch/mouse_start.bat`，默认 Interception。
2. 当前 DLL 存在，固定版本哈希正确，加载成功，8 个必需导出齐全；`interception_create_context()` 返回空，系统驱动枚举没有相应服务。并未安装驱动或完成真实设备接管验证。
3. 监听关闭和开启时，默认预检同样退出 2、正常启动同样退出 1；显式 Win32 注册检查都退出 0。未复现监听器造成的启动冲突。
4. 新启动封装将输出记录到 `runs/mouse_startup/`，保留退出码，运行失败后停留窗口。这只是诊断修复。
5. 监听器同时记录 Hook 注入标志、Raw Input 设备句柄、额外标记。无标记 SendInput 本机实测也进入 Raw Input，因此“无标记就是实体鼠标”不成立。无注入标志/有设备句柄也不是物理真实性证明。
6. 用户的监听记录有真实操作期间的事件，但没有与发送台账配对；不能据此证明应用转写已生效。Hook 与 Raw 两条流不能直接相加算重复。

优先阅读：
- `docs/project/MOUSE_LISTENER_STARTUP_AB_20260908.md`
- `runs/mouse_input_monitor/launcher-ab-1788831400908662000/summary.json`
- 同目录 `dll-probe.json`
- `docs/project/MOUSE_INPUT_PROVENANCE_MONITOR_20260908.md`
- `docs/project/MOUSE_DEVICE_FILTER_OPTIONS_20260908.md`

已有代码，不要重新造轮子：
- `native/mouse_link/`：独立链路工具、接收端、监听器、生命周期验证。
- `native/mouse_native/mouse_interception_transport.cpp`：过滤、读取和发送封装，尚缺真实驱动验证。
- `native/mouse_native/kmdf_relay/`：已有 KMDF/VHF 源码，包括服务回调和 VHF 输出。源码存在不等于已完成驱动构建、安装包、签名、部署或设备验收；启用前必须逐项核查现状。

## 下次工作的交付顺序

先核查现有设备后端能否在这台机器以正常受支持方式加载、枚举并建立连接，再进行指定设备的接管实验。若存在驱动构建、安装、签名或重启前提，应明确具体缺项和影响，不要继续只调整上层代码，也不要将尚未执行的操作记为已完成。

使用独立接收记录与发送台账验证：原样输入只转发一次；有非零源位移时输出零，下游没有位移；反向输出时下游只有反向量；退出、断开与超时能恢复正常输入。分开记录设备层实测、操作系统接收和目标游戏接收。只有前面的证据成立，才接回既有 AI controller 并验证速度与手感。

不能承诺单次对话必然完成需要重启、实体鼠标操作或目标程序验收的工作；但应把能够独立完成的工作连续做完，遇到确切外部前提再报告。

## 工作区注意

存在大量未提交工作，不能整文件回滚或清理。此前还有未完成的 manual judgment/adapter 算法候选；最新监听/启动改动没有重建主 runtime，也没有证明整个当前工作区全绿。此前 tuning fixture 的失败尚未处理。启动测试 21 项通过只适用于启动范围，不能推广为整套鼠标产品通过。

`.agent-context/handoff.md` 仍是 9 月 1 日的 Fusion 交接，不能据此将当前任务切回 Fusion。此文档保存最新鼠标链路接手范围。
