# ADS / BodyLock 控制链跳变修复验收

日期：2026-08-01（Asia/Hong_Kong）
状态：**Task 1–4 实机验收通过；后续双提案版本已部署并通过合成验收；最新实机日志存在轻微 ADS 过冲/欠跟残余**

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

## 四种现场问题的后续落地

在 Task 1–4 基线之后，四段现场视频又暴露了 firing position、same-target
reacquire、Coasting 施力寿命和强同向 manual/AI 叠加四个问题。当前源码与
`DE31...` runtime 已包含：

1. Fresh firing position 与 velocity innovation 分权：新鲜位置立即生效，
   `fire_innovation_limit_px` 只限制速度残差，避免位置已经穿过中心但 plan
   仍留在旧侧。
2. 同一 target 的 `Reacquiring` 不再被 fuser 硬切到 manual-only；不同 target、
   NoTarget、Manual 模式和 full escape 的既有边界不变。
3. `hold_ms` 继续负责 identity；Coasting actuation 使用独立的
   `12.5 ms` grace 和 `65 ms` release，目标缩回掩体后不再把旧方向施力维持到
   整个 identity hold 结束。
4. manual 与 AI 都作为绝对摇杆提案进入 `VectorIntentFuser`。强同向 ADS 和
   近距 BodyLock 采用 AI-priority 仲裁：AI 完整保留，manual 按 2.4 灵敏度下的
   情境尺度归一化后仅保留 20% 平行 headroom；切向、反向纠偏、full escape 和
   远距 BodyLock 保持原有所有权。

这不是“从尊重人手改成忽略人手”，而是把两个输入交给同一个最终所有权层计算，
避免 `manual + AI` 被当成两股可无限相加的力。

## 用户实机验收

用户在覆盖当前 launcher runtime 后进行了真实游戏测试，并确认：

- 最开始少数几次定位仍可能有偏差或轻微抖动，随后几乎都能直接定位到人物；
- 静止目标、玩家主视角移动时，BodyLock 仍能保持跟随；
- 移动跟枪时不再出现此前非常频繁的抖动；
- 此前的 ADS 突然拉飞和 BodyLock 突然跳远已不再构成当前可用性问题。

因此 Task 1–4 从 synthetic/matched acceptance 提升为 **live-feel accepted**。

后续 `DE31...` 双提案版本也已由用户启动并完成约 25.5 分钟 bot 测试。用户报告
玩久后仍偶有 ADS 拉过头或欠一点；该残余已经完成只读日志诊断，但尚未被宣布
修复或 live-feel accepted。

## `learn` 现象的边界

用户观察到会话开始的少数定位较差、随后明显变准。代码事实与解释边界如下：

- `runtime.control_learning.mode = rollout_shadow` 只评估和记录 shadow telemetry，
  不直接改写生产控制输出；
- `NativeGamepadController` 内的会话态 `AimResponseEstimator` 会根据已发送摇杆量与
  随后观察到的屏幕响应估计响应尺度和置信度，并把反馈交给
  `TargetCoordinator`；它确实位于实时控制链中；
- 因而“后续定位更准”与响应估计器及 tracker/body geometry 状态逐步建立相符，
  但这是代码和现象一致的 **AI 推断**，尚未由单独的 live telemetry A/B 证明。

最新长局日志没有显示误差或响应幅度随时间单调漂移：正常 ADS handoff 的结束
误差与会话时间 Spearman `rho=0.070`，响应幅度代理为 `rho=-0.054`。晚局问题
更集中在移动目标接近 `220 ms` ADS ceiling 时的交接与旧方向速度前馈。生产
`AimResponseEstimator` 的 scale/confidence 当前没有被直接记录，因此只能判定
“学习不是主要原因”，不能声称完全排除其幅度贡献。

详细证据见
[ADS 长局轻微过冲/欠跟日志诊断](ADS_LONG_SESSION_DIAGNOSIS_20260801.md)。
当前不需要启用持久化学习、在线探索或让 shadow rollout 获得控制权。

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
- 双提案最终矩阵相对严格旧加法基线：
  - Recover 静止 ADS / BodyLock tracking `+8.4% / +19.0%`；
  - Recover 移动 ADS / BodyLock tracking `+8.6% / +16.1%`；
  - 平均误差分别降低 `33.6% / 48.4% / 18.3% / 25.7%`；
  - pure 静止/移动 × ADS/BodyLock 控制单元精确零差异。
- 20% contextual layer 相对第一阶段 AI-priority candidate 的大多数
  tracking/error 变化约在 `±1%`；它负责恢复合适的人手参与度，而不是第二次
  大幅“刷分”。

详细证据：

- [Task 1–3 final verification](../../artifacts/benchmarks/control-chain-jump-fix-20260801/codex-final-ab/VERIFICATION.md)
- [Task 4 ownership/admission verification](../../artifacts/benchmarks/control-chain-jump-fix-20260801/task4-ownership-admission/VERIFICATION.md)
- [2.4 灵敏度 manual/AI 双提案验收](../../artifacts/benchmarks/sensitivity-manual-mix-20260801/CONTEXTUAL_DUAL_PROPOSAL_VERIFICATION.md)
- [最新长局 ADS 日志诊断](ADS_LONG_SESSION_DIAGNOSIS_20260801.md)
- [实施计划](../superpowers/plans/2026-08-01-ads-bodylock-jump-stability-repair.md)

## 当前 runtime 与回退

| 项目 | 身份 |
|---|---|
| 当前安装/验证 runtime | SHA-256 `DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590` |
| 当前 launcher 路径 | `native/vision_native/build/Release/cod_native_runtime.exe` |
| 归档 candidate | `artifacts/runtime-candidates/20260801-contextual-dual-proposal-headroom20-DE31FF53/cod_native_runtime.exe` |
| 上一 runtime 备份 | `artifacts/runtime-backups/20260801-pre-contextual-dual-proposal-0E5E9A3B/cod_native_runtime.exe` |
| 上一 runtime SHA-256 | `0E5E9A3BBDFB98BBE564E6C72704A0E5F400A6A0C09B24C7E33E1DD412105053` |
| 最新 manifest config hash | `db80e13a191df21905218dd53d590c299b91b8527b9ceabc2d9692dacdf79df8` |
| TensorRT engine SHA-256 | `45FC56274FF3BBC659E534C3B7833065B0483EF8022AC5D7657CD6DA7DBDEB21` |
| capture / tensor / model | `640x512 -> 480x384` / `models/best_480x384.engine` |
| 最新实机 session manifest | `runs/native_perf/sessions/20260801T131000Z_6544_1/session.json`，SHA-256 `3497A244E9689ABBFB6E35D970A9EFDFE28741E2097F24D10A252FE69CC0524A` |

最新 session manifest 仍保留 `state=active` 和旧 `git_commit=5d9f6d3...`，
且不记录 executable hash。它的 config/engine/capture 身份有效，但不能单独证明
二进制源码身份；runtime 关联仍依赖 21:04 构建、21:10 启动且中间没有再次覆盖
的安装链。

## 后续边界

- 当前 dual-proposal 参数保持不动；最新长局残余先按 ADS handoff/motion 问题
  验证，不用继续调全局强度。
- 新问题先记录 runtime/config/engine/source identity，再对齐视频与 telemetry。
- 不恢复重复可靠度 gate、第二输出 brake、全局 observation clamp 或 ADS re-arm。
- 下一轮若实施修复，先补物理 ADS epoch elapsed、当前 target segment elapsed、
  handoff reason、position/motion contribution 和生产 response scale/confidence
  telemetry，再建立 late-target 与 center-cross fixture。
- 任何 handoff 修复都必须保持“一次物理 LT 只有一次强 ADS snap”；不能通过重置
  ADS epoch 掩盖欠跟。
