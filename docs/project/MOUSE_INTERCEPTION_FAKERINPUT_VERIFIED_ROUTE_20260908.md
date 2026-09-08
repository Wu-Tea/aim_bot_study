# 指定实体鼠标拦截 + 独立虚拟鼠标：已验证的输出与剩余工作

> 后续状态：驱动安装、重启和真实桌面转写已经完成，见[最终桌面验收](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)。正式 AI runtime 的接入分工和门禁见 [Mouse 输入替代路线](MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md)。本文保留最初仅验证虚拟输出时的研究证据；下文“Interception 未安装”“完整目标仍未实现”描述当时状态，不是当前继续工作的起点。

日期：2026-09-08。按用户要求，全程未使用 subagent。

后续实施状态见 [等待重启的实施交接](MOUSE_VIRTUAL_RELAY_READY_FOR_REBOOT_20260908.md)：用户已授权并完成 Interception 安装，独立转写程序已构建；系统尚未重启，真实接管仍待验收。以下为安装前的调查和输出实验记录，不能作为安装后设备仍正常的证明。

## 结论与准确状态

有具体的软件驱动组合可继续实施：**Interception 拦截指定物理鼠标，应用处理输入，FakerInput 从独立虚拟 HID 相对鼠标输出最终结果。**

本次新增的关键实测是：这台 Windows 11 电脑早已安装 FakerInput 0.1.0，而且它的相对鼠标输出在正常桌面已通过跨进程接收验证。无需先开发、签名、安装自有 VHF 驱动，才能验证虚拟输出。

完整目标仍未实现：Interception 驱动缺失，指定实体鼠标尚未接管；现有 Interception transport 仍向原设备通道发送，尚未改成 FakerInput 输出。不能把本次输出通过写成“物理信号已屏蔽”。

| 能力 | 本次状态 |
| --- | --- |
| Windows 正常识别独立虚拟 HID 鼠标 | 实测通过 |
| 应用向独立虚拟相对鼠标提交位移 | 实测通过 |
| 两个独立进程从该鼠标接收完整位移序列 | 各 64/64，通过 |
| 屏蔽指定物理鼠标的原始鼠标事件 | 未实现、未实测；Interception 未安装 |
| 实体输入转写到虚拟鼠标，避免原始与最终位移叠加 | 仍需连接两端并实测 |
| 按钮、滚轮、退出、断开、故障恢复 | 本次未验证 |
| 1000 Hz 稳定性、延迟、目标游戏接收 | 本次未验证 |

## 设备与接口证据

`pnputil /enum-devices /deviceid root\FakerInput` 找到正在运行的 `ROOT\SYSTEM\0003`。已安装包为 `oem62.inf`，版本 `06/03/2021,0.1.0.0`，签名者 Travis Nickles。

相对鼠标子设备为 `HID\SYSTEM&Col03\1&2d12bed1&0&0002`，Windows 描述为 `HID-compliant mouse`，状态 Started，ProblemCode 0；父设备确为上述 FakerInput。它的硬件 ID 包括 `HID\FakerInput&Col03`，设备栈包含系统 `mouhid` / `mouclass`。

原始核查输出保存在：

- `artifacts/mouse_link/fakerinput-research-20260908/installed-fakerinput.txt`
- 同目录 `installed-relative-mouse.txt`
- `runs/mouse_input_monitor/fakerinput-device-map-current-20260908/devices.csv`

上游 `FakerInputDll` 固定源码版本 `194f8752a5135765645986e7050f2ce5102f9ced` 在本机构建成功，连接到已安装设备，API 版本为 1。其 `fakerinput_update_relative_mouse` 接口接受按钮状态、相对 X/Y、垂直及水平滚轮参数。软件库通过 HID 控制报告提交，不使用 SendInput。

FakerInput 是 UMDF HID 驱动包。这纠正了“虚拟鼠标只能先完成本项目自有 KMDF/VHF 包”这一过窄选型：微软 VHF 的源驱动仅支持内核模式，不代表所有虚拟 HID 都必须采用本项目的 VHF 路线。

## 正常桌面接收实验

新脚本：`scripts/verify/fakerinput_virtual_mouse_probe.py`。默认仅连接并读取版本；只有显式指定 `--send-motion` 才提交位移。发送实验要求提供已通过 PnP 父子关系核对的完整 Raw Input 设备路径，不能通过“没有注入标记”猜测来源。

本次刺激冻结为 `(1,0),(-1,0),(0,1),(0,-1)` 重复 16 次，共 64 包。每包间隔约 8 ms；不发送点击、滚轮或键盘输入。发送前等待两个独立监听进程完成 Hook / Raw Input 注册后的就绪消息。

| 接收端 | 虚拟设备位移包 | 顺序和值 | X/Y 净位移 | X/Y 绝对 counts | 丢包/读取错误 |
| --- | --- | --- | --- | --- | --- |
| 接收端 0 | 64 | 与发送序列完全一致 | 0 / 0 | 32 / 32 | 0 / 0 |
| 接收端 1 | 64 | 与发送序列完全一致 | 0 / 0 | 32 / 32 | 0 / 0 |

两路接收均绑定同一独立虚拟鼠标句柄 `65616`，位移都是相对 counts，没有按钮或滚轮事件。两个进程各收到一份是广播给不同接收者的正常行为，不是同一个接收端收到重复输入。

完整发送台账和结果：`runs/mouse_input_monitor/fakerinput-output-desktop-20260908/summary.json`，对应 `receiver_0`、`receiver_1` 保存 CSV 和监听器状态。

监听器 SHA-256：`24FBB7DAB0B579C4161B68C255214D09830720CC5C8247AB4BF4B44E36543017`。

本地构建客户端 SHA-256：`97F343E2E1B668F2767E736443D247585065C7A70D1EE52CC96F9BBC0533138B`。

同一实验在受限执行环境中，发送成功但两个监听器均收到零个虚拟位移；原始记录保留在 `fakerinput-output-20260908/`。切换正常桌面执行后通过。执行环境影响是本次 A/B 的推断；没有把受限环境的零接收当成驱动失效，也没有修改驱动掩盖该现象。

离线判据负对照：对已收集 CSV 分别制造缺包、重复、顺序颠倒、全部零位移，全部拒绝通过。它们只验证分析判据，不是新的硬件实验。记录位于 `artifacts/mouse_link/fakerinput-research-20260908/oracle-negative-controls.json`。当前源码监听器独立 Release 构建与 MouseInputProvenance CTest 通过。

## 包的正常签名与维护状态

另外下载并只读检查了上游 x64 的 0.1.0 和 0.1.1 安装包，没有运行安装器。通过 Windows Installer 只读数据库 API 提取内嵌 CAB，未执行 MSI 自定义安装动作。

0.1.1 发布于 2025-12-19；MSI SHA-256 为 `4C0AEFB7340051A91D606776243298B5CD1143EF5508BBAE6800C474F9ED0840`，与 GitHub asset digest 一致。MSI、驱动 DLL、CAT 的 Authenticode 检查有效；`signtool verify /pa /v /c fakerinput.cat FakerInput.inf FakerInput.dll` 返回 0，两个文件均属于已签名 catalog。INF 本身没有嵌入签名不是此项验证失败。

上述签名证明对应包的来源和完整性；没有把 Authenticode 检查写成 WHQL、内核签名或 0.1.1 本机加载验收。本次输出实测使用的是已有 0.1.0，未升级。

FakerInput 及其客户端仓库于 2026-08-19 归档。现有功能可实测，但不能承诺后续 Windows 更新有维护保障。

## 完整转写的实现要求

拟采用的数据链路：

```text
指定实体鼠标
  → Interception 在鼠标输入路径截住原包
  → 应用读取位移、按键、滚轮，产生最终输出
  → FakerInput 独立虚拟相对鼠标
  → Windows / 目标程序接收
```

物理鼠标应保持启用，才能向过滤器提供数据；不能在设备管理器直接禁用它来替代拦截。这里的目标是阻止原始鼠标事件继续送达普通 Windows 鼠标输入消费者，并不承诺实体设备从 PnP 列表消失，或对内核/厂商专用访问路径完全隐身。

实施时需明确以下所有权：

1. 仅捕获用户选定实体设备；排除 FakerInput 的虚拟设备，避免输出被再捕获形成循环。
2. 捕获期间的原始位移、按钮和滚轮由应用消费；最终鼠标报告只经 FakerInput 输出。不能继续沿用现有 transport 的原设备 `interception_send` 作为正常输出。
3. Interception 的按键是边沿，HID 鼠标按键是状态位；桥接层必须维护按键状态。滚轮单位和有符号范围需要实际验证，不能直接按字节转型声称原样保持。
4. 仅在虚拟输出就绪后启用拦截；F12、正常退出和故障恢复要恢复实体输入，并正确释放虚拟按钮。上游的客户端断开不应被假定会自动释放全部按键。
5. 大位移拆包必须保持 counts 总量；有非零源位移的零输出模式、反向输出模式、原样转写模式要使用独立接收端验证。零输出时原始设备不得继续贡献鼠标事件。

当前 Interception v1.0.1 DLL 已存在，鼠标和键盘驱动服务 `mouse` / `keyboard` 均返回服务不存在（1060）。安装器路径已准备：`artifacts/mouse_link/deps/Interception/command line installer/install-interception.exe`。安装会涉及全系统键鼠过滤，可能需要重启；现有过滤项必须保留。本次没有执行这项系统变更。

## 官方来源

- [HidHide FAQ：明确不支持鼠标与键盘](https://docs.nefarius.at/projects/HidHide/FAQ/)
- [Interception 官方项目：设备过滤、读取、发送与驱动安装要求](https://github.com/oblitum/Interception)
- [Interception 上游发送实现](https://github.com/oblitum/Interception/blob/master/library/interception.c)
- [FakerInput 客户端相对鼠标 API](https://github.com/Ryochan7/FakerInputDll/blob/194f8752a5135765645986e7050f2ce5102f9ced/FakerInputDll/fakerinputclient.h)
- [FakerInput 0.1.1 发布包](https://github.com/Ryochan7/FakerInput/releases/tag/v0.1.1)
- [FakerInput 官方仓库与归档状态](https://github.com/Ryochan7/FakerInput)
- [微软 VHF 的虚拟 HID 架构与内核源驱动边界](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
