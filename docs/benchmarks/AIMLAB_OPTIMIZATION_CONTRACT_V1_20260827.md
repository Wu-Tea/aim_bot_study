# AimLab 优化与实战映射合同 V1

状态：已确认，2026-08-27 起作为 AimLab 优化的强制入口。

上游合同：

- `docs/project/AIM_CONTROL_PRODUCT_CONTRACT_V1_20260811.md`
- `docs/benchmarks/CLOSED_LOOP_GAMEPLAY_ACCEPTANCE_V1_20260811.md`
- `docs/benchmarks/sustained-aimlab.md`

## 1. 唯一目标

AimLab 最重要的目标不是制造一个更高的纯数字，而是逐步提高数字结果对真实游戏行为的解释力和预测力。

当前获取分、跟踪分和平滑奖励仍可保留为加分制。加分制本身没有问题，问题是允许候选用一种错误换取另一种分数：例如更快进入圆圈，却跟错身份、重复 ADS、顶住合法人工修正、跨代继续输出，或增加振荡。以后采用两层判断：

1. **约束层决定候选有没有资格。** 任一硬失败、受保护指标退化、关键协变量不匹配或相关场景缺失，都不能由分数补偿。
2. **得分层只给合格候选排序。** 获取、跟踪、误差和平滑指标只在约束层通过后用于判断哪个候选更好。

一句话规则：**约束决定能不能选，分数只决定合格方案里选哪个。**

## 2. 结果状态

每次 AimLab 工作必须给出下面五种状态之一，不能只写 `PASS`：

- `INVALID / NON-COMPARABLE`：身份、seed、场景、时长、plant、Vision、controller cadence、输入画像或脚本不匹配。
- `EXPLORATORY / MISSING COVERAGE`：数字可供研究，但某项相关产品约束没有可信 fixture 或 oracle。
- `FAILED CONSTRAINTS`：硬门禁或受保护的非回归预算失败；总分不再参与结论。
- `BENCHMARK-ELIGIBLE`：离线约束和可比性通过，可用分数排序；仍不等于实战通过。
- `LIVE-ACCEPTED`：在 `BENCHMARK-ELIGIBLE` 基础上通过 matched native/live A/B、画面结果和用户手感确认。

可执行程序无异常退出、指标为有限数值或打印 `PASS`，只能证明测试成功运行，不能自动获得后三种状态。

## 3. 不可换分的产品约束

### 3.1 目标身份与区域

- 任一时刻只能有一个当前权威目标身份；权限必须绑定 target ID、selector generation 和当前 lifecycle。
- 友方、尸体、过期目标、无效协议数据或跨代数据不能获得瞄准或合成开火权限。
- 没有明确换人意图时不得自动切换目标；换人必须由拥有该语义的 selector/lifecycle 路径完成。
- 控制目标是 selector 发布的人体允许区域 `R` 和当前期望点 `D`，不是无条件追检测框中心。
- 场景没有真实触发身份、权限和控制路径时，测试不得报告成功。

### 3.2 ADS

- 一次物理 LT 上升沿只授予一次 ADS snap token；同一次 LT 不能重复 snap。
- LT 后允许在有界窗口内稍等目标出现；当前约定等待上限约 220 ms。窗口内接纳目标可消费 token，窗口外后来出现的目标只能进入普通跟随，不能补做 snap。
- selector 接纳有效敌人后，ADS 使用已配置的完整权威；cue、clarity 或 reliability 不再二次缩放已接纳 ADS 的力度。
- 标称快速定位窗口约 135 ms；近目标可连续缩短。额外约 220 ms 只允许低机动武器或慢响应 plant 完成剩余定位，不能把超时伪装成成功。
- 只有新鲜、同目标的稳定到位或可信中心穿越才能完成 ADS。目标丢失、目标切换、LT 释放、明确退出或执行期限到期必须终止该 owner。
- ADS 完成后进入 BodyLock；不能在同一 LT 下重新制造 snap。
- 开火时向下人工修正、明确退出和人体内 `D` 修正必须得到正确处理，不能被 ADS 长时间顶住。

### 3.3 BodyLock

- BodyLock 是连续跟随，不是第二次 snap。它应保持准星位于 `R` 内并趋向当前 `D`。
- 输出需求同时包含位置误差修正和维持目标总运动所需的持续量；不能因为屏幕相对误差速度变小就把持续跟随需求错误降为零。
- 目标运动 observer 只使用直接、新鲜、同身份且 capture-aligned 的样本；target generation、replacement 或 lifecycle 边界必须重置。cue 不能训练 observer，recoil 不能被当作目标运动。
- manual 和 AI 是同一个目标优先解算的证据，不是两个独立力相加。合法同向人工输入不能被无故削弱，错误方向或过量输入可由同一最终 envelope 约束。
- 人体内 `D` 修正、明确退出、开火下拉和新鲜目标对陈旧反向人工工作的取消都必须保持语义连续。

### 3.4 Cue

- cue 只可在同一 target ID、generation 和 LT epoch 下临时延续，当前上限约 180 ms。
- cue 不能创建新目标、切换身份、重新训练目标运动、重新授予 ADS snap 或触发合成开火。
- cue 期间人工修正和退出仍然有效；到期或身份不一致必须有界释放，不得留下残余轴值。

### 3.5 Manual 与最终输出

- 每个 controller tick 只能产生一个最终右摇杆 `T`。manual 与 AI 是 proposal/evidence，禁止实现为两个物理输出直接相加。
- manual 的含义由生命周期解释：可能是选人、人体内 `D` 修正、退出/交接、开火下拉，或无目标时原样透传。
- 无目标、AI/Vision 故障或遥测故障不能吞掉原始手柄输入。
- 最终输出必须有限、有界，无卡键、非预期尖峰、跨目标残余或失效权限残余。

### 3.6 Controller、Vision 与 cadence

- 接受的生产路径在约 1000 Hz 外层 cadence 上按同一 tick 执行 physical/manual 采样、目标权威/lifecycle、AI solve、dynamics、AutoFire、recoil、最终 composition 和 ViGEm publication。
- benchmark 可把整条 controller cadence 作为明确的反事实轴，并继续支持不同 Vision cadence、Vision age 和灵敏度 plant；不得把请求的固定频率冒充日志中的实际频率。
- 2026-08-27 的独立 AI proposal 固定 250 Hz 实验已否决并回滚。它在权威 tick 中产生约 24.9% 的零 assist 请求，而旧 lockstep 日志为 0%，并且没有测得 whole-runtime 性能收益。
- 将来若重开独立 AI cadence，必须先建立新 RED：任一 target ID、selector generation、mode、LT/lifecycle 或 authority 变化都要在同一个外层 tick 重算；权威 proposal gap 必须为零，跨目标陈旧输出必须为零，并证明真实整机收益。未满足前不得恢复运行时开关或缓存 owner。

### 3.7 AutoFire 与 recoil

- 合成开火只能使用直接、新鲜、敌方、同身份、区域 ready、已 armed 且非 cue 的证据；这些条件并行判断，不能堆叠成多级等待。
- 物理开火始终按产品合同透传；合成开火失败不能阻塞瞄准或下一帧处理。
- recoil 是独立 feed-forward，只拥有 recoil contribution；它不能拥有目标、身份、lifecycle、manual 或 pre-recoil 输出，也不能重复用户已完成的下拉工作。

## 4. AimLab 当前得分与失败含义

Sustained AimLab 当前总分由三项相加：

- 获取分：目标越早在 deadline 前首次进入圆圈，得分越接近 1000；超时为 0。
- 跟踪分：进入跟踪阶段后，每个 plant 毫秒按圆内位置给分；中心附近接近 1 分/ms，边缘和圈外接近 0。
- 平滑奖励：在圆内、误差不增加且输出变化较小时，额外获得最多约 10% 的小奖励。

这个总分不是 0–100，也不能跨不同时长、target 数量、seed、cohort、plant 或 cadence 直接比较。ADS 与 BodyLock 仍是不同任务，不能用两个 cohort 的总分互相排序。

从 2026-08-27 起，每个目标使用固定 wall-clock 靶位：默认靶位 1575 ms（220 ms target wait + 135 ms snap + 220 ms extension + 1000 ms tracking 的上界）、其后 gap 50 ms；只生成能完整落入时长的靶位，因此 60 秒脚本固定为 36 个机会。成功、ADS 超时或 BodyLock 进入失败都必须消耗同一个完整靶位；提前失败只能等待，不能立即生成下一目标。250–330 ms 仍是获取速度的计分 deadline，但不再错误充当产品执行超时；合法延长期内完成可进入跟踪，只是迟到部分不会获得获取分。跟踪计分最多持续 1000 ms，靶位剩余空闲时间不计分。BodyLock 隔离 cohort 只有在生产 controller 真正以同一 target ID 进入 BodyLock 后才记为 acquired 并获得按实际进入时间计算的获取分；575 ms 进入超时记 miss、获取分为 0，且 `bodylock_entry_failures > 0` 是绝对硬失败。

过冲、欠跟、circle exit、空输出、方向反转、振荡、handoff、occlusion 和延迟等大多只是诊断字段；它们未必直接扣总分。benchmark 可执行程序的普通 `PASS` 主要表示没有异常；`--smoke` 额外检查 tick/update 数、有限指标、是否看见 assisted mode，并把 BodyLock 隔离 cohort 的进入失败作为硬失败。它们仍不等于产品质量通过。

## 5. 强制优化流程

每次参数搜索、算法 A/B 或“跑 AimLab 看能否优化”必须按顺序执行：

1. 在运行候选前声明本次要改善的实战症状、相关 owner、硬约束、连续指标和允许变化的参数。
2. 冻结 baseline、candidate 的场景、seed、时长、cohort、manual profile、target/POV preset、Vision/capture age、controller cadence、plant、灵敏度、response curve、脚本和报告 schema。
3. 对日志画像注明每个输入是 `measured`、`inferred` 还是 `assumption`；不完整或截断的日志不得生成“已审计”画像。
4. 先运行相关产品/事故回归，确认触发断言、oracle 和 counterfactual 都有效。任何硬门禁失败即停止比较总分。
5. 使用 `scripts/verify/compare_sustained_aimlab.ps1` 检查配对身份和
   `docs/benchmarks/sustained-aimlab-optimization-policy-v1.json` 中的受保护指标。默认不允许用另一项增益补偿任何受保护退化。
6. 约束通过后才比较获取分、跟踪分、误差分布和平滑性，并解释分数变化来自哪些场景和目标，而不是只报 aggregate。
7. 若结果依赖工具尚未表达的真实机制，状态必须是 `EXPLORATORY / MISSING COVERAGE`，下一步是补 matched log、校准或事故 RED，而不是继续扫参数。
8. 离线候选最终还要经过 matched native/live A/B、固定真实游戏场景、自由实战和用户手感确认，才能称为实战改善。

阈值、fixture 和 oracle 必须在看到候选输出前冻结。只有新的外部证据证明旧门禁不正确时才可修改，并记录原因；不得为了让候选变绿临时放宽。

## 6. 当前工具的能力边界与建设顺序

日志画像已经能够提供一部分实际 Vision delivery/capture age、目标初始几何和经过匿名化的右摇杆操作习惯；schema-18 direct-observed BodyLock 样本还能从横向 position term 和源 Controller 的 80 ms X 轴 horizon 反推出当时 Controller 采用的 response scale 分布。runner 默认把该分布 P50 用作模拟 plant 的 base response，并标为 `inferred`。这只是“Controller 当时相信的速度”，不是独立测得的游戏相机真值。benchmark 也能显式改变灵敏度 plant、Vision cadence/age、整条 controller tick、target motion preset 和 POV motion preset。

它仍不能从现有日志可靠分离世界目标运动、POV/相机运动、FOV/weapon view kick 和检测几何变化；真实相机响应（区别于 Controller 内部 estimate）、游戏原生减速、身份交错、死亡/友方、真实 target handover、left-stick/fire/recoil 习惯以及 OS/USB/ViGEm jitter 也未完整重建。因此建设优先级是：

1. 把每个真人确认的坏行为冻结为生产路径 RED；
2. 给现有聚合诊断补场景级 trigger 和失败 oracle；
3. 标定不同灵敏度/FOV/瞄具下的 stick-step camera plant 与 slowdown；
4. 采集按相对时间对齐的输入、候选身份、目标点、最终输出和画面运动 episode；
5. 用 matched live A/B 统计离线指标对真实成功/失败的区分能力，再调整权重或场景分布。

在这条映射被数据验证以前，扩大随机场景数量或提高 aggregate score 都只能增加测试量，不能自动增加真实性。

## 7. 每次报告必须回答

- 本次状态属于五级中的哪一级？
- 哪些产品约束由哪个 fixture/gate 覆盖，哪些仍缺失？
- baseline/candidate 的全部比较身份是否一致？
- 是否有任一受保护指标退化？若有，候选直接失败，不能继续以总分推荐。
- 分数变化由获取、跟踪还是平滑奖励贡献，具体发生在哪个 cohort/seed/场景？
- 模拟输入中哪些来自日志实测，哪些是推断或算法预设？
- 离真实游戏还缺什么证据，下一次 matched 采集要记录什么？
