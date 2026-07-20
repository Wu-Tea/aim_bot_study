# 瞄准控制优化记录：2026-07-16 至 2026-07-20

## Executive Summary

这段工作的核心并不是“把参数调强”，而是把一个由多层隐式叠加产生手感的控制器，改造成一个能解释、能复现、能逐步优化的实时控制系统。

7 月 16 日的 Refactor B 删除了重复的 target hold、brake、owner gate、BodyLock lifecycle、短计划和输出修正路径，确立了单一生产链：

```text
VisionObservationBatch + IntentState
    -> TargetCoordinator
    -> immutable TargetPlan
    -> ADS / BodyLock
    -> AimDynamicsShaper
    -> intent fusion
    -> AutoFire / Recoil / final output
```

这次重构的直接效果是状态更一致、切换更自然、抖动和相互打架更少；代价是旧系统中一部分“强度”和“阻尼”其实来自重复逻辑的偶然叠加，删除后 ADS 与 BodyLock 明显变弱。[用户确认] 用户实际体验为“更自然、不会乱跳毁手感”，但也明确感到力度、刹停和近目标抓力下降。

后续改进没有把旧 gate 堆回去，而是依次建立：真实左摇杆与错误右摇杆场景、持续 60 秒闭环 AimLab、目标相对 brake episode、counterfactual 局部/未来负担、在线响应模型和二维 causal vector fusion。由此恢复并改善了多项可测控制能力，同时保持单一 owner 与单一平滑路径。[仓库证据] 同 seed 的 Refactor B、response-model 和 brake acceptance 文档给出了可复现结果；vector fusion 已进入生产路径，但其最终 A/B 原始 artifact 未随仓库保存，因此本文不把对话中的“惊人提升”写成可审计的数值结论。

本轮最终还修复了几项容易让所有 benchmark 失真的运行契约：ADS snap 只在物理开镜 epoch 内消费一次；近目标在短遮挡下不会被远目标抢走；绿色友军 cue 硬过滤、黄色敌方 cue 只作辅助；生产 vision crop/engine 恢复为 `480x416`；AutoFire 保持 100 ms 周期、至少 30 ms 按压且不吞物理开火；并提供无控制台窗口的启动/停止入口。[仓库证据]

## Evidence Labels and Comparison Rules

本文使用三种证据标签：

- **[用户确认]**：来自实战录像、手感反馈或用户对历史行为的明确描述。
- **[仓库证据]**：来自提交、源码、测试、带 revision/config/seed 的 artifact 或 acceptance 文档。
- **[推断]**：由多项证据形成、但尚未被同条件 A/B 或实战日志单独证明的因果判断。

数字只能在以下身份一致时直接比较：运行二进制、git revision、dirty state、配置路径及 fingerprint、benchmark schema、scenario script hash、seed、cohort、vision/controller cadence、slowdown 和 camera response。缺少 `configuration` 的旧 artifact 仅是 **configuration-unproven** 历史样本，不得充当当前 live profile 的基线。

## Starting Failure Pattern

7 月 16 日前的问题不是单一增益不足，而是多个环节同时拥有控制权：selector、tracker/provider、ADS completion/carry brake、BodyLock lifecycle、短计划、dynamics 和 output validation 都可能持有自己的状态、计时器和衰减规则。

结果表现为：

- 同一时刻不同层对“目标还在不在”“该 ADS 还是 BodyLock”“用户是否接管”给出不同答案。
- 同一份输入先被一个 gate 削弱，又被后面的策略恢复或再次制动。
- 摇杆漂移被当成用户 ownership，导致未输入时 ADS 也变弱。
- Scope 边框或短遮挡使 vision 瞬时丢框，多层 hold 的释放时刻不同，形成卡顿、跳变或错误切换。
- BodyLock 在 focused fixture 中能够进入，在 production-style moving chase 中却记录到 0 帧，说明测试与生产生命周期语义已经分叉。
- 旧手感可能很强，但无法说明强度来自正确控制，还是来自重复输出与重复阻尼。

[仓库证据] 冻结基线 `e6c1f2f` 的四个 moving/slide/occlusion/jump chase 都记录 `BodyLock frames = 0`；同时专门 handoff fixture 又能进入 BodyLock。详情见 [Refactor B baseline](REFACTOR_B_BASELINE_20260716.md)。

## Phase 1 - Refactor B Removes Accidental Control Stacking

### Observation

[用户确认] 用户要求先清理重复流程，避免 BodyLock 再出现多个 brake/gate 叠加造成的粘滞、停顿和抖动，同时不建立武器数据库或额外视觉负担。

### Hypothesis

[推断] 当一个实时控制系统的“目标所有权、输入意图、模式切换、刹停和平滑”分别由多个有状态对象解释时，调参只会改变冲突发生的位置，不会消除冲突。

### Change

[仓库证据] `df8bdc4` 冻结基线；`ee7ac42` 至 `3406889` 依次落地统一 contract、单一 IntentFilter、内存内 ADS response learning、唯一 TargetCoordinator 生命周期、immutable TargetPlan、plan-driven ADS/BodyLock 和单一 AimDynamicsShaper，最后原子切换生产运行时。旧路径只保留给历史 fixture，不再链接进生产 executable。

关键边界：

- Vision 只发布观察证据，不持有目标或 AutoFire latch。
- TargetCoordinator 唯一负责 identity、hold/coast/reacquire、预测与 ADS/BodyLock 建议。
- IntentFilter 唯一解释 drift 与 deliberate input；raw sticks 仍保留给最终 mixer。
- ADS 是点获取控制；BodyLock 是轨迹跟随控制；ADS brake 不进入 BodyLock。
- AimDynamicsShaper 是唯一有状态 delivery envelope。
- Recoil 是最终独立 feed-forward，不参与 target ownership。

### Result

[仓库证据] 四个 production-style chase 的 BodyLock 从 0 帧恢复到 1997 帧，moving chase 最大过冲 `28.031 -> 5.615 px`，adversarial manual-fight `132 -> 2`，continuity fixture 无 jerk、无短 BodyLock run。与此同时 moving chase mean error `27.09 -> 31.44 px`、P95 `83.52 -> 95.36 px`，表明架构正确性提高但跟随力度有所损失。见 [Refactor B acceptance](REFACTOR_B_ACCEPTANCE_20260716.md)。

[用户确认] 体感上重构后更自然、更平滑，也更像用户自己在控制；但 ADS、BodyLock 力度与阻尼明显下降。这个现象后来被视为重要发现：旧系统有用的“抓力”不能简单等同于旧实现中的重复 gate。

### Lesson

删除重复逻辑可能让短期分数或手感退步，因为重复逻辑曾经提供了真实的力。正确做法不是因此恢复重复 owner，而是为丢失的控制功能找到一个明确、可测、单一归属的替代机制。

## Phase 2 - Recovering Feel Without Restoring the Gate Stack

### Observation

Refactor B 后 ADS 长距离获取偏弱，BodyLock 接近目标时抓力不足；单纯提高 BodyLock 上限又会增加用户对抗时间。左摇杆运动还会改变屏幕相对运动，固定 `left_x * constant` 无法适应不同 ADS 移速与目标同向/反向关系。[用户确认]

### Change

[仓库证据] `ac7ff12` 在不恢复旧 brake/gate 的前提下恢复强而平滑的响应：

- ADS 使用更强的水平/垂直 scale。
- BodyLock 的近目标反馈范围从 `tolerance_px` 推导，而不是错误地使用 activation box 的一半。
- closing velocity 使用一个有界 20 ms stopping lookahead，只允许减到零，不允许在过中心前反向。
- 过滤后的左摇杆只影响短期相对运动响应；漂移先由 IntentFilter 去除。
- 仍只有一个 AimDynamicsShaper。

### Result

[仓库证据] 选中方案相对 rewrite baseline 的四个 chase mean error 均改善，near-target assist 提高 28.9%，adversarial user-fight 从 2 降到 0；而复现 7 月 14 日强曲线虽明显更快，却产生 138 个 user-fight frames，因此被拒绝。见 [Legacy aim feel restoration](LEGACY_AIM_FEEL_RESTORE_20260716.md)。

### Configuration Audit

[仓库证据] 随后的配置审计发现 benchmark 曾经匿名覆盖场景参数，且旧 artifact 未记录实际配置。于是 profile-faithful 模式被设为默认，artifact 必须记录 effective configuration、seed 和 override list；stress fixture 若不能解释每个 override 就 fail closed。`range_px`、`activation_range_px`、`tolerance_px` 被明确为不同语义，废弃配置不再出现在公共模板。见 [Controller config audit](CONTROLLER_CONFIG_AUDIT_20260716.md)。

### Lesson

“加强控制”必须先说明加强的是 peak cap、error slope、arrival horizon、near-target feedback 还是 feed-forward。名称不等于运行语义；本项目后来证明 `strength_scale` 在 response-model 控制器中会饱和成 peak cap，`1.40/1.54/1.68` 在特定矩阵里行为相同。

## Phase 3 - Making Real Combat and Human Error Measurable

### Observation

[用户确认] 实战并非只有右摇杆：人物会用左摇杆左右移动来辅助修正和规避，右摇杆也会出现延迟、方向错误、越过目标后仍保持旧方向、短暂反向等典型错误。原 benchmark 没有这些输入，因此“AI 对/用户对”的讨论没有可验证答案。

### Benchmark Expansion

[仓库证据] 7 月 17 日先加入半身遮挡、X/Y 同时移动、vision jitter/dropout、left-strafe onset/reversal/release 和 classic erroneous manual input。场景被拆成：

1. 单纯实战运动/遮挡；
2. 同一实战轨迹叠加错误用户输入。

错误案例包括 stale direction、wrong X、wrong Y、wrong both、crossing inertia、mixed axes，以及更高抖动和更强错误输入的 practical/destructive stress。

### Per-Axis Experiment and Its Limit

[仓库证据] 早期 per-axis correction 在 wrong-X 等定向 fixture 上有效：ordinary human-error mean error 约改善 4.35%，practical stress mean/P95 约改善 5.0%/9.1%；但 normal combat 几乎不变，destructive stress 甚至轻微退步。见 [Axis stress A/B](../benchmarks/axis-stress-ab-seed1337.md)。

[用户确认] 实战仍有“黏糊糊、距离目标 10-20 px 拉不过去、跟抢谨慎”的感受。用户进一步指出，X/Y 分开并不等于全向纠正：错误方向可能是斜向、切向或径向，不能先按轴判断再只抵消一个分量。

[推断] per-axis 方案的根本问题不是“轴数不够”，而是一个 ownership 判断被执行了两次：先清除该轴的 intent confidence 影响 AI 计算，后又衰减 physical stick，而 AI 仍完整保留。这会把有限判断放大成用户被吞输入的手感。

### AutoFire Contract

[仓库证据] 同期恢复 AutoFire：100 Hz vision 与 1000 Hz controller 间不再把“没有新 frame”误当作 miss；稳定强目标可每 100 ms 启动一次 synthetic pulse，每次至少按住 30 ms；fresh processed miss、cue/weak target、ADS release 或 manual takeover 同 tick 撤销；物理 RB/RT 始终直通。见 [Native controller benchmarks](NATIVE_CONTROLLER_BENCHMARKS.md#autofire-pulse-cadence-and-physical-fire-ownership-2026-07-17)。

## Phase 4 - Sustained Tracking and Meaningful Brake Metrics

### Why the Old Score Was Insufficient

短 fixture 和满分制评分容易隐藏两类失败：获取一次后长期欠跟，以及控制器在目标周围频繁停错、反向或退出。旧 `overshoot = 0` 也可能只是统计条件从未触发，而不是没有过冲。

### Sustained AimLab Contract

[仓库证据] `fb125e9` 建立 60 秒闭环 benchmark：目标随机生成，约 1000 ms 跟随；250-330 ms 获取窗口失败后目标消失；目标为有半径的圆，越靠近中心得分越高；slowdown 从圆周外开始，基准 edge/center 为 `0.50/0.40`；评分采用无限加分制，分别统计 acquisition points、tracking points、acquired/settled targets、smoothness、over、undertrack、false interruption 和 false stop。

BodyLock cohort 必须在有效 ADS capture 后才开始计分，避免 ADS pre-roll 让 BodyLock 分数虚高。纯输入与 mixed human-error、ordinary 与 small target 分开报告。

### Response Model

[仓库证据] `55bbb10` 用一个在线 response-model solver 取代 ADS/BodyLock 各自的 force heuristic。它观察“已交付 stick -> 屏幕运动”，预测短期 residual error，再生成二维 bounded correction；状态只在内存中按 ADS epoch 保留，不记录武器名，ambiguous manual、coasting/stale、低可靠观察或坏 cadence 时停止学习。

固定三 seed 结果相对冻结基线：ADS acquisition points 在四个 ordinary/small × pure/mixed cohort 中提高 13.6%-56.0%，tracking 提高 27.3%-42.2%；BodyLock tracking 提高 0.9%-6.7%。mixed small 的 interruption 诊断仍有小幅退步，因此没有为它增加新 gate。见 [Response-model acceptance](RESPONSE_MODEL_AIM_CONTROL_ACCEPTANCE_20260718.md)。

### Brake Episodes

[仓库证据] 7 月 19 日将 inert severe-only overshoot 替换为目标相对 brake diagnostics：center crossing、post-cross amplitude/area、circle re-exit、10-20 px stall residence、correction reversal、settle state 和 ADS-to-BodyLock residual。

在三 seed、两 target size、pure/mixed、三 camera response 与三 slowdown 环境的完整矩阵中，`120 ms + BodyLock 0.52/0.58` 相对 `160 ms + 0.45/0.50`：

- ADS acquisition points `+25.0%`、tracking `+40.8%`、acquired targets `+26.4%`；post-cross area/target `-23.6%`、circle exits/target `-10.6%`、mean run P95 error `-17.2%`。
- BodyLock tracking `+8.4%`、settled targets `+5.2%`；post-cross area/target `-5.0%`、circle exits/target `-4.0%`；代价是 P95 output delta `+5.1%`、jerk `+1.9%`。

`100 ms` ADS 虽更快，但 worst post-cross excursion 变差，按 brake-first rule 被拒绝。这个 candidate 当时没有自动写入用户 live config，仍需实战确认。见 [Brake episode acceptance](BRAKE_EPISODE_BENCHMARK_ACCEPTANCE_20260719.md)。

## Phase 5 - Counterfactual Local/Global Optimization

### Problem Definition

[用户确认] 即使某一刻已经锁住目标，当前“向上”输入可能对正在跳跃的目标正确，但如果目标在随后变向、下落、被遮挡或出现新目标，这个选择可能增加后续修正负担。需要同时找局部最优和全局最优。

### Counterfactual Benchmark

[仓库证据] `803e05b` 至 `67c43ac` 将相同 deterministic target/manual trace 从分支点重放，替换有限的 manual/AI mix，并测量：

- 40/80/160 ms local regret；
- 500 ms future burden 与 settle delay；
- AI-helpful/manual-helpful input 被压制的时间；
- both harmful、wrong-way commitment 和 destructive stacking；
- causal oracle 与 hindsight oracle 的差距。

固定 revision `ebf3b45`、config fingerprint `16587694727024197693`、三 seed、12 个 60 秒 run 的 baseline 分析 455 个 episode；重复运行的 core rows、causal gap 和 future burden delta 全为零。hindsight 只量化上限，不可复制到 runtime。见 [Counterfactual acceptance](COUNTERFACTUAL_CONFLICT_BENCHMARK_ACCEPTANCE_20260719.md)。

### Lesson

单帧无法证明“用户对还是 AI 对”。可验证的问题应改写为：在当时可观察信息下，候选 mix 对 40/80/160 ms 误差、crossing、反向负担和 manual ownership 的预测成本是多少；随后再用 delayed outcome 检验这个预测。

## Phase 6 - Causal Vector Fusion and Target Inertia

### Change

[仓库证据] `VectorIntentFuser` 取代 per-axis arbiter，把 physical manual `M` 与 shaped AI `A` 作为完整二维向量，评估固定候选：existing mix、manual-supported、AI-supported、manual-only、AI-only、reduced mix。成本只使用分支时可见信息，覆盖 40/80/160 ms integrated error、terminal residual、continued push、output direction/magnitude change、reversal burden 和 manual ownership loss。

组件只拥有当前 manual/AI weight、previous fused output 和 target ID；weight 在一个 `weight_transition_ms` 内平滑变化，不再叠加第二个 final-stick low-pass。强 manual escape 保留 raw vector；target change、reacquiring、低可靠或低 response confidence 时不得新增 manual attenuation。

### Target Inertia

[用户确认] BodyLock 不应把所有过中心都视作错误，因为运动目标变向、跳跃到下落会在若干 vision tick 内表现出真实惯性。允许有界 overshoot，比在中心制造零输出 brake 更符合跟抢。

[仓库证据] `cf030ba` 将 target inertia 与 radial closing/away relationship 纳入 vector candidate arbitration，并扩充 reversed/continuing、crossing 和 integration tests；`3d20ba6` 随后启用 production vector fusion。ADS brake 仍只属于 ADS，BodyLock 不制造 zero-output brake。

### Result Boundary

[仓库证据] 源码、测试与 runtime wiring 证明 analytical vector fuser 已落地；提交序列还包含 benchmark experiment 与 comparator。[用户确认] 当时用户对汇报的提升幅度表示“非常惊人”，随后同意同步到 dev。

但最终 legacy/vector A/B JSON 没有作为受控 artifact 留在当前仓库，因而本文不复述无法独立核验的百分比。[推断] 这次效果可信的原因不是单个总分，而是实现同时满足了固定候选、causal input、manual escape、可靠性 fallback、同目标 ADS->BodyLock weight continuity 和 focused tests；仍应补一份当前 revision 的完整 acceptance artifact。

### Long-Term Learning Boundary

已记录但尚未实现的全局学习路线是：

1. G0：TargetCoordinator 边界的 bounded decision/outcome journal；
2. G1：500-1500 ms sequence counterfactual beam oracle；
3. G2：只在 shadow 中运行的 bounded tail-value estimator；
4. G3：最多占 analytical score 10-15% 的 coordinator score adjustment；
5. G4：先仅内存，持久化必须另行证明和批准。

这条路线不能覆盖 manual reject、target validity、lifecycle 或 smoothness guardrail，也不能在 live game 中主动探索。见 [Global aim policy learning plan](../superpowers/plans/2026-07-19-global-aim-policy-learning.md)。

## Phase 7 - Runtime Contract Corrections

这些修复看似零散，实际上决定 benchmark 和实战是否运行同一个系统。

### Target Selection and ADS Epoch

[仓库证据]

- `0309804`：复数目标且当前目标短遮挡时，selector 保持近目标 preference，避免远目标趁空窗抢准星。
- `821e255`：ADS snap 只在物理 LT 从 released -> pressed 建立的新 epoch 内触发；ADS 中途出现新目标不会重新获得强 snap。要主动切换时由用户重新开关镜。
- `5403abe`：绿色 cue 友军硬过滤；黄色 cue 是敌方辅助证据，不能单独获得强控制或 fire authority。
- `b0c5bec`：BodyLock 在不可靠 plan 且 AI 与 manual 对抗时平滑释放到 manual；benchmark ADS epoch 与生产语义对齐。

### Vision Identity

[仓库证据] `0c7a2f9` 将 native capture 默认和 engine fallback 恢复为 `crop_width=480`、`crop_height=416`，匹配 `body_union_manual_core_x2_neg_e6_480x416.engine`。此前意外回到 `640x512`，会让现有 engine、目标几何和 benchmark 的输入契约不一致。

### Runtime Operation

[仓库证据] `f4bcd4b` 提供双击启动/停止的 VBS 外壳与 PowerShell 核心脚本。启动器无控制台窗口，避免重复实例；停止器只终止由启动器记录且身份匹配的进程。该功能是后台运行便利性，不是隐藏进程，也不会规避任务管理器。

### Logging

[仓库证据] high-rate telemetry 默认关闭，debug session 使用 fresh manifest、完整 session 轮转和 whole-session cleanup。`--perf-log` 是显式 debug 行为；旧 session 通过 dry-run list/prune 管理，不在启动时任意删文件。热路径 perf logging 曾导致输入粘滞怀疑，因此 `d8557ad` 先默认关闭并留作待验证问题。

## Failed or Rejected Directions

### 1. 用 `left_x * 常数` 代替相对运动估计

不同 ADS response、目标与玩家同向/反向、目标自身加速度都会改变符号和比例。最终采用“已交付输入与观察位移”的短期内存学习，而非武器表。[用户确认 + 仓库证据]

### 2. 为小目标增加额外视觉流程

用户明确不希望增加视觉负担。当前方向是使用已有 box size/reliability 连续降低 authority；小目标识别本身不是控制器重构的理由。[用户确认]

### 3. 恢复旧 gate 堆叠来找回力度

旧强 profile 跟得更快，但 user-fight 明显增加。保留单 owner 和单 shaper，只恢复其有用的 response/near-target/stopping 功能。[仓库证据]

### 4. 粗糙 body-box/authority gate

它改善部分 wrong-target 数字，却让 slide/jump BodyLock frames、low-close、dropout 和 overshoot 退步，因此被拒绝。说明 ADS 强目标资格与 BodyLock continuity 不能共享一个粗 gate。[仓库证据]

### 5. 只做 X/Y 独立仲裁

它在定向错误 fixture 上有效，但不能表达径向/切向/斜向关系，还会把一次 ownership 判断变成两次衰减。最终被完整二维 vector fusion 替换。[仓库证据 + 用户确认]

### 6. 以旧 severe overshoot 为唯一刹停指标

所有候选长期为 0，指标没有区分力。替换为 crossing、post-cross area、circle exit、stall residence、reversal 和 settle residual。[仓库证据]

### 7. 只看实验室总分或满分制

总分可能被 ADS pre-roll、更多目标完成数或场景覆盖变化抬高。采用 additive outcome score，同时保留负向诊断与 per-target normalization。[用户确认 + 仓库证据]

### 8. 把 hindsight oracle 写入 runtime

hindsight 只能说明理论 headroom；生产候选只能用决策时可见的 causal evidence。[仓库证据]

## Current Architecture and Verified Baseline

截至 `dev` 上本记录前的运行架构：

```text
Vision candidates/evidence
  -> TargetSelector (intent-aware, green-friendly reject, yellow auxiliary cue)
  -> TargetCoordinator (single identity/lifecycle/plan owner; ADS epoch)
  -> TargetPlan (observed/coasting/reacquiring, motion, reliability, horizon)
  -> ADS response-model acquisition OR BodyLock trajectory follow
  -> AimDynamicsShaper (single delivery envelope)
  -> VectorIntentFuser (single 2-D manual/AI ownership decision)
  -> ADS Brake only while ADS acquisition owns the mode
  -> Recoil final feed-forward
  -> virtual gamepad output

TargetPlan.fire_authority
  -> AutoFireGate (100 ms period, >=30 ms pulse, physical fire passthrough)
```

当前可以明确称为“已验证”的是 contract、focused tests、固定 seed acceptance 和 runtime smoke；不应把用户本地未入库的 `config.toml` 当成仓库默认值。`config.native.example.toml` 是公共示例，历史 candidate 也只是候选，实战基线应额外记录 config fingerprint。

## Remaining Risks and Next Evidence

1. **缺失最终 vector-fusion acceptance artifact。** 重新用当前 revision、固定三 seed、legacy/vector 明确 identity 跑完整矩阵并保存 comparator 输出。
2. **全局多目标策略仍是 roadmap。** G0-G4 尚未落地，当前 fuser 只优化单目标内的短期 future burden。
3. **小目标/远目标 authority 尚未完成系统性验收。** 优先用已有 size/reliability，不增加 inference；需要 ordinary/small、遮挡、multi-target 与 manual intent 组合 fixture。
4. **实战配置 provenance。** 每次录像/日志必须能回答运行 binary、revision、config fingerprint、crop/engine、是否 perf-log。
5. **BodyLock 微调被吞的真实边界。** 继续用 radial/tangential、slowdown ring、10-20 px stall residence 和 manual escape episode，而不是再加轴 gate。
6. **学习机制。** 先 shadow、有限候选、bounded residual、无 live exploration、进程内存；没有跨 session 证据前不持久化。
7. **复杂度预算。** 新行为必须替换 owner 或扩展现有 contract；不得新增独立 hold/brake/timer 来快速修一个 fixture。

## Evidence Index

### Primary acceptance records

- [Refactor B baseline](REFACTOR_B_BASELINE_20260716.md)
- [Refactor B acceptance](REFACTOR_B_ACCEPTANCE_20260716.md)
- [Legacy aim feel restoration](LEGACY_AIM_FEEL_RESTORE_20260716.md)
- [Controller config audit](CONTROLLER_CONFIG_AUDIT_20260716.md)
- [ADS/BodyLock tuning sweep](AIM_TUNING_SWEEP_20260718.md)
- [Response-model acceptance](RESPONSE_MODEL_AIM_CONTROL_ACCEPTANCE_20260718.md)
- [Brake episode acceptance](BRAKE_EPISODE_BENCHMARK_ACCEPTANCE_20260719.md)
- [Counterfactual conflict acceptance](COUNTERFACTUAL_CONFLICT_BENCHMARK_ACCEPTANCE_20260719.md)
- [Sustained AimLab benchmark contract](../benchmarks/sustained-aimlab.md)
- [Axis stress A/B](../benchmarks/axis-stress-ab-seed1337.md)

### Architecture and roadmap

- [TargetCoordinator rewrite design](../superpowers/specs/2026-07-16-target-coordinator-rewrite-design.md)
- [Sustained AimLab design](../superpowers/specs/2026-07-18-sustained-aimlab-vector-control-design.md)
- [Causal vector fusion design](../superpowers/specs/2026-07-19-causal-vector-intent-fusion-design.md)
- [Global policy learning plan](../superpowers/plans/2026-07-19-global-aim-policy-learning.md)
- [ADS epoch and color cue contract](../superpowers/plans/2026-07-19-ads-epoch-and-color-cue-contract.md)

### Key commits

- Refactor B: `df8bdc4`, `ee7ac42`, `c27f6eb`, `3a2928f`, `3bd4d19`, `b35d071`, `b2829b2`, `3406889`
- Feel/config: `ac7ff12`, `a49a74b`, `e4cf50e`
- Human error/axis work: `977db87`, `60be9d8`, `26c8760`, `a92ad73`, `01ef8fc`
- AutoFire: `4b1db3a`, `2ba13f1`, `a4bc131`, `5dc6bbb`
- Sustained/response model: `fb125e9`, `55bbb10`, `f7ee02b`
- Brake/counterfactual: `d991170`, `67c43ac`
- Vector fusion: `d298e35`, `fdfd060`, `14dc5e9`, `5795a46`, `cf030ba`, `3d20ba6`
- Runtime contracts: `0309804`, `821e255`, `5403abe`, `b0c5bec`, `0c7a2f9`, `f4bcd4b`
