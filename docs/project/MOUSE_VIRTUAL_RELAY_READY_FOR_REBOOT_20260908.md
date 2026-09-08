# 鼠标转写实施交接：等待安装后的 Windows 重启

> 后续更新：用户已重启，驱动与 Raw Input 枚举均恢复，移动、左右键、滚轮已完成真实桌面验收。当前状态见 [桌面验收结果](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)；以下保留重启前事实。

日期：2026-09-08。用户已明确授权安装 Interception 并完成转写，要求不使用 subagent、减少阶段汇报、最终一次性交付。不要再次询问是否安装或是否采用该方案。

## 当前状态

**功能代码和独立验证工具已完成；完整实体链路尚未验收。下一项外部前提是重启 Windows，完成 Interception 安装。**

上游安装器本身包含明确提示：`Interception successfully installed. You must reboot for it to take effect.` 本次安装返回 0。没有重启电脑，没有关闭驱动签名或安全保护。

已完成：

- 按用户授权，通过标准 Windows 管理员提升执行已核验的 Interception v1.0.1 `/install`。
- 安装前后键鼠类过滤项已保存；保留了原有 `klmouflt.K4W-21-26` / `mouclass` 和 `klkbdflt.K4W-21-26` / `kbdclass`。
- 已安装的 `mouse.sys` / `keyboard.sys` 与先前核验的上游文件 SHA-256 一致，签名检查有效；鼠标驱动 `signtool verify /kp` 通过。
- 独立 C++ `mouse_virtual_relay.exe`、启动脚本、构建脚本、接收分析器均已生成。
- 六项 CTest 和七项 Python 测试通过，包括保留的 Interception 行为测试、泄漏负对照、包转换、生命周期、工作进程卡死及真实父进程死亡的模拟验证。

尚未完成：

- 安装后的系统重启。
- 新 C++ 后端的真实虚拟输出与实体鼠标接管验证。
- 原样 / 零位移 / 反向三阶段、五键、滚轮、F12、拔插及故障恢复的真实设备验收。
- 1000 Hz、完整延迟测量、目标游戏接收、接回 AI controller。

不能把编译、模拟测试或安装器返回 0 写成“物理信号已屏蔽”。

## 安装后检查的具体情况

安装日志：`runs/mouse_link/interception-install-20260908/`。

安装时原始直接启动因缺少管理员令牌失败，没有改动；随后以 `Start-Process -Verb RunAs` 启动同一核验安装器，返回 0。注册表中出现 `mouse` / `keyboard` 服务项及类过滤项，文件也已安装；SCM 查询仍返回 1060。

为检查能否不重启系统完成加载，只通过标准 `pnputil /restart-device ROOT\SYSTEM\0003` 重新绑定了现有 FakerInput 虚拟设备树。没有重启实体鼠标、实体键盘或整个电脑。PnP 显示虚拟键鼠栈中已经出现新的过滤驱动，但 Interception 的 20 个接口均无法返回硬件 ID（错误 1），没有可接管鼠标。

**此次虚拟设备重绑定后，FakerInput 相对和绝对鼠标暂时不再出现在 Raw Input 设备列表中（7 → 5）。** 不要继续引用重绑定之前的 64/64 结果，声称当前虚拟链仍然可用。已安装 FakerInput 的控制接口仍可连接，PnP 子设备仍显示 Started；这不等同于 Raw Input 接收正常。原始记录在 `runs/mouse_input_monitor/fakerinput-after-filter-install-20260908/`。

因此下一步先完成安装器要求的正常系统重启，再检查加载和设备枚举。尚无证据确定重启一定解决上述问题；如果重启后继续出现缺失，应按具体系统错误定位，而不是继续改监听标志或宣称成功。本轮没有查到匹配 mouse/keyboard 驱动的近期 Code Integrity 拒绝记录，但这也不是完整兼容性证明。

## 新程序与使用入口

- 启动：`scripts/launch/mouse_virtual_relay.bat`
- 二进制：`artifacts/mouse_link/virtual-build/Release/mouse_virtual_relay.exe`
- 构建：`D:/env/python/python.exe scripts/verify/build_mouse_virtual_relay.py`
- 列举/预检：`mouse_virtual_relay.exe --list`，不启用物理过滤。
- 正常转写：`mouse_virtual_relay.exe --source <核验后的设备编号>`。
- 三阶段验收：`mouse_virtual_relay.exe --source <编号> --acceptance --out <新目录>`。

`--acceptance` 在捕获就绪后执行原样、零位移、反向各 6 秒，总计约 18 秒，然后退出。需要用户实际持续移动所选鼠标，另行覆盖按钮和滚轮。无实体输入的记录不能通过。可用 `--seconds 1..300` 做定长单模式实验。

热键都是 Ctrl+Alt：F6 原样，F7 零位移，F8 反向，F12 释放并退出。普通启动不限运行时长；内存证据缓冲区写满后继续转写但标记证据截断，分析器拒绝把截断记录判为通过。

程序独立运行设备链路，不启动 Vision 或 AI。现有 AI runtime 的默认输出仍是原来的 Interception transport；本次未把未验收的虚拟输出接到 AI。

## 实现和验证边界

- `native/mouse_link/virtual_mouse_relay.cpp`：独立接收/监督进程和输入工作进程；按硬件 ID 固定设备，核对 USB/Bluetooth 祖先和独立 Raw Input 路径，拒绝虚拟输出回捕及身份不明确的设备。
- `fakerinput_output.cpp/.h`：直接通过 FakerInput 公开 HID 报告协议输出，不依赖本地研究客户端 DLL。独占打开输出控制端点，避免另一映射器同时写入；在物理接管之前检查输出和 API 版本。
- `virtual_mouse_packets.h`：相对 counts 拆包保持总量，五键边沿转状态，滚轮从 Windows 120 单位换算 HID 单位。依据实际 HID 描述符确定位移范围，不信任上游注释中的最小值。
- 非整格高分辨率滚轮、不支持的绝对坐标/标志、同包同键冲突边沿会终止接管并报告错误；不把它们静默当普通相对鼠标处理。普通垂直/水平 120 单位滚轮有离线覆盖，真实硬件待验收。
- 工作进程读取 Interception 的事件通知，不依赖固定 1 ms 轮询限速。只给既有 `MousePacketDriver` 增加可选 `wait_for_input`，默认不改变旧消费者的非阻塞语义。
- 双向进程存活/心跳检查：监督进程可结束卡死的工作进程；监督进程退出/卡死时，工作进程自行释放输入。关闭时清理虚拟按钮；关闭边界会取消尚未输出的排队手势，不能将故障会话视为完整转写证明。
- 关闭和父进程死亡的模拟已测试；系统驱动实际恢复、同时多个进程失效、驱动无法完成取消等情况没有实测，不宣称已全部覆盖。

会话保存 `events.csv`、`devices.txt`、`worker-pid.txt`、`session.json`。CSV kind：1 = 捕获源包，2 = 虚拟提交，3 = 独立接收端看到的原设备输入，4 = 独立接收端看到的虚拟输入，5 = 虚拟按钮复位报告。

运行 `D:/env/python/python.exe scripts/verify/analyze_mouse_virtual_relay.py <会话目录>`。分析器独立检查：每个源包到输出的总位移/滚轮、按钮状态、虚拟接收序列、原设备事件泄漏、有效源运动量和会话健康。缺包、重复、源与发送不一致、模拟、错误或证据截断均不能通过。

## 重启后继续顺序

1. 读取本文，确认用户已经重启；不要再次安装驱动。
2. 查询 Interception 驱动、FakerInput PnP 和 Raw Input；运行新程序 `--list`。先确认两端正常，再启用任何物理过滤。
3. 当前主鼠标是 Razer Viper V3 HyperSpeed，活动 Raw Input 接口对应 `VID_1532&PID_00B8&MI_00`。设备编号需要重新枚举，不能沿用重启前编号；同设备还有 MI_01 的另一鼠标集合，必须区分。
4. 如果新 C++ FakerInput 输出仍失败，先独立验证输出。此前成功的上游 DLL 微位移探针可以作为对照，但不能代替新后端的验收。
5. 请求用户一次完成持续移动/按键/滚轮动作，运行三阶段采集；按实际覆盖报告。随后验证恢复，再决定是否接入 AI。
6. 若加载或兼容性持续失败，先记录具体错误和影响，再选择修复或恢复；不关闭保护、不用 SendInput 替代并伪称设备链已完成。

建议将“已安装、待重启、虚拟 Raw Input 重绑定后缺失、真实接管待验收”同步进 `.agent-context/`；本轮没有修改该目录。
