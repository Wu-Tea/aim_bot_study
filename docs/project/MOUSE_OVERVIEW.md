# Mouse Controller Overview

Last updated: 2026-09-08

## 当前路线与状态

Mouse 的当前目标是：**接管指定物理鼠标，由现有 controller 决定唯一最终位移，通过独立虚拟 HID 鼠标交付**。无辅助时原样转写；辅助拥有目标时可以覆盖手动量。接管期间移动、按钮和滚轮全部经虚拟鼠标输出，避免原物理信号绕过 controller 与最终输出叠加。

完整模块分工、研究结论和接入验收以 [Mouse 输入替代路线](MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md) 为入口。

## 使用与配置入口

从仓库根目录运行 `scripts/launch/mouse_start.bat`；`--check-config` 只读回配置，`--check-transport` 只检查设备。正常运行前松开鼠标按钮，Ctrl+Alt+F12 退出；Ctrl+Alt+F11 可选校准，右键状态决定腰射或 ADS。启动程序不会安装驱动，当前设备路径要求已有 Interception 和 FakerInput。其他物理鼠标需通过 `--mouse-hardware` 或 `--mouse-device` 明确选择。

本机 `config.toml` 属于忽略的用户配置；仓库保存完整默认值的 `config.native.example.toml`。新环境在文件不存在时复制模板，并配置本机模型/运行参数；已有文件直接合并需要的 `[mouse]` 项。修改后重启。

| 配置项（均在 `[mouse]`） | 默认值 | 含义 |
| --- | --- | --- |
| `dpi` / `sensitivity` / `fov` / `ads_multiplier` | 1200 / 5 / 104 / 1 | 实际鼠标与游戏参数；FOV 为 16:9 水平角度 |
| `speed` / `breakaway` | 2 / 4 | AI 速度倍率 / 快速手动脱离范围 |
| `bodylock_deadzone` | 0.5 | 手动抖动过滤；累计持续慢拖仍可接管 |
| `bodylock_range_px` | 180 | 按目标尺寸扩展的持续辅助基础范围 |
| `bodylock_accel_ms` / `bodylock_decel_ms` | 40 / 25 | 从零到最大轴速度 / 最大轴速度到零的时间 |
| `bodylock_point_tolerance_px` | 3 | 每轴目标点误差容差；与人物框和手动死区独立 |
| `recoil_enabled` / `recoil_counts_per_second` / `recoil_require_ads` | true / 30 / true | 开火期间固定向下压枪；默认要求 ADS |
| `log_enabled` / `log_directory` / `log_max_mb` | true / `runs/mouse` / 2048 | 异步逐 tick 日志；单次预算，不删除旧日志 |

建议阅读顺序：

1. [原生运行与校准](MOUSE_NATIVE_CONTROLLER_20260907.md)：控制链、启动参数、设备选择。
2. [日志、压枪和慢拖修正](MOUSE_DIAGNOSTICS_RECOIL_20260908.md)：日志分析方法与计数语义。
3. [BodyLock 范围和加减速](MOUSE_BODYLOCK_RANGE_CURVE_20260908.md)、[目标点容差](MOUSE_TARGET_POINT_CONTROL_20260908.md)：当前控制行为及回归证据。
4. [输入替代路线](MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md)、[桌面验收](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)：设备所有权与已验证范围。

已有 native 构建目录时，`scripts/verify/mouse_native.ps1` 构建正式 runtime 并运行共享/鼠标门禁；首次构建的 SDK 参数见 `tools/build_native_runtime.ps1`。Python 配套检查已退役；当前验证使用原生 CTest。这些命令默认不接管实体输入，`-DesktopProbe` 则会执行桌面注入检查。

历史 2026-09 检查：Release 构建及原生 34/34、当时的 Python 30/30 通过。当前 C++-only 全量检查见 CURRENT_STATE.md。原始日志、二进制、构建输出和带哈希的 RED/GREEN 包保留在本机忽略的 `runs/` 中；仓库提交源夹具、配置模板和可复现命令，文档中的本地证据链接不保证新 clone 存在。正式运行已有 215371 tick 完整日志用于控制内部诊断，尚无目标点修正版本的匹配游戏 A/B 和手感验收。

## 实现与证据边界

```text
物理鼠标 → Interception 全包捕获 → MouseRateAdapter
        → 现有 NativeGamepadController（ADS / BodyLock / final-T）
        → MouseActuatorAdapter + 按钮/滚轮合成
        → FakerInput 独立虚拟鼠标 → Windows / 目标程序
```

此链路已接入正式 C++ runtime。不同入口的验证范围如下：

| 入口 | 已有能力 | 尚缺 |
| --- | --- | --- |
| `scripts/launch/mouse_virtual_relay.bat` | 独立物理接管与虚拟转写；两次桌面记录共 16,487 源包、原设备泄漏 0；原样/零移动/反向、左右键、上下滚轮与正常退出恢复通过 | 正式 controller、游戏、1000 Hz/完整延迟及真实故障恢复 |
| `scripts/launch/mouse_start.bat` | C++ Vision + 共享 controller + 全包转写；默认 `virtual-hid`，独立监督与唯一 FakerInput 输出已落地；已读取正式运行内部日志 | 与独立接收端对齐的正式组合证明、新版游戏 A/B、高负载性能与真人恢复验收 |

设备研究与证据见[桌面验收结果](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)，当前控制器参数见[原生 Mouse Controller](MOUSE_NATIVE_CONTROLLER_20260907.md)。两个接管程序不能同时运行。

输入窗口、全虚拟按钮、AutoFire 所有权及独立进程恢复现已实现，控制器继续复用现有目标、ADS、BodyLock 和手动接管逻辑。构建与验证见[本次集成记录](../../runs/mouse_virtual_runtime_integration_20260908/README.md)。此路线不创建虚拟 gamepad。

FOV、DPI、游戏灵敏度和 ADS 倍率也从 `[mouse]` 中的 `fov`、`dpi`、`sensitivity`、`ads_multiplier` 读取，命令行可逐项覆盖。当前 `[mouse]` 控制配置默认 `speed=2.0`、`breakaway=4.0`、`bodylock_deadzone=0.5`。BodyLock 抑制小幅往返抖动，累计同向移动超过空间容差后恢复手动修正，避免持续慢拖被无限吞掉；设置 `bodylock_deadzone=0` 可关闭。

最新运行默认记录逐 tick JSONL，并提供固定向下压枪：默认 30 counts/秒，要求右键瞄准且实际开火。日志、压枪配置、分析命令与近距离慢拖 RED/GREEN 见[鼠标诊断与压枪](MOUSE_DIAGNOSTICS_RECOIL_20260908.md)。游戏内乱抖的其他原因仍需新日志确认。

后续 BodyLock 调整已加入独立鼠标基础范围 180 px、加速 40 ms、减速 25 ms，复用现有目标协调器和 `AimDynamicsShaper`。ADS 继续负责初次拉入，BodyLock 持续跟随；参数与验证范围见[BodyLock 范围与加减速](MOUSE_BODYLOCK_RANGE_CURVE_20260908.md)。

目标点控制现增加 `bodylock_point_tolerance_px=3.0`：每轴容差内停止细小位置/运动修正，容差外朝最终目标点纠偏，反向速度补偿不能吞掉全部位置请求。它与人物框大小和手动输入死区独立。真实日志的诊断边界、RED/GREEN 和新日志字段见[目标点纠偏与容差](MOUSE_TARGET_POINT_CONTROL_20260908.md)。

## 历史实现

旧 Python additive 鼠标链路及分析脚本已退役，历史说明从 Git 查看。当前原生链路和实际输入边界以上述代码为准。
