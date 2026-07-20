# Evidence-Driven Control Optimization 接入 Prompt

下面的 Prompt 可以直接复制到另一个项目的 Codex 任务中。首次使用建议完整复制；后续任务可以只使用文末的“继续执行 Prompt”。

使用前只需要替换 `PROJECT_INPUT` 区域。不确定的字段保留 `unknown`，不要猜。

---

## 首次接入主 Prompt

```text
你现在要为当前仓库接入一套 evidence-driven real-time control optimization 工作流。

这套工作流服务于实际程序性能、控制质量和健壮性。Benchmark、日志、counterfactual 和学习机制都只是测量与验证手段，不是最终产品目标。

==================== PROJECT_INPUT ====================

ENGAGEMENT_MODE: onboard
# 可选值：onboard / diagnose / optimize / learning

SYSTEM_NAME: unknown
CONTROLLED_OBJECT: unknown

USER_REPORTED_SYMPTOM:
- unknown

EXPECTED_BEHAVIOR:
- unknown

KNOWN_REPRODUCTION:
- unknown

RUNTIME_ENTRY:
- unknown

AVAILABLE_EVIDENCE:
- logs: unknown
- videos_or_traces: unknown
- benchmarks: unknown
- historical_results: unknown

KNOWN_CONSTRAINTS:
- 不要增加未获授权的外部服务、额外感知负载或生产依赖。
- 不要创建持久化学习、live exploration 或设备/武器数据库，除非后续证据明确要求并得到批准。
- 不要用总分提高代替实际控制质量验证。

AUTHORIZED_SCOPE:
- 允许只读检查仓库、配置、日志、构建入口、git 历史和测试。
- 允许创建接入文档、证据清单、benchmark 设计和验证计划。
- 除非用户明确要求实现或修复，否则第一阶段不要修改生产控制策略。

METHOD_REFERENCE:
- 如果仓库中已复制方法文档，请填写相对路径。
- 如果没有，按本 Prompt 内的约束执行，并建议后续加入项目内方法文档。
- path: unknown

=======================================================

请使用当前项目的语言与现有文档习惯工作。开始时先说明：

1. 这个系统是什么；
2. 谁或什么使用它；
3. 当前控制闭环大致做什么；
4. 用户报告的问题是什么；
5. 第一阶段会检查哪些区域；
6. 成功接入后会留下哪些可复用产物。

然后严格按以下流程工作。

## A. Context first

1. 检查仓库是否有 AGENTS.md、.agent-context、handoff、session log、decision records、README、architecture、benchmark 和近期 acceptance 文档。
2. 若存在项目上下文机制，先读取 handoff，再读与当前问题最相关的 session/decision；不要从 README 或 git log 盲目重建已记录历史。
3. 检查工作区和分支状态。不要覆盖或清理用户已有修改。
4. 建立“应先读文件”清单，并解释每个文件对当前问题的作用。

## B. Freeze runtime identity

尽可能确认并记录：

- 实际启动入口与 executable/build identity；
- git revision 与 dirty state；
- effective configuration 与 fingerprint；
- model、engine、schema 或协议版本；
- controller/update cadence 与 observation/feedback cadence；
- benchmark seed、scenario hash、duration 与 cohort；
- 环境响应参数、latency、slowdown 或其他闭环条件；
- logging/debug/perf flags。

若其中任何一项未知，不要阻止所有工作，但必须将相关 artifact 标记为：

- binary-unproven；
- configuration-unproven；
- semantic-unproven；或
- environment-unproven。

身份不完整的数字只能生成假设，不能用于宣称提升或回归。

## C. Map the control chain

用当前项目实际组件名画出最小控制链，至少区分：

observation/evidence
  -> identity/lifecycle/state owner
  -> controller proposals
  -> human/automatic ownership or fusion
  -> smoothing/brake/clamp/validation
  -> delivered output
  -> delayed environment response

同时列出：

- 每个状态的唯一 owner；
- 所有 hold、brake、gate、timer、fallback 和 output validation；
- 同一输入被解释几次；
- 同一模式切换被几个组件决定；
- benchmark-only 和 production-only 的行为差异。

不要只统计代码行数。优先统计 stateful policy、重复 owner、重复 timer 和无 active consumer 的配置项。

## D. Build a claim table

将当前所有重要陈述分为：

1. user-confirmed：用户体感、视频或明确陈述；
2. repository-evidence：源码、测试、日志、提交或带身份的 artifact；
3. inferred：合理但尚未被 matched A/B 证明的因果解释。

输出表格：

| Claim | Evidence class | Direct evidence | Confidence | Missing proof |

尤其检查这些常见误判：

- 摇杆/传感器漂移被当成用户输入；
- proposal 被当成 delivered output；
- 旧 binary 被当成当前源码；
- benchmark 参数覆盖被当成 live config；
- 某个指标恒为 0 被当成没有缺陷；
- hindsight 最优被当成 runtime 可用策略；
- 用户输入错误时仍被无条件完整保留；
- AI 判断错误时通过更强 gain 放大。

## E. Extract one minimum defect fixture

从真实失败中提取一个最小、时间结构完整的 fixture。它应尽量保留：

- 目标/控制对象的速度、加速度、停止、反向或模式变化；
- 遮挡、dropout、延迟、错误观察或 identity churn；
- 用户输入的 reaction delay、弱输入、错误方向、旧方向惯性、越过后反向或 deliberate escape；
- observation cadence 与 controller cadence 的差异；
- 最终环境响应。

优先制作配对 cohort：

1. pure：相同环境轨迹，没有典型人工错误；
2. mixed：完全相同环境轨迹，加入 deterministic human mistakes。

解释 fixture 与 production 在 lifecycle、input consumption、config reader、cadence、fusion 和 output delivery 上如何保持语义一致。

## F. Define metric semantics before implementation

定义加分型 outcome metrics 与独立 diagnostics。至少考虑：

- acquisition/settle time；
- centered or on-target time；
- integrated error 与 P50/P95/max error；
- crossing、post-cross excursion 和 post-cross area；
- stall residence、exit/re-entry 和 reversal burden；
- false interruption、false stop 和 unexpected mode；
- output delta、jerk、sign flips 和 saturation；
- useful manual/automatic input suppression；
- deliberate manual escape preservation；
- local regret 与 future burden。

说明每个指标的单位、触发条件、归一化分母和“数值变好代表什么”。

若一个核心指标在所有候选中恒定或恒为零，先修复统计语义，不要继续以它作为 acceptance gate。

## G. Decide whether this is tuning or policy work

只有同时满足以下条件才建议调参：

- 目标、mode、owner 和方向正确；
- 新逻辑确实在 fixture 中触发；
- 参数变化产生单调且可解释的输出变化；
- 参数未被 clamp、cap 或下游逻辑饱和；
- pure/mixed 和多个环境中趋势一致。

以下情况优先重新设计 policy：

- pure 改善但 mixed 退步；
- 用户输入被吞或双重衰减；
- 一个 gate 改善某模式却伤害另一模式 continuity；
- 参数多个取值结果完全相同；
- 局部误差下降但 future burden 上升；
- 需要不断增加例外 gate 才能覆盖新场景。

## H. Produce the onboarding artifacts

在不覆盖项目已有文档规范的前提下，建议创建或更新：

docs/control-optimization/START_HERE.md
docs/control-optimization/EVIDENCE_MAP.md
docs/control-optimization/METRIC_CONTRACT.md
docs/control-optimization/BASELINE_MANIFEST.md
docs/control-optimization/REJECTED_EXPERIMENTS.md

如果当前阶段没有足够证据，文件可以先是结构化 draft，但必须明确 unknown、inferred 和下一份所需证据。

不要在第一阶段伪造 benchmark 成绩，不要把计划写成已实现状态。

## I. First-turn deliverable

首次接入这一轮结束时，请交付：

1. 当前系统与问题的简明说明；
2. runtime identity 表和未知项；
3. 控制链与 owner/gate/timer 审计；
4. claim table；
5. 一个最小 defect fixture 设计；
6. metric contract 草案；
7. tuning vs policy 判断；
8. 推荐的最小后续改动，包含要替换/删除的旧逻辑；
9. 验证矩阵和停止条件；
10. 新增或更新文档的链接。

如果用户只要求诊断，不要擅自实现生产修复。
如果用户明确要求实现，则在上述接入产物完成后，先写实施计划和 focused failing test，再修改代码。

## J. Hard boundaries

- 不要把 benchmark 做成本身的产品目标。
- 不要用满分制总分隐藏 acquisition、tracking、smoothness 和 anti-intervention 的差异。
- 不要为同一决定增加第二个 owner。
- 不要在已有融合器、shaper 或 lifecycle 后再叠一个无法解释的 gate。
- 不要使用 hindsight 信息生成生产动作。
- 不要让 learner 直接控制输出；先 journal、offline oracle 和 shadow evaluation。
- 不要默认持久化学习状态。
- 不要删除原始日志、历史 artifact 或 rejected experiment；需要清理时先 dry-run 并获得授权。
- 不要在没有 runtime identity 的情况下宣布优化有效。
```

---

## 常用模式替换

### 只做诊断

将：

```text
ENGAGEMENT_MODE: onboard
```

替换为：

```text
ENGAGEMENT_MODE: diagnose
第一阶段只确定原因和证据缺口，不修改生产行为。
```

### 已有 benchmark，准备优化

替换为：

```text
ENGAGEMENT_MODE: optimize
先审计当前 benchmark 是否覆盖真实失败、是否与 production 语义一致，
再冻结 baseline 和 acceptance gates。不要先选择参数。
```

### 准备加入学习机制

替换为：

```text
ENGAGEMENT_MODE: learning
先证明 analytical policy 已稳定，并依次设计 decision/outcome journal、
offline counterfactual oracle、shadow estimator 和 bounded adjustment。
第一版必须 memory-only、无 live exploration，并可完全回退到 analytical policy。
```

---

## 继续执行 Prompt

首次接入完成后，后续任务可以使用这份短 Prompt：

```text
继续当前 evidence-driven control optimization 工作。

先读取项目的 control-optimization START_HERE、EVIDENCE_MAP、METRIC_CONTRACT、
BASELINE_MANIFEST、最新 acceptance/rejected experiment 和 .agent-context handoff。

本轮目标：<填写一个具体目标>

开始前确认当前 executable、revision、dirty state、effective config fingerprint、
benchmark schema/seed/cadence 与上一基线是否一致。

只处理一个可验证的 defect 或一个明确 owner。先说明：
1. 本轮假设；
2. 触发该缺陷的 fixture；
3. 要改变的唯一 owner；
4. 要删除或替换的旧逻辑；
5. primary metrics、anti-intervention metrics 和 future-burden guardrails；
6. focused test、fixed-seed A/B、runtime smoke 和 hand-feel 验收方式。

若身份或语义不一致，先修复证据链，不要继续调参。
若结果只提高总分、只改善 pure cohort、或增加 future burden，则拒绝 candidate 并记录原因。
完成后更新 acceptance/rejection 记录和项目 handoff。
```

---

## 与方法文档的关系

- 快速了解入口：[REALTIME_CONTROL_OPTIMIZATION_START_HERE.md](../REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)
- 完整方法论：[EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md](../../project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md)
- 本仓库案例：[AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md](../../project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md)

复制到其他仓库时，至少保留本 Prompt 和快速入口。完整方法论可以复制、以内部文档链接引用，或在目标项目中提炼成自己的版本；案例仅用于理解方法，不应把项目专属参数带入新系统。
