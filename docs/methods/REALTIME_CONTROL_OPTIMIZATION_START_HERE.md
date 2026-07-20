# 实时控制优化：跨项目使用入口

这是一份给其他项目使用的短入口。它不要求项目采用本仓库的 ADS、BodyLock、Vision 或游戏手柄结构；它只要求目标系统是一个闭环控制系统，并且存在“输出效果、用户输入、环境反馈和时间状态互相影响”的问题。

完整方法论见 [Evidence-Driven Real-Time Control Optimization](../project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md)。本项目的完整案例见 [Aim Control Optimization History](../project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md)。

## 什么时候从这里开始

满足任意一项即可使用：

- 用户反馈“弱、黏、抖、过冲、抢控制、吞输入、错误中断”；
- benchmark 分数提高，但实机体验变差；
- 控制逻辑积累了多个 hold、brake、gate、timer 或 fallback；
- 人和自动控制同时输入，无法判断某一刻谁更合理；
- 参数 sweep 没有区分力，或者只在实验室场景有效；
- 当前局部动作有效，但可能增加后续修正负担；
- 想加入在线学习，却还没有安全边界和验收方法。

不适合直接套用的情况：纯离线预测、没有反馈闭环的一次性转换、只有确定性公式错误的简单 bug。此类任务先用普通单元测试和系统调试即可。

## 三种使用深度

### Level 1：缺陷定位

适合先回答“问题到底在哪一层”。不改策略，只建立：

1. runtime identity；
2. 一条真实失败证据链；
3. 一个最小 defect fixture；
4. proposal、ownership、delivery 的分层记录。

预期产物：`EVIDENCE_MAP.md` 和一个可重复失败的测试。

### Level 2：策略优化

适合已有复现，需要比较不同控制方案。增加：

1. metric contract；
2. pure/mixed paired cohorts；
3. fixed-seed A/B；
4. counterfactual candidates；
5. local regret 与 future burden；
6. live smoke 和体感验收。

预期产物：baseline manifest、candidate artifact、comparator 和 acceptance/rejection report。

### Level 3：学习机制

仅在 analytical policy 已稳定后使用。顺序必须是：

```text
decision/outcome journal
  -> offline counterfactual oracle
  -> shadow estimator
  -> bounded score adjustment
  -> memory-only live validation
  -> persistence decision
```

不要直接让新 learner 控制生产输出，不要在真实用户环境主动探索，不要默认持久化跨设备或跨配置状态。

## 开始前需要准备什么

不必一次收集完所有材料。第一轮最少提供以下内容：

```markdown
# Control Optimization Intake

## System
- 项目/模块：
- 控制对象：
- 控制频率：
- 观察/反馈频率：
- 主要运行入口：

## Symptom
- 用户看到或感受到什么：
- 最容易复现的条件：
- 正确行为应该是什么：
- 当前行为造成什么负担：

## Inputs and Outputs
- 人工输入：
- 自动输入：
- 环境/传感器输入：
- 最终交付输出：

## Runtime Identity
- revision：
- dirty state：
- executable/build：
- effective config/fingerprint：
- model/schema：
- logging/debug flags：

## Existing Evidence
- 日志：
- 视频/轨迹：
- benchmark：
- 已尝试和已拒绝的方案：
```

如果 runtime identity 不完整，仍可开始定位，但所有数字只能标记为线索，不能写成正式回归或提升。

## 给 Codex 的复制式启动提示词

将下面内容复制到目标项目的任务中，并附上 intake、日志或录像：

```text
请按 evidence-driven real-time control optimization 方法处理这个问题。

目标不是制作 benchmark 本身，而是提高实际控制性能与健壮性。
先同步项目上下文并确认真实 runtime identity，不要直接调参或加 gate。

第一轮请只完成：
1. 解释当前控制链中 observation、state/owner、proposal、fusion 和 delivered output 的关系；
2. 把现有陈述分成 user-confirmed、repository-evidence、inferred；
3. 从真实失败提取一个最小 defect fixture，并说明它与生产语义如何保持一致；
4. 定义有区分力的 outcome metrics 和 anti-intervention metrics；
5. 指出重复 owner、重复 timer/gate、benchmark semantic drift 和 config provenance 风险；
6. 给出最小修改方向、要替换/删除的旧逻辑和验证矩阵。

不要用 hindsight 信息设计 runtime 决策；不要把总分提高等同于实战提升；
不要创建武器/设备数据表、额外感知流程、持久化学习或 live exploration，除非证据明确要求并得到批准。
```

## 第一轮固定工作流

### 1. 冻结身份

记录 executable、revision、dirty state、effective config、model/schema、seed、cadence、环境参数和 debug flags。

### 2. 画出控制链

最少区分：

```text
observation -> lifecycle/owner -> controller proposal
            -> arbitration/fusion -> shaping/brake/clamp
            -> delivered output -> delayed response
```

### 3. 建立 claim table

| Claim | Evidence type | Direct evidence | Missing proof |
| --- | --- | --- | --- |
| 示例：系统吞了用户输入 | user-confirmed | 视频中需要二次拉杆 | 缺 manual proposal 与 delivered output |

### 4. 先做 defect fixture

Fixture 必须保留造成失败的最小时间结构，例如：反向前减速、短遮挡、多目标切换、错误人工输入、反馈延迟或模式切换。不要只保留一个静态误差值。

### 5. 先定义指标语义

指标至少覆盖：任务效果、错误干预、平滑度、状态中断、局部修正和未来负担。若某指标所有候选恒为 0，应先修指标，不要据此宣布安全。

### 6. 只改变一个 owner

Candidate 必须说明：

- 哪个组件拥有新决定；
- 删除或替代了哪个旧判断；
- 为什么不是新叠一层 gate；
- 低可靠、non-finite、target change 或人工 escape 时如何 fallback。

### 7. 验收顺序

```text
focused test
  -> paired pure/mixed fixture
  -> fixed-seed matrix
  -> determinism
  -> runtime smoke
  -> real hand-feel validation
  -> durable decision
```

## 推荐目录结构

目标项目不需要复制本仓库历史。建议只建立：

```text
docs/control-optimization/
  START_HERE.md
  EVIDENCE_MAP.md
  METRIC_CONTRACT.md
  BASELINE_MANIFEST.md
  ACCEPTANCE_<date>.md
  REJECTED_EXPERIMENTS.md

artifacts/control-optimization/
  <revision>_<config-fingerprint>_<seed>.*
```

若项目已有 `.agent-context/`，handoff 只保存当前目标、已接受边界、下一证据和应先读的文档；详细实验移入 project docs 或 archive。

## 每次提交前的最小检查表

- [ ] 实际 binary、revision 和 config fingerprint 已确认。
- [ ] Benchmark 与 production 使用相同 lifecycle、cadence 和 config reader。
- [ ] Pure 和 mixed 输入分别报告。
- [ ] Proposal、ownership 和 delivered output 可以区分。
- [ ] 正向得分与 anti-intervention 指标同时报告。
- [ ] 局部收益没有用更高 future burden 换取。
- [ ] Deliberate manual escape 和低可靠 fallback 保留。
- [ ] 新逻辑替换了旧 owner，而不是增加重复 gate。
- [ ] Fixed-seed rerun 可复现。
- [ ] Runtime smoke 与实机体验均已记录。
- [ ] 失败候选和拒绝原因被保留。

## 什么时候应该停下来

- 无法确认运行的是哪个 executable/config/model；
- benchmark 和 production 状态机语义不同；
- 唯一提升来自更改评分定义或匿名场景参数；
- candidate 提高总分却破坏人工 escape、安全或平滑度；
- 需要新的外部权限、持久化、生产探索或额外感知负担；
- 用户反馈尚无法映射到任何可观察量，且没有足够证据建立 fixture。

停下来不等于放弃。此时应记录 blocker 和需要的下一份证据，而不是用一个新 gate 掩盖未知。

## 从文档升级为 Skill 的边界

当至少两个不同项目都能使用这份入口，并且它们重复产生相同的 evidence map、metric contract、baseline manifest 和 acceptance report 时，再创建正式 skill。

Skill 应只自动化可泛化流程：上下文同步、身份冻结、claim 分类、fixture/metric 审查、A/B 结构、verification 和 durable decision。项目专属参数、传感器语义、游戏 cue、设备数据和 acceptance 数值必须作为项目输入，不能写死在 skill 中。
