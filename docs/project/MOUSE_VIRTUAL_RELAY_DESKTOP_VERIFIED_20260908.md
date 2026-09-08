# 物理鼠标 → 虚拟 HID 鼠标：桌面验收结果

2026-09-08。用户最终明确验收范围为 **鼠标移动、左键、右键、滚轮**；两个侧键没有绑定功能，不作为本次验收要求。全过程没有使用 subagent。

**上述范围已经完成真实桌面验收。两次正式采集合计 16,487 个物理源包，接管期间独立 Raw Input 接收端看到的原设备输入泄漏为 0。移动、左右键和上下滚轮经 FakerInput 虚拟鼠标输出，接收序列检查通过；退出后原设备输入恢复。**

## 使用入口

双击 `scripts/launch/mouse_virtual_relay.bat`。默认按完整硬件 ID 自动选择当前 Razer Viper V3 HyperSpeed 的活动鼠标接口，不依赖重启后可能变化的 Interception 编号。启动为原样转写，正常运行不限时。

- Ctrl+Alt+F6：原样转写。
- Ctrl+Alt+F7：将移动改写为零位移，按键和滚轮继续转写。
- Ctrl+Alt+F8：反向移动。
- Ctrl+Alt+F12：释放输入并退出。

当前默认设备：`HID\VID_1532&PID_00B8&REV_0100&MI_00`。如果设备缺失或身份匹配不唯一，程序拒绝启动接管。

命令行示例：

```powershell
# 只列设备，不接管
artifacts/mouse_link/virtual-build/Release/mouse_virtual_relay.exe --list

# 三阶段验收，窗口内按空格后才开始；各阶段 6 秒
artifacts/mouse_link/virtual-build/Release/mouse_virtual_relay.exe --hardware 'HID\VID_1532&PID_00B8&REV_0100&MI_00' --acceptance

# 12 秒原样转写窗口，补测按键/滚轮，空格开始
artifacts/mouse_link/virtual-build/Release/mouse_virtual_relay.exe --hardware 'HID\VID_1532&PID_00B8&REV_0100&MI_00' --input-check
```

这是独立的设备转写入口。现有 `mouse_start.bat` / AI runtime 尚未接入此虚拟输出，不应当将两个输入接管程序同时运行。物理设备仍可以出现在设备管理器和设备枚举中；本次验证的是其正常输入事件被截获，最终事件由另一台虚拟 HID 鼠标输出。

## 实测证据

主鼠标当前 Interception 编号 13，Raw Input 句柄 65641。FakerInput 相对鼠标为 `HID\SYSTEM&Col03\1&2d12bed1&0&0002`，Raw Input 句柄 65634。虚拟输出与物理输入有不同设备身份。

三阶段会话：`runs/mouse_virtual_relay/physical-acceptance-query-fixed-20260908/`。

| 阶段 | 物理源包 | 源绝对位移计数 | 输出绝对位移计数 | 结果 |
| --- | ---: | ---: | ---: | --- |
| 原样 | 4,793 | 25,365 | 25,365 | 源到输出逐包一致 |
| 零位移 | 1,096 | 3,537 | 0 | 有实际源移动，输出移动为零 |
| 反向 | 4,988 | 17,769 | 17,769 | 符号反转、总量保持，接收序列正确 |

该会话包含左、右、中键的按下和松开。共提交 10,878 个虚拟报告（含关闭时的中立报告），接收 9,777 个非零移动包。零位移报告不会被当作非零移动包计数。物理泄漏为 0，无接收错误、截断、强制结束或内核退出等待异常。`three_mode_motion_verified=true`。

用户明确确认三个阶段分别呈现正常移动、停住、反向移动，并在结束后恢复正常。

滚轮补测会话：`runs/mouse_virtual_relay/buttons-wheel-query-fixed-20260908/`。

- 5,610 个物理源包，5,611 个虚拟报告（含关闭中立报告）。
- 6 个向上、11 个向下滚轮源包；虚拟接收的方向、数值与顺序一致。
- 物理泄漏为 0，源与虚拟输出的移动总量均为 12,919 counts。
- 无会话错误、接收错误或证据截断。
- 用户确认侧键没有绑定东西，侧键不纳入本次完成标准。

两次会话退出后，监听器分别又收到了 **119 / 148 个原物理设备事件**，位于释放时间戳之后。结合用户恢复确认，这证明退出后确实恢复了物理输入，不只是程序设置了“已退出”标志。

每个会话的 `analysis.json`、`session.json`、`events.csv`、`devices.txt` 保存完整证据。分析器为 `scripts/verify/analyze_mouse_virtual_relay.py`；会话健康、逐包源需求、输出、独立接收、按钮状态、滚轮序列、物理事件泄漏必须同时满足。

额外输出对照：`runs/mouse_virtual_relay/native-output-postreboot-20260908/summary.json`。使用与转写程序相同的 C++ FakerOutput 后端发送 64 个微小往返位移，两个独立监听进程各收到 64/64，顺序正确、相对坐标正确、无丢弃和读错误。

## 启动失败的定位与修正

首次正式验收没有进入倒计时，不能计为通过。最初根据 `interception_get_filter` 读回 0 推断过滤未生效；后续最小对照证明这个推断不成立，因为本机的查询路径自身异常：

1. 不接管的真实预检，工作进程能正常启动、清理并退出。
2. 只通过官方 SDK 设置过滤并立即取消，不调用查询，进程正常退出。
3. 加入过滤状态查询，读回 0；直接核对查询得到 `DeviceIoControl=true`、返回字节数为 0。进程随后存在“退出码已经设置，但进程对象仍未置为完成”的状态。
4. 父进程只等待进程对象完成，会把这个内核退出异常传播为测试窗口迟迟不结束。

客户端修正在 `native/mouse_native/mouse_interception_transport.cpp`：取消对异常 `GET_FILTER` 的依赖，检查官方 SDK 同步 setter 的 Windows 错误，并在转写边界使用实际源包和独立 Raw Input 接收证据验证接管。SDK setter 的调用关系可在[上游公开实现](https://github.com/oblitum/Interception/blob/master/library/interception.c)中核对。

同时，监督进程在工作进程明确关闭输入/输出资源且退出码已设置的前提下，不再无限等待异常的内核退出；如果内核仍未完成退出，会记录 `kernel_exit_pending` 并将会话判为失败，不能冒充成功恢复。

修正后的两秒设备检查正常启动、清理并退出。随后上述两次真实采集均通过，且 `kernel_exit_pending=false`。

这修复了客户端错误依赖该查询而无法启动的问题。**第三方驱动查询为何在本机返回零字节的更底层原因仍未确定；未修改第三方驱动，也未关闭安全保护。** 管理员对照曾被 Windows 返回“操作已被用户取消”，没有运行；由于普通权限下的最小对照已经定位调用差异，后续实测和交付均不依赖这个管理员对照。

## 自动检查与交付边界

- 最终 CTest **8/8 通过**：包转换、既有 Interception 行为、查询异常回归、泄漏负对照、生命周期、工作进程卡死、内核退出异常负对照、输入来源区分。
- 最终 Python **7/7 通过**：独立验收分析器的错误样本与父进程死亡后的模拟工作进程退出。
- 查询异常回归的替代 SDK 只在构建目录的 `filter-query-contract` 子目录使用，未进入真实运行目录。
- 最终启动文件 SHA-256：`befb72425ee0b09235d38caddc861076619917a9ff40066cf4781ad9ffd9857f`。
- 运行目录的官方 `interception.dll` SHA-256：`ab88164c11b1b48488772d4c3bfaa4509d5b0ae9dbc5a691dc4f96f0260443c8`。
- 构建日志：`artifacts/mouse_link/virtual-build/verification-delivery.log`。

一次 Python 生命周期测试曾与等待中的真实验收窗口争用全局热键而失败；结束验收窗口后，最终整组 7 项已通过。不要并行运行多个占用同一组全局热键的实例。

本次验收边界是 Windows 桌面正常输入链路。尚未实测目标游戏、1000 Hz/完整延迟、水平滚轮、拔插和真实进程崩溃恢复。F12 已实现且热键注册/模拟生命周期有覆盖，但本次两段真人采集均由定时结束，不能记为真人 F12 验收。

诊断缓冲区有固定上限；填满后转写继续运行，证据标记截断，分析器拒绝将该长会话判为完整通过。正常转写不因诊断缓冲区写满而停止。

本轮结束时未保持任何真实鼠标接管实例运行，用户可以通过启动入口自行启用。建议将最终范围与上述验收事实同步到 `.agent-context/`；本轮没有修改该目录。
