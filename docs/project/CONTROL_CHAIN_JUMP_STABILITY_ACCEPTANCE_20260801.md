# ADS / BodyLock 控制链跳变修复验收

日期：2026-08-01（Asia/Hong_Kong）
状态：**合成验收通过，实机验收通过，当前稳定基线**

## 这项修复解决什么

该控制链负责把 Vision/selector 选出的目标转换成 ADS 定位或 BodyLock 跟随请求，
再由 `AimDynamicsShaper` 和 `VectorIntentFuser` 生成最终虚拟手柄输出。此次修复针对：

- ADS 定位时偶发突然拉飞；
- ADS→BodyLock 首帧继承旧 ADS 饱和值；
- 短暂失去目标或换目标后 AI 输出全关/全开；
- BodyLock 在人物静止、玩家主视角移动或移动跟枪时发生远跳和高频抖动。

本轮没有通过降低 ADS/BodyLock 力度来换取表面平滑，也没有新增第二套 tracker、
位置状态或最终输出 brake。

## 已接受的控制链改动

1. `VectorIntentFuser` 不再用重复的 `reliability < 0.65` 硬门制造 AI 全关/全开；
   短暂 reacquire 和恢复继续经过原有二维输出连续性边界，强手动逃逸仍精确接管。
2. `AimDynamicsShaper` 感知 target/mode 交接。ADS→BodyLock 不再把旧 ADS 饱和状态
   带入首个 BodyLock tick，新目标也不继承旧目标的 AI 状态。
3. selector 明确发布“无选中目标”时，`TargetCoordinator` 不再从检测候选中任取一个
   目标，避免身份协议旁路。
4. 同一物理 LT epoch 内发生短 no-target gap 后，如果出现不同 target，
   `VectorIntentFuser` 先给新目标一个精确 manual/零 AI admission tick，再通过现有
   `0.08` 二维 slew 渐入。Fresh Vision 位置仍然 authoritative；同 track 的合法
   横移不被 tracker clamp。

## 用户实机验收

用户在覆盖当前 launcher runtime 后进行了真实游戏测试，并确认：

- 最开始少数几次定位仍可能有偏差或轻微抖动，随后几乎都能直接定位到人物；
- 静止目标、玩家主视角移动时，BodyLock 仍能保持跟随；
- 移动跟枪时不再出现此前非常频繁的抖动；
- 此前的 ADS 突然拉飞和 BodyLock 突然跳远已不再构成当前可用性问题。

因此 Task 1–4 从 synthetic/matched acceptance 提升为 **live-feel accepted**。

## `learn` 现象的边界

用户观察到会话开始的少数定位较差、随后明显变准。代码事实与解释边界如下：

- `runtime.control_learning.mode = rollout_shadow` 只评估和记录 shadow telemetry，
  不直接改写生产控制输出；
- `NativeGamepadController` 内的会话态 `AimResponseEstimator` 会根据已发送摇杆量与
  随后观察到的屏幕响应估计响应尺度和置信度，并把反馈交给
  `TargetCoordinator`；它确实位于实时控制链中；
- 因而“后续定位更准”与响应估计器及 tracker/body geometry 状态逐步建立相符，
  但这是代码和现象一致的 **AI 推断**，尚未由单独的 live telemetry A/B 证明。

当前不需要为该现象启用持久化学习、在线探索或让 shadow rollout 获得控制权。

## 合成与 matched 验收

- Release 注册测试：`34/34` 通过；focused fuser、shaper、controller、
  coordinator、BodyLock 测试均通过。
- `git diff --check`：退出码 `0`，只有既存换行符提示。
- Task 4 的 12-run matched A/B：tracking `+0.0343%`，overshoot
  `-0.0712%`，continued push `245 ms -> 245 ms`，direction discontinuities
  `59 -> 59`，false interruptions `9 -> 9`，false stops `0 -> 0`。
- 最差单 run tracking 变化 `-0.4877%`，在 `-2%` guardrail 内。
- production-chain fixture：defect count `0`，selected-track changes `1`，
  reacquired error `51 px`，useful-assist latency `1 ms`，最大输出 delta `0.064`。

详细证据：

- [Task 1–3 final verification](../../artifacts/benchmarks/control-chain-jump-fix-20260801/codex-final-ab/VERIFICATION.md)
- [Task 4 ownership/admission verification](../../artifacts/benchmarks/control-chain-jump-fix-20260801/task4-ownership-admission/VERIFICATION.md)
- [实施计划](../superpowers/plans/2026-08-01-ads-bodylock-jump-stability-repair.md)

## 当前 runtime 与回退

| 项目 | 身份 |
|---|---|
| 当前稳定 runtime | SHA-256 `B7F9F28B6A39E1AE58DB75C5F3A3A18DDF3D9692886245ABF2C5493FDC84140C` |
| 当前 launcher 路径 | `native/vision_native/build/Release/cod_native_runtime.exe` |
| 归档 candidate | `artifacts/runtime-candidates/20260801-task4-ownership-admission/cod_native_runtime-task4-B7F9F28B.exe` |
| 上一 runtime 备份 | `artifacts/runtime-backups/20260801-task4-before-overwrite/cod_native_runtime-pre-task4-3D9ED74C.exe` |
| 上一 runtime SHA-256 | `3D9ED74CBCD7007F9EEF3CACC6B17EB5763F7056047BE35AC37495BF1CEB7402` |
| 原始 config SHA-256 | `6CA2D349D13F45645BF93ADB7D7793BDC360D649A5553203DDD62BAD9A63E1CB` |
| effective config SHA-256 | `16FA226298A7957D98AC3635F417C1C74CC2AC6BE2CE7EEAFDF305CD15D48033` |
| TensorRT engine SHA-256 | `45FC56274FF3BBC659E534C3B7833065B0483EF8022AC5D7657CD6DA7DBDEB21` |
| relevant dirty source diff | `DB7D86F17AEB6B4D34FBC92B775473FA65E4FAC164D704FE97D179328DC55B5E` |
| 最新实机 session manifest | `runs/native_perf/sessions/20260801T043817Z_58840_1/session.json`，SHA-256 `8850D33C2B26371150E2B7C8F2FB1E7F0A689D4ACF3647D5E0CEB597675AA9EA` |

`--dump-effective-config` 已验证 `640x512 -> 480x384 @ 160 FPS`。最新实机
session manifest 的 config/engine 身份与上表一致；manifest 当前不记录 executable
hash，因此 runtime 关联还依赖“安装后未发生再次替换”的现场链路。

## 后续边界

- 冻结当前 runtime 和控制参数；没有新的可复现 live defect 时不继续调强度。
- 新问题先记录 runtime/config/engine/source identity，再对齐视频与 telemetry。
- 不恢复重复可靠度 gate、第二输出 brake、全局 observation clamp 或 ADS re-arm。
- 若再次出现“首次几次偏、随后变准”且需要定位原因，再做只读 response-confidence
  对齐；在此之前不把该现象升级为新的学习策略工作。
