# 鼠标来源监听工具与本机实验

日期：2026-09-08。范围：独立监听和来源证据分类；未修改 ADS、BodyLock、转写算法或游戏设置，未安装驱动。

## 已实现

双击 `scripts/launch/mouse_input_monitor.bat`，默认只监听 30 秒，然后退出。无需 Interception DLL 或驱动。每次创建独立报告目录，不覆盖旧记录。

```powershell
# 构建独立监听器；此命令不发送鼠标事件
& .\scripts\verify\build_mouse_input_monitor.ps1

# 被动记录，可在期间手动移动鼠标或运行待测应用
& .\artifacts\mouse_input_monitor\build\Release\mouse_input_monitor.exe --seconds 30

# 显式模拟输入实验：20 次有标记 + 20 次无标记的正负 1 count 移动，无点击
& .\artifacts\mouse_input_monitor\build\Release\mouse_input_monitor.exe --self-test
```

可用 `--out 新目录` 指定存放位置，`--tag 0xHEX` 增加一个已知标记。目录已存在时拒绝写入。

后续实验新增 `--no-legacy`，仅用于验证注册应用本身的 legacy 消息设置；不会屏蔽其他应用收到的鼠标输入。跨进程可行性脚本为 `scripts/verify/mouse_user_mode_feasibility.py`，完整结果见 `docs/project/MOUSE_SOFTWARE_FEASIBILITY_20260908.md`。下文 SHA-256 对应首次来源识别实验版本，新版 SHA-256 记录在后续实验报告中。

## 如何判断来源

监听器同时订阅 `WH_MOUSE_LL` 和 `WM_INPUT`，不拦截、不修改、不转发事件。Hook 始终调用下一环节。事件先进入预分配内存，结束监听后才查询设备名称、写文件。缓冲区上限 262144 个事件；溢出计数不为零或读取失败时返回失败，不把不完整采样作为通过。

来源证据和应用标记是独立维度：

| 证据 | 报告值 | 能说明什么 |
| --- | --- | --- |
| Hook 的注入标志 | `os_injected` | Windows 将该事件标记为注入；不提供发送进程 PID |
| Hook 没有注入标志 | `unconfirmed` | 没有该项证据；不等于已验证物理来源 |
| Raw Input 有设备句柄 | `device_associated` | 可映射到 Windows 输入设备；设备可能是实体，也可能是虚拟设备 |
| Raw Input 没有设备句柄 | `unconfirmed` | 不能仅凭空句柄判定模拟输入，真实精密触控板也可能如此 |
| 额外信息匹配已知标记 | `tag_match=1` | 与项目位移、AutoFire、实验或指定标记匹配；标记不是来源认证 |

输出包括 `events.csv`（逐事件）、`devices.csv`（设备映射及各设备收到的包数，包括闲置设备）、`summary.json`（计数、读取错误及自测结果）。设备名称查询发生在采样之后，已断开的设备可能查询失败，此时保留句柄和查询失败状态。设备句柄只用于本次记录，不跨会话当作稳定身份。

Hook 的 X/Y 是屏幕位置，Raw Input 的 X/Y 是相对 counts 或绝对原始坐标，CSV 用 `coordinate_kind` 区分。两条事件流可能观察到同一次输入，不能相加计算“重复输入”，也不按相近时间戳强行一一配对。普通监听退出成功只表示完成采样；零输入不证明实体来源分类、屏蔽或游戏接收通过。

## 本机实验结果

最终二进制：`artifacts/mouse_input_monitor/build/Release/mouse_input_monitor.exe`

SHA-256：`47875C4814F8342614135BA606A6D17C5EF6A552768009B1330227D915F88ADB`

模拟实验：`runs/mouse_input_monitor/20260908-provenance-final/summary.json`

| 项目 | 结果 |
| --- | --- |
| SendInput 提交带标记移动 | 20/20 |
| Hook 收到带标记、系统注入移动 | 20/20 |
| SendInput 提交无标记移动 | 20/20 |
| Hook 收到无标记、系统注入移动 | 20/20 |
| Raw Input 收到带标记移动 | 20，空设备句柄 |
| Raw Input 收到无标记移动 | 20，空设备句柄 |
| 丢事件 / 读取错误 | 0 / 0 |
| Windows 鼠标设备枚举 | 8 个；不能推导为 8 个实体鼠标 |

40 次发送在两条流上产生 80 条观察记录，不是发送 80 次。本机受控实验表明，无应用标记的模拟移动也会进入 Raw Input。因此仅以“没有本应用标记”判定原生鼠标不成立；这不能单独证明此前游戏叠加问题的根因。

分类单元测试 11 个案例通过，覆盖未标记注入、低完整性注入、仅有标记但没有注入标志、空设备和设备关联的不确定性。Mouse Link 的策略、无捕获生命周期、独立 watchdog 等 4 个 CTest 项目全部通过。原 Mouse Link 内部的 `native_motion/native_baseline` 更名为 `unmarked_motion/unmarked_baseline`，界面明确显示“来源未确认”；没有修改其接管或验收逻辑。

首次 15 秒纯监听及最终 3 秒纯监听都没有鼠标事件：`runs/mouse_input_monitor/87952-171838703/summary.json`、`runs/mouse_input_monitor/20260908-passive-final/summary.json`。因此实体鼠标输入、实体与模拟混合输入、驱动转写来源和目标游戏接收仍未实测。实体设备的确认需要手动移动与设备记录对应或独立设备侧证据，不能用 SendInput 模拟这项证据。

## 下一次链路实验的证据要求

先采样明确选定的实体鼠标和已知应用输出，保留输入设备、标记、系统注入标志三个维度。来源未确认事件单独保留。随后才比较过滤前后接收记录与发送台账；不能根据“桌面光标动了”“出现某设备句柄”或“没有注入标志”宣称游戏已接收替代输入。

## 官方依据

- [MSLLHOOKSTRUCT：注入标志与额外信息](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct)
- [RAWINPUTHEADER：设备句柄与精密触控板的空句柄例外](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputheader)
- [RAWMOUSE：原始坐标、标志与额外信息](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse)
