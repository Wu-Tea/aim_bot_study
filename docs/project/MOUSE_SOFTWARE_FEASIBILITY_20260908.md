# 鼠标软件链路可行性：实测结论和设备后端前提

## 结论

**Windows 提供软件驱动实现鼠标过滤与虚拟 HID 的机制；但当前项目还没有完成实体接管。已测试的 Raw Input 监听 / NOLEGACY / SendInput 组合不会创建新设备，也没有给监听进程提供独占接收。** 这不是“所有用户态 API 都已穷举”的结论，也不是目标游戏兼容性证明。

设备后端有继续验证的依据：项目现有 KMDF/VHF 驱动源码在本机成功构建，现成 Interception 驱动文件通过签名检查。但安装、加载、实体输入接管和接收端验收仍是独立步骤，不能从编译或签名结果推导通过。

## 本次实际执行的跨进程实验

运行：

```powershell
& D:/env/python/python.exe scripts/verify/mouse_user_mode_feasibility.py
```

每阶段使用外部 Python 进程调用 SendInput，发送 64 个带本轮标记的微小往返位移，两个轴各累计 64 个绝对 counts、净位移均为零，不发送点击。接收端是分别启动的 `mouse_input_monitor.exe` 进程。父进程等到每个子进程完成 Hook 和 Raw Input 注册并输出就绪信号后才发送，不靠固定等待时间假定已就绪。

| 阶段 | 发送 | 接收端 A 的 Raw 包数 | 接收端 B 的 Raw 包数 | 发送前后鼠标设备数量 |
| --- | --- | --- | --- | --- |
| 一个普通接收端 | 64 | 64 | 不适用 | 7 → 7 |
| 两个普通接收端 | 64 | 64 | 64 | 7 → 7 |
| A 启用 NOLEGACY，B 普通监听 | 64 | 64 | 64 | 7 → 7 |

所有接收端的对应 Hook 注入事件数也与 64 匹配，两个轴的净位移和绝对位移都符合发送值，没有丢事件和读取错误。设备句柄集合在注册前、注册后、发送后和关闭后保持一致。两个负对照验证：如果接收端缺包或包数翻倍，分析判据会拒绝通过。

这些结果说明：

- 两个进程可以分别收到同一批输入，监听 A 不会消费掉 B 的输入。
- A 的 NOLEGACY 设置没有屏蔽 B 的 Raw Input。该设置在微软文档中也被定义为针对注册应用的 legacy 消息。
- SendInput 在这次实验中没有枚举新鼠标设备。观察到的注入 Raw 事件没有设备句柄。
- 这三组刺激是 SendInput，不是实体鼠标，所以不能冒充“物理信号已屏蔽”的实验证据。
- 没有测 USB 轮询率，没有测目标游戏，不从 64 包的突发发送推导 1000 Hz 结论。

完整原始记录：`runs/mouse_input_monitor/software-feasibility-20260908/summary.json`，各阶段子目录保存每个接收进程的 CSV 和 summary。

监听器 SHA-256：`B8E668B1B19F7C343FD610B9052F7571D7B742007A8D5B19454416D34227B967`。

实验脚本 SHA-256：`8380D98726A3D6230AFDA62B79F770135FF71419BDC94988802FD8060916176E`。

## 设备后端检查

主机为 Windows 11 Pro，版本 10.0.26100。

| 项目 | 实际结果 | 不能由此推导的结论 |
| --- | --- | --- |
| HidHide | 驱动正在运行；官方明确不支持隐藏鼠标/键盘/触控板 | 不能因为本机已安装就把它作为鼠标过滤后端 |
| Interception DLL | 存在、哈希正确、加载和导出检查通过；创建驱动连接失败 | 不是缺少 DLL 导致的启动失败 |
| Interception 驱动服务 | 本机未发现相应 mouse/keyboard 服务，类过滤列表也未包含它们 | 尚未执行真实驱动加载验收 |
| 缓存的 Interception 驱动 | 鼠标和键盘 `.sys` 的 Authenticode 与 `signtool verify /kp` 均通过 | 不代表 Windows 11 运行兼容、系统已加载或游戏认可 |
| KMDF 最小探针 | 本机 WindowsKernelModeDriver10.0 工具链编译成功 | 只证明编译工具链可用 |
| 现有 `cod_mouse_relay_driver.vcxproj` | 在独立实验输出目录构建成功，产生 `cod_mouse_relay.sys` | 不代表驱动行为正确或已经安装 |
| 自有驱动部署前提 | 生成物未签名，项目未产生 INF/CAT，构建日志明确跳过相关步骤 | 编译产物不能当成已经可正常部署的驱动包 |

源码驱动构建输出：`runs/mouse_input_monitor/software-feasibility-20260908/relay-build/cod_mouse_relay.sys`。

环境审计：同目录 `driver-readiness.json`；构建日志 `kmdf-build-probe.log`、`relay-build.log`；键盘内核签名日志 `keyboard-kernel-signature.log`。鼠标签名工具也实测退出 0。

## 继续实施的判断

不再把新增监听标志、提高 SendInput 频率或继续调 ADS/BodyLock 当作设备后端问题的解决办法。现有后端下一项有价值的实验，是在正常 Windows 驱动支持下验证加载与真实过滤/转写。创建虚拟鼠标本身也不会消除物理输入，必须另行验证排除。

优先评估现成 Interception 正常安装后的能力，可以复用已有代码；其上游明确记录的测试范围止于 Windows 10，因此当前 Windows 11 主机上的加载与行为必须实测。若正常加载失败，应记录系统返回的具体原因，不能改用隐藏来源、绕过检查或关闭安全保护来宣称兼容。

### 系统变更审查范围（尚未执行）

候选包为已核验的上游 Interception v1.0.1，安装器：
`artifacts/mouse_link/deps/Interception/command line installer/install-interception.exe`。

安装器 SHA-256：`E137863A79DA797F08E7A137280FF2A123809044A888FD75CE9C973198915ABE`。

归档 SHA-256：`AD038963D6413055765128B0B931F6E765147C9916DBA79E65D872B261F9AF10`。

该组件涉及全系统鼠标和键盘过滤，不只影响当前应用。本机这两类设备已存在其他过滤项，应完整保留；不手动覆盖过滤列表。安装或卸载可能需要重新启动系统，本次没有安装、卸载、修改注册表过滤项或重启。

这项系统驱动变更需要用户明确确认后执行。确认前可完成的监听对照、签名检查、源代码构建和现状审计已完成。确认不应被解释为允许关闭安全保护或自动重启。

## 官方依据

- [RAWINPUTDEVICE：NOLEGACY 仅影响注册应用的传统消息](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice)
- [VHF：通过内核 HID 源驱动枚举虚拟设备](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-)
- [HidHide FAQ：鼠标不受支持](https://docs.nefarius.at/projects/HidHide/FAQ/)
- [Interception：接口用途、安装要求和已测试系统范围](https://github.com/oblitum/Interception)
