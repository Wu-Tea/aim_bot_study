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

已有 native 构建目录时，`scripts/verify/mouse_native.ps1` 构建正式 runtime 并运行共享/鼠标门禁；首次构建的 SDK 参数见 `tools/build_native_vision.ps1`。Python 配套检查为 `python -m unittest tests.test_startup_scripts tests.mouse.test_mouse_diagnostics`。这些命令默认不接管实体输入，`-DesktopProbe` 则会执行桌面注入检查。

最新验证：Release 构建成功，原生 34/34、启动/日志 Python 30/30 通过。原始日志、二进制、构建输出和带哈希的 RED/GREEN 包保留在本机忽略的 `runs/` 中；仓库提交源夹具、配置模板和可复现命令，文档中的本地证据链接不保证新 clone 存在。正式运行已有 215371 tick 完整日志用于控制内部诊断，尚无目标点修正版本的匹配游戏 A/B 和手感验收。

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

## 历史：2026-04-30 Python additive 路径

以下保留旧 Python 插件、遥测与注入实现的说明。其中“current”、默认参数及启动脚本描述均指 2026-04-30 的历史版本，**不是当前 mouse_start.bat 的启动行为，也不是当前产品目标**。旧路径保留物理输入并追加软件修正，不能作为唯一最终输出的实现或验收。

## Goal

The native mouse path keeps the physical mouse active and injects extra correction deltas on top of it.

It is designed for:

- additive AI mouse correction
- optional pulse auto-fire
- simple recoil pull-down

It is narrower than the gamepad path, but it is a real current implementation, not a placeholder.

## Main Files

- `controllers/mouse_controller.py`
- `controllers/mouse/state.py`
- `controllers/mouse/plugin.py`
- `controllers/mouse/ai_aim.py`
- `controllers/mouse/auto_fire.py`
- `controllers/mouse/recoil_compensation.py`

## Runtime Flow

1. `main.py --controller-mode mouse` creates `MouseController`.
2. Vision sends:
   - `update(dx, dy, target=ControllerTarget | None)`
   - `set_auto_fire(bool)`
   - `clear_target()` for transient aiming frames with no target
   - `reset()`
3. `MouseController` listens to physical mouse movement through `pynput`.
4. The host builds a `MouseFrame`.
5. The host starts from an empty `MouseOutput`.
6. Mouse plugins add movement and click behavior.
7. The final output is injected through `win32api.mouse_event(...)`.

## Important Boundary

The mouse host still does not choose targets on its own, but `AIAimPlugin`
does consume `ControllerTarget` metadata from vision:

- `target_source`
  - `observed` can start a fresh aggressive acquire or direct stabilize
  - `reconstructed` can also start fresh acquire/stabilize when vision has
    rebuilt the same target strongly enough
  - `predicted` never starts a fresh acquire; it only keeps stabilize alive for
    the same committed target family
- `body_box`
  - used to decide whether a reconstructed or predicted target is still the same
    target family instead of a silent switch

So the boundary is:

- vision remains responsible for selection/tracking
- mouse plugins decide whether the current target metadata is eligible for
  aggressive acquire, near-target stabilize, or manual fallback

## Data Structures

`controllers/mouse/state.py` defines:

- `MouseFrame`
  - immutable snapshot of manual mouse delta, aiming state, vision delta, and auto-fire request
- `MouseOutput`
  - mutable output buffer with:
    - `move_dx`
    - `move_dy`
    - `left_click`
    - `auto_fire_active`

Unlike gamepad output, `MouseOutput` starts at zero. The physical mouse has already moved the cursor on its own.

## Current Plugin Chain

The default plugin chain created by `MouseController` is:

1. `AIAimPlugin`
2. `AutoFirePlugin`
3. `RecoilCompensationPlugin`

## Current AI Aim Behavior

`AIAimPlugin` now uses a four-stage model instead of the retired
entry/hold/bridge path.

Current modes:

- `acquire_far`
  - the main long-range ADS pull phase
  - uses the strongest per-frame movement for `observed` and `reconstructed`
    targets outside the midrange band
- `acquire_mid`
  - takes over once the error has been reduced into the midrange band
  - still pulls decisively, but with less per-frame movement than `acquire_far`
    so the cursor does not fly past the target as often on strafe scenarios
- `reacquire`
  - only arms after a transient no-target gap while the same target family is
    still inside the short continuity window
  - gives a short, stronger-than-mid burst to reconnect to the same fight
    without turning every normal stabilize exit into a burst
- `motion lead`
  - `acquire_far`, `acquire_mid`, and `reacquire` can all add a small same-
    target motion lead based on successive aim-point movement
  - this is bounded and only applies when the current target still matches the
    last seen target family, so it helps strafe/diagonal scenarios without
    blindly chasing target switches
- `chase hold`
  - when the same target is still moving outward along the current error vector
    fast enough, the plugin can keep `acquire_far` active instead of dropping to
    `acquire_mid` or `stabilize` too early
  - this is mainly for strafe/diagonal cases where the midrange cap would
    otherwise fail to catch up before the target decelerates
- `stabilize`
  - activates once the crosshair is already near the target
  - activates later than before, so mouse assist spends more time pulling the
    crosshair in before switching to fine correction
  - uses lower per-frame movement and hysteresis so close-range correction does
    not chatter between help/no-help on every frame
  - scales down inside the inner release band instead of hard-zeroing, so tiny
    near-center corrections can still accumulate instead of pausing completely
  - if the same target starts pulling away on-screen and error grows sharply,
    it immediately falls back to `acquire_mid` instead of idling in a weak
    stabilize
  - can continue briefly through `predicted` targets, but only for the same
    target family and only inside the grace window
- `manual`
  - emits no AI movement
  - transient no-target aiming frames fall back here without wiping the current
    stabilize continuity context

Current config loader support for `[mouse.ai_aim]` is:

- `acquire_radius_px`
- `mid_acquire_enter_px`
- `mid_acquire_exit_px`
- `stabilize_enter_px`
- `stabilize_exit_px`
- `inner_release_band_px`
- `stabilize_reacquire_growth_px`
- `stabilize_reacquire_motion_px`
- `acquire_gain`
- `mid_acquire_gain`
- `reacquire_gain`
- `stabilize_gain`
- `predicted_stabilize_gain`
- `acquire_max_move_px`
- `mid_acquire_max_move_px`
- `reacquire_max_move_px`
- `stabilize_max_move_px`
- `predicted_stabilize_max_move_px`
- `acquire_lead_seconds`
- `mid_acquire_lead_seconds`
- `reacquire_lead_seconds`
- `acquire_lead_max_px`
- `same_target_grace_ms`
- `reacquire_radius_px`
- `reacquire_window_ms`
- `chase_hold_projection_px_per_sec`
- `chase_hold_min_radius_px`
- `breakaway_speed_px`

## AutoFire

`AutoFirePlugin` uses a pulse cycle rather than a continuous held click.

Current defaults:

- `aim_only = True`
- `hold_seconds = 0.120`
- `release_seconds = 0.030`

The host also forces a clean click edge by sending `LEFTUP` before a new `LEFTDOWN`.

## Recoil Compensation

`RecoilCompensationPlugin` adds downward mouse delta when `auto_fire_active` is true.

Current default:

- `amount_px = 0.80`

Unlike the gamepad path, mouse recoil and mouse auto-fire are not currently exposed through the shared config loader.

## Host Implementation Notes

Important host details in `MouseController`:

- right-click controls ADS aiming state
- left-click can also start a mouse aim session for click-to-snap scenarios
- transient aiming frames with no target use `clear_target()`
  - clears target deltas/metadata without resetting plugins
- releasing right-click still triggers full `reset()` when left-click is not holding an aim session
- synthetic movement is suppressed before manual arbitration so injected movement does not loop back in as fake manual input
- physical right-button polling fail-closes missed ADS releases and releases synthetic left-click holds
- mouse telemetry can record button state, target age, AI phase, output/move cadence, backend, and injection errors
- the loop runs at roughly `1000 Hz` through `time.sleep(0.001)`

This path is intentionally additive. It does not try to replace or intercept the physical mouse device.

## Startup

`scripts\launch\mouse_start.bat` currently:

- enables `VISION_PERF_LOG=1`
- defaults `VISION_BACKEND=native`
- defaults `VISION_CAPTURE_FPS=140`
- defaults `VISION_QUIT_KEY=Q`
- defaults `MOUSE_INJECTION_BACKEND=sendinput`
- launches `main.py --controller-mode mouse --vision-backend native --perf-log`

`scripts\launch\debug\mouse_native_debug.bat` currently:

- enables the same native defaults as `scripts\launch\mouse_start.bat`
- enables `MOUSE_TELEMETRY=1`
- probes mouse injection before launching unless `MOUSE_PROBE_INPUT=0`
- writes a run-specific CSV under `artifacts\mouse_telemetry\`
- analyzes that exact CSV with `tools\analyze_mouse_telemetry.py --assert-healthy`
- launches `main.py --controller-mode mouse --vision-backend native --vision-debug --vision-debug-save --perf-log`

Neither script provides the gamepad startup prompts.

For live diagnosis of no-effect or stepped pulling, see `MOUSE_TELEMETRY_DEBUGGING.md`.

## Relationship To `kbm_to_gamepad`

`mouse` and `kbm_to_gamepad` are not the same thing.

- `mouse`
  - outputs native mouse events
  - keeps the physical mouse semantics
- `kbm_to_gamepad`
  - outputs a virtual Xbox 360 controller
  - translates mouse movement into right-stick motion

So `mouse` is the native-output path, while `kbm_to_gamepad` is the virtual-gamepad bridge path.
