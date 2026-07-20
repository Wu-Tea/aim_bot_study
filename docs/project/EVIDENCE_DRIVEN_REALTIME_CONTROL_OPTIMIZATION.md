# Evidence-Driven Real-Time Control Optimization

跨项目首次使用请先读 [实时控制优化：跨项目使用入口](../methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)。该入口提供 intake 模板、可复制提示词、最小产物和停止条件；本文保留完整原理与设计边界。

## What This Method Solves

实时控制器的难点通常不是缺少一个更复杂的公式，而是缺少可信的因果链：用户说“黏”“弱”“乱跳”，日志看到几个轴值，benchmark 又给出一个更高总分，但三者可能运行了不同 binary、配置、输入语义或状态机。

本方法用于人机混合的闭环控制系统，例如瞄准辅助、相机跟随、机器人遥操作、车辆稳定、云台或指针辅助。它帮助开发者回答四个问题：

1. 用户感受到的失败究竟发生在哪个 owner、哪个状态和哪个输出阶段？
2. 测试是否真的复现该失败，而不是只模拟了一个方便的实验室代理？
3. 改动的收益来自正确策略，还是来自参数饱和、重复控制或评分漏洞？
4. 局部更优是否把更多修正负担推迟到了未来？

它不是一个新运行时模块，而是一套从证据到最小策略改动的研发流程。

## The Core Loop

```text
runtime identity
  -> evidence chain
  -> defect fixture
  -> metric contract
  -> counterfactual A/B
  -> minimal policy change
  -> fixed-seed gate
  -> live smoke
  -> hand-feel validation
  -> durable decision
```

每一步都有明确产物和停止条件。跳过任何一步，都可能得到“分数更高但实战更差”的优化。

## 1. Freeze Runtime Identity Before Explaining Behavior

### Required identity

每个可用于比较的 run 至少记录：

- executable 路径或 build identity；
- git revision 与 dirty state；
- effective config 和稳定 fingerprint；
- model/engine identity 与输入几何；
- benchmark/runtime schema version；
- seed、scenario script hash、duration 和 cohort；
- controller/observation cadence；
- 环境参数，如 camera response、slowdown、latency、dropout；
- debug/perf logging 是否启用。

如果录像实际运行的是旧目录 executable，或 engine 按 `480x416` 导出而 capture 是 `640x512`，后面的日志分析再精细也不能证明当前源码行为。

### Rule

身份不完整的 artifact 可以用于发现假设，不可用于宣称回归或提升。标记为 `configuration-unproven`、`binary-unproven` 或 `semantic-unproven`，不要删除原始数据，也不要把它混入正式 baseline。

## 2. Build an Evidence Chain, Not a Single Metric

一次完整缺陷证据链应连接：

```text
用户可见症状
  -> physical input / target observation
  -> owner and lifecycle decision
  -> controller proposal
  -> arbitration/fusion decision
  -> shaped/final delivered output
  -> delayed target response
```

建议为每个 case 写一张简表：

| Field | Question |
| --- | --- |
| Symptom | 用户具体看到了什么或做不到什么？ |
| Trigger | 遮挡、变向、开镜、目标切换、用户错误输入发生在何时？ |
| Observation | Vision/tracker 当时发布了什么，freshness/reliability 如何？ |
| Ownership | 谁认为自己拥有目标、模式和用户输入？理由是什么？ |
| Proposal | ADS/BodyLock/manual 分别想输出什么？ |
| Delivery | fusion、shaper、brake、recoil 后实际交付了什么？ |
| Response | 目标相对误差、crossing、settle 和后续负担如何变化？ |
| Confidence | 用户确认、仓库证据还是推断？ |

只记录 final stick 无法判断 AI、manual、brake 或 saturation 谁造成失败；只记录 proposal 又无法证明输出真的交付。

## 3. Turn the Symptom Into a Defect Fixture

### Start from real failure features

不要先问“现有 benchmark 能测什么”，先提取实战失败的最小结构：

- 目标运动：匀速、加速、急停、反向、跳跃顶点、下落、翻越；
- 可见性：半身遮挡、scope 边框遮挡、短 dropout、错误框、ID churn；
- 多目标：近/远、主目标遮挡、新目标出现、队友 cue；
- 用户输入：reaction delay、弱输入、错误轴、错误斜向、旧方向惯性、过冲后反向、manual escape；
- 系统差异：80/100 Hz observation 对 1000 Hz control、slowdown ring、不同 camera response。

### Use paired cohorts

至少保留两种同轨迹场景：

1. **pure combat**：只有目标、vision 和控制器；
2. **mixed human error**：完全相同 target/vision trace，加 deterministic human mistakes。

如果 pure 改善而 mixed 退步，通常不是强度不足，而是 ownership/fusion 问题。如果两者都不变，可能 fixture 没触发新策略，先查触发覆盖而不是继续调参。

### Stress has levels

把 stress 分成 ordinary、practical 和 destructive。Destructive fixture 用于检查 fail-safe，不应用来独自选择实战参数。一个策略能在极端错误输入下赢，却让普通微调变黏，仍然是失败。

## 4. Define Metric Semantics Before Running

### Outcome scores are additive

对持续任务使用加分制：更快 acquisition、更多 centered time、更低 center-weighted error 持续累积。不要用“满分 100”压扁差异，也不要把 acquisition 和 tracking 混成一个无法解释的总分。

### Diagnostics remain uncapped

至少单独保留：

- time to first acquisition / stable acquisition；
- acquisition points / tracking points；
- acquired / settled targets；
- P50/P95/max target-relative error；
- center crossings 与 maximum post-cross excursion；
- post-cross error area；
- circle exits / re-entries；
- 10-20 px stall residence 或其他 near-target dead zone；
- reversal count / reversal burden；
- false interruption、false stop、unexpected mode time；
- output delta、jerk、sign flip 和 saturation；
- manual-helpful / AI-helpful input suppression；
- deliberate escape preservation。

### Normalize opportunity-sensitive metrics

更快策略会完成更多目标，因此 raw post-cross area、exit count 或 interruption count 可能自然变大。按 acquired/settled target、有效跟随秒数或 episode 数归一化，再同时报告 raw count，避免把更多机会误判成更差。

### A zero metric may be inert

当所有候选的 overshoot 都是 0，先验证统计定义：触发阈值是否太高？是否只统计“严重越过圆心”而实际问题是 crossing 后继续推、退出减速环或在 10-20 px 停滞？没有区分力的指标应保留历史兼容性，但不能作为选择信号。

## 5. Preserve Semantic Parity

Benchmark 必须使用与 production 相同的：

- target lifecycle；
- ADS epoch 与 ADS->BodyLock handoff；
- observation consumption semantics；
- intent filtering；
- controller proposal；
- fusion、brake 和 output shaping 顺序；
- config reader 和默认值。

典型错误是 BodyLock cohort 混入 ADS pre-roll，导致 baseline 分数虚高；或 benchmark 每 1 ms 重复消费一个 100 Hz fresh observation，而 production 只消费一次 sequence。测试代码“调用了同一个类”不代表语义相同。

## 6. Separate Proposal, Ownership and Delivery

人机混合控制至少要区分三层：

1. **Proposal**：manual、AI、recoil 各自希望输出什么；
2. **Ownership/fusion**：在当前 evidence 下保留或削弱哪些贡献；
3. **Delivery**：slew、jerk、brake、clamp 后实际交付什么。

同一个判断不能在两层重复生效。例如先清空 manual intent 让 AI 变强，再降低 physical manual retention，会把一次“用户可能错了”的判断变成双倍 AI ownership。

推荐的 telemetry 字段是 component proposal、selected candidate/weights、reason、shaped output、final delivered output 和 delayed response；normal runtime 只记录低频事件，per-tick trace 只在显式 debug session 开启。

## 7. Use Counterfactual Replay for “Who Was Right?”

单个时间点没有“用户正确/AI 正确”的真值。建立有限候选集合，在相同 trace 的同一分支点重放：

```text
existing mix
manual-supported
AI-supported
manual-only
AI-only
reduced mix
```

生产策略只能用分支时可见的 causal evidence 预测 40/80/160 ms 成本。hindsight oracle 可以看到未来，但只用于量化 headroom。

建议成本包含：

- integrated predicted error；
- terminal residual；
- center crossing 后 continued push；
- output magnitude/direction change；
- reversal burden；
- deliberate manual ownership loss。

Counterfactual 的目的不是在 live runtime 中搜索所有分支，而是找到哪类 conflict 有稳定 headroom，再为一个最小分析策略提供证据。

## 8. Measure Local Regret and Future Burden Separately

一个 80 ms 内更接近目标的输入，可能在 500 ms 后造成更大的反向修正。至少报告：

- local regret at 40/80/160 ms；
- future burden at 160/500/1000 ms；
- settle delay；
- next acquisition/handoff burden；
- wrong-way commitment；
- unnecessary target switch/loss。

单目标 fuser 负责 within-target future burden；跨目标 opportunity cost 应留给 TargetCoordinator。不要把 input mixing 和 multi-target selection 合并成一个巨型状态机。

## 9. Architecture Rule: Authority Before Strength

当用户说“弱”或“黏”，先确定：

- 目标是否正确？
- 当前 mode 是否正确？
- AI 是否有权输出？
- manual 是否被误判为 drift 或 harmful？
- 输出是否在后续 shaper/brake 被二次削弱？
- 参数是否已经饱和？

只有 ownership、lifecycle 与 delivery 都正确后，才调 strength、arrival horizon、tolerance 或 slowdown response。

### One owner per decision

建议预算：

- 一个 target lifecycle owner；
- 一个 intent interpretation；
- 一个 immutable short plan；
- 一个 ADS controller；
- 一个 BodyLock controller；
- 一个 manual/AI fuser；
- 一个 stateful output shaper；
- 一个 AutoFire owner。

新增 boolean gate 前必须回答：它扩展哪个 owner 的显式状态？为什么现有 contract 无法表达？哪个 fixture 证明必要？删除哪个旧分支？

### Complexity budget

不要只数源代码行。更重要的复杂度指标是：

- stateful policy object 数；
- 同一概念的 timer/hold 数；
- 同一输入被解释的次数；
- 可配置但没有 active consumer 的 key 数；
- mode transition 的 owner 数；
- benchmark-only 与 production-only 的语义分叉数。

一个 6000 行控制文件可能值得拆分，但把它拆成十个互相持状态的小类并不会降低行为复杂度。

## 10. Parameter Tuning Versus Policy Redesign

适合调参的情况：

- 策略确实在 fixture 中触发；
- mode、owner 与 direction 正确；
- 改变参数产生单调且可解释的输出变化；
- candidate 在多 seed、多环境中方向一致；
- 没有参数饱和或被下游 cap 覆盖。

需要改策略的情况：

- 成绩几乎不变，因为新逻辑没有被触发；
- pure 好、mixed 差；
- 只在某一轴或某一合成场景有效；
- 用户输入被吞，但 AI proposal 看起来正确；
- 同一参数多个值结果完全相同；
- 一个 gate 改善 ADS 却伤害 BodyLock continuity；
- 当前局部成本下降而 future burden 上升。

参数 sweep 应先验证“参数实际控制什么”。不要根据名字假设 scale 是线性增益，也不要把 activation radius、error slope 和 settle tolerance 混在一起。

## 11. Learn Response Without Building a Data Table

当系统响应随武器、ADS、速度或环境变化时，可以在 clean windows 内在线估计：

```text
known delivered control + expected target motion
    -> observed reticle-relative displacement
    -> bounded response estimate + confidence
```

规则：

- 只在 stable identity、fresh reliable observation、足够 excitation、无强 manual correction 时更新；
- 遮挡、目标加速度、ID churn、bad cadence 时 freeze；
- 新 ADS epoch 降低 confidence，但可保留 conservative warm scale；
- confidence 低时回退到 error feedback；
- 第一版只在内存，不按武器名持久化。

这比录入武器表更通用，也避免用错误目标运动污染 response learning。

## 12. Fixed-Seed Gates and Live Validation

### Fixed-seed gate

每个 candidate 至少通过：

- focused unit/contract tests；
- pure/mixed、ordinary/small；
- 多 seed；
- cadence/slowdown/response mismatch；
- safety/fallback/manual escape；
- determinism rerun；
- comparator identity check。

### Live smoke

确认实际 executable 能加载当前 config 与 engine、正确创建 input/output backend、运行有限 ticks 并正常关闭。脚本或后台 launcher 也要验证 duplicate start 和 identity-safe stop。

### Hand-feel validation

实战不是低质量证据，而是闭环模型没有覆盖的高维验收。记录清楚：

- 用户主动操作占比是否更自然；
- 是否出现近目标粘滞、抢准星、错误中断；
- ADS 一次定位是否仍够快；
- BodyLock 是否容许必要惯性而不持续错推；
- 手感问题能否映射回已有 metric，不能则先扩 benchmark。

不要为了通过实战反馈直接加 gate；先把反馈转成 defect fixture。

## 13. Durable Decision Record

一次优化结束时保存：

- symptom 与 evidence label；
- baseline/candidate identity；
- hypothesis；
- change 与删除的旧逻辑；
- primary gains 和明确 tradeoff；
- rejected variants 及原因；
- test/benchmark/live smoke；
- remaining limitation；
- 下一次需要什么证据才重开决策。

这使后续 agent 不会重新发明已被拒绝的 gate，也不会把旧 artifact 当成当前真相。

## 14. Logging and Fresh Evidence

Debug log 可以大，但必须可维护：

- 每次运行一个 session directory 和 manifest；
- `fresh_session` 指向当前会话，不靠“猜最新文件”；
- active/closed/abandoned 状态明确；
- rotation 只发生在 session 内；
- cleanup 先 dry-run，只删除完整 closed session；
- fresh、pinned 和最近 N 个 session 受保护；
- normal mode 只写事件和 bounded sampling；
- hot controller tick 不做格式化、磁盘扫描或锁竞争。

日志关闭时仍出现卡顿，说明需要检查 runtime/scheduler/input/output，而不是默认把问题归咎于文件大小。`--perf-log` 必须被视为运行身份的一部分。

## 15. Common Failure Modes

| Failure | Why it misleads | Correction |
| --- | --- | --- |
| 只调 strength | ownership 错误会被放大 | 先验证 target/mode/authority |
| 只看总分 | ADS pre-roll 或完成更多目标可抬高分数 | 分 cohort、归一化、保留负向指标 |
| overshoot 永远 0 | 指标没有触发 | 改为 crossing/post-cross/exit/stall |
| benchmark 匿名改 config | 结果无法代表 live profile | profile-faithful + fingerprint |
| 每轴独立 gate | 无法表达完整方向且可能双重衰减 | 单次二维 fusion |
| 看到用户反向就永远保留 | 用户也会错 | causal candidate cost + manual escape |
| 看到用户错就吞输入 | 低置信预测会破坏手感 | reliability fallback + bounded transition |
| 恢复旧逻辑找手感 | 把偶然叠加重新带回 | 提取有用功能到单 owner |
| 用 hindsight 选 runtime action | 使用未来信息 | hindsight 只量化 headroom |
| 学习直接控制输出 | 难以界定安全边界 | 先 shadow residual，限制为 analytical cost 的小比例 |

## 16. Skill-Ready Package Proposal

未来可以将本方法封装为 `evidence-driven-control-optimization` skill。此处只定义包，不在本任务中安装。

### Trigger conditions

- 用户报告实时控制“弱、黏、抖、过冲、抢目标、吞输入或错误中断”；
- benchmark 与实战体感矛盾；
- 多个 controller/gate/timer 可能重复拥有同一决策；
- 需要设计闭环 benchmark、counterfactual 或长期学习验收；
- 参数 sweep 无区分力或结果不稳定。

### Mandatory workflow

1. 同步项目 context，读取 handoff、最近 session 和相关 decisions。
2. 冻结 runtime identity 与 clean baseline。
3. 建立 claim table：user-confirmed / repository-evidence / inferred。
4. 从真实失败提取 defect fixture，先写 metric semantics。
5. 验证 production/benchmark semantic parity。
6. 运行 counterfactual 或最小 A/B，定位 proposal/ownership/delivery 层。
7. 设计只改变一个 owner 的最小策略，列出要删除的旧路径。
8. TDD、fixed-seed matrix、determinism、runtime smoke。
9. 报告 gains、tradeoffs、rejected variants 和 hand-feel gap。
10. 生成 durable decision 与 context SyncSet 建议。

### Required references

- 项目运行契约与 pipeline architecture；
- benchmark schema/metric definitions；
- effective config provenance；
- 当前 handoff/session log；
- 至少一个 acceptance artifact 和一个 rejected experiment。

### Expected artifacts

- evidence map；
- frozen baseline manifest；
- defect fixture spec；
- metric contract；
- A/B comparator output；
- acceptance/rejection report；
- architecture complexity delta；
- context update proposal。

### Stop conditions

- runtime/config/model identity 无法确定；
- benchmark 与 production lifecycle 语义不一致；
- candidate 只提高总分却违反 safety/manual escape/smoothness gate；
- 连续验证失败且无法区分环境与代码；
- 需要新增持久化、live exploration 或外部数据权限；
- 用户手感反馈尚不能映射到任何可观察量，且没有足够证据设计 fixture。

### Project-specific details that must not be generalized automatically

- COD19 slowdown 的具体 edge/center 倍率；
- `480x416` crop 与特定 TensorRT engine；
- ADS/BodyLock 参数名、AutoFire RB/RT 语义；
- green/yellow cue 的游戏含义；
- 1000 Hz controller、80/100 Hz vision 的具体 cadence；
- 当前候选 mix 和 acceptance 百分比；
- 任何用户本地配置、视频路径或个人目录。

可泛化的是证据链、语义一致性、局部/未来负担、单 owner、counterfactual、bounded learning 和 hand-feel 闭环，不是本项目的数值。

## Related Project Case Study

本方法的项目内案例见 [Aim control optimization history](AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md)。它展示了第一次架构重构为何同时“更自然但更弱”，以及后续如何通过更真实的 benchmark 与二维 causal fusion 恢复控制能力，而没有把重复 gate 堆回生产路径。
