# Research Brief: Causal Online Response Learning and Short-Horizon Control

请对下面的实时控制问题进行深度研究，并生成一个可以交给 Codex 接入现有 C++ 项目的完整研究包。优先使用控制理论、闭环在线系统辨识、时延估计、自适应控制和模型预测控制领域的论文或官方资料。不要只写概念；需要给出明确的状态定义、单位、更新公式、伪代码、测试方法、失败条件和推荐实现边界。

## 1. 研究目标

设计一套：

```text
Causal Online Response Learning
+
Short-Horizon Global Control Planning
```

即“因果在线响应学习 + 短时全局控制规划”。

在不读取武器、FOV、灵敏度或 ADS 倍率的情况下，程序需要从高频视觉和真实下发的手柄输入中实时估计：

```text
当前输入实际会产生多少画面运动
输入到视觉反馈之间存在多少有效时延
已经发送但尚未在画面中兑现多少控制运动
当前动作会给未来 80–250 ms 留下多少负担
```

最终目的不是增加学习功能，而是改善真实程序的：

- ADS 首次定位速度与刹停；
- ADS 二次修正效率；
- BodyLock 持续跟踪与变相处理；
- 10–20 px 附近的欠跟和黏滞；
- 过冲、错误反向修正和错误中断；
- 用户与 AI 输入融合；
- 短期局部最优与未来累计负担之间的取舍。

## 2. 当前项目架构

当前主要运行链路：

```text
Vision evidence
  → intent-aware selector
  → TargetCoordinator
  → immutable TargetPlan
  → ADS acquisition 或 BodyLock trajectory follow
  → AimDynamicsShaper
  → VectorIntentFuser
  → ADS-only brake
  → recoil final feed-forward
  → virtual gamepad
```

当前条件：

- Vision 约为 80–100 Hz；
- Controller 运行频率高于 Vision；
- Vision 模型输入固定为 `480×416`；
- 已有 tracker、目标速度/加速度、遮挡 hold 和 TargetPlan horizon；
- 已有简单 `AimResponseEstimator`，输出近似 `px_per_stick_second`；
- 已有左摇杆相对运动响应估计；
- 已有 causal/hindsight benchmark 和 future burden 指标；
- ADS 与 BodyLock 已经只有一个正式控制路径；
- ADS snap 只在一个物理 ADS epoch 开始时触发；
- ADS brake 只属于 ADS，不能进入 BodyLock；
- recoil 是最终 feed-forward，不能拥有目标状态；
- 动态 ROI 可能移动物理捕获窗口，但 selector、tracker 和 controller 使用 stable-centered 坐标。

历史问题：

- ADS 总是晚一拍减力或刹停；
- BodyLock 在目标变相后继续沿旧方向输入；
- 用户已经开始修正，但 AI 仍在响应上一段输入；
- 左摇杆移动造成目标相对屏幕运动后，AI 可能欠输入或错误解释；
- 10–20 px 附近出现难以越过的黏滞或欠跟；
- 固定倍率无法适应实际有效响应变化；
- 高频运行仍可能因为错误的输入—视觉配对和滤波相位滞后而表现迟钝。

## 3. 永久架构约束

所有版本必须遵守：

1. 学习结果不跨应用重启持久化。
2. 不识别、记录或建立武器数据库。
3. 不读取或要求用户录入 FOV、灵敏度、ADS 倍率。
4. 不主动向游戏注入校准摇杆脉冲。
5. 不要求用户完成专门的转圈或靶场校准。
6. 不增加额外视觉模型或额外视觉推理。
7. 不新增第二个 target lifecycle、tracker、controller、fusion 或 brake owner。
8. 学习器不能直接输出摇杆。
9. 学习器不能选择目标、切换 ADS/BodyLock 或控制 AutoFire。
10. 第一阶段必须支持 shadow-only，不能影响实际输出。
11. 正常高频运行时 CPU 和内存开销必须很低。
12. 动态 ROI 后必须使用 stable-centered 坐标，不能把 viewport 位移误学成相机运动。
13. disabled/shadow 模式不得改变现有 controller 输出。
14. 不能用未来数据参与实际运行决策；未来数据只允许进入 hindsight oracle。
15. 学习器只提供环境动力学估计，不拥有控制策略。

## 4. 核心概念

### 4.1 有效局部响应

不显式建模 FOV、灵敏度、武器或 ADS 倍率。这些都视为隐藏环境变量，只学习它们在当前时刻共同产生的有效结果：

```text
delivered stick
→ effective camera/reticle velocity in stable px/s
```

目标不是学习完整游戏设置，而是实时估计当前工作点附近的局部响应。

### 4.2 有效因果时延

高帧率只缩短采样间隔，不会自动消除：

```text
输入下发
→ 游戏读取
→ 游戏渲染
→ DXGI 捕获
→ TensorRT 推理
→ tracker/controller 收到结果
```

之间的总时延。

必须研究如何将当前视觉变化与真正导致它的历史输入正确配对。

### 4.3 Pending control motion

定义：

```text
pending_camera_motion =
已经发送、但尚未从视觉反馈中确认实现的控制运动
```

需要估计：

- 已经发送了多少输入；
- 其中多少运动已在视觉中兑现；
- 还有多少预计会在未来若干 tick 实现；
- pending motion 的方向、像素量和置信区间。

### 4.4 局部最优与短期全局最优

局部策略只根据当前误差决定当前输出：

```text
目标当前仍在右边
→ 继续向右加力
```

短期全局策略还需要考虑：

```text
过去已经发送了多少右向输入
其中多少尚未兑现
目标未来可能减速、变相、跳跃或下落
当前动作会不会增加未来反向修正和用户对抗
```

## 5. 被动闭环在线系统辨识

研究在不能主动激励系统的情况下，如何利用自然产生的用户输入和 AI 输入估计：

```text
delivered right stick
→ camera motion px/s
```

需要覆盖：

- closed-loop system identification；
- passive online identification；
- persistency of excitation；
- recursive instrumental variables；
- exponentially weighted RLS；
- robust RLS；
- ARX/ARMAX；
- augmented-state Kalman/EKF；
- disturbance observer；
- unknown-input observer；
- online change-point detection。

请明确回答：

- 哪些参数在当前观测条件下可辨识；
- 哪些情况下数学上不可辨识；
- 怎样检测 excitation 不足；
- 什么时候必须冻结学习；
- 如何避免闭环反馈造成估计偏差；
- 如何避免平滑但错误的倍率获得高置信度。

## 6. 输入—视觉因果时延估计

研究如何在大约 `10–100 ms` 范围内在线估计有效响应延迟。

候选算法：

- fixed delay bank；
- lagged regression；
- cross-correlation；
- recursive delay estimation；
- delay-augmented state model；
- Smith predictor。

要求：

- 使用 monotonic controller timestamp；
- 使用 vision `captured_at` 而不是仅使用 result-arrival time；
- frame 与 applied ROI offset 必须原子绑定；
- 处理 Vision/Controller 不同频率；
- 处理不均匀 frame interval、掉帧、timeout 和 reused observation；
- 不允许用 frame `N+1` 的 viewport 或输入解释 frame `N`；
- 给出适合实时 C++ 的固定容量数据结构和复杂度。

## 7. 左右摇杆联合动态模型

研究统一二维观测模型：

```text
observed_target_error_rate
≈ target_inertial_motion
 - R × delayed_right_stick
 + L × delayed_left_stick
 + disturbance
```

其中：

- `R`：右摇杆到画面运动的局部响应；
- `L`：左摇杆移动导致的目标相对屏幕运动；
- target inertia：tracker 的目标运动预测；
- disturbance：未解释的目标运动、噪声和游戏状态变化。

要求：

- 不使用独立 X/Y 控制仲裁；
- 可以使用二维 `2×2` response matrix；
- 能表示轴向响应差异和有限 cross-coupling；
- 明确左右摇杆贡献何时不可分离；
- 输入共线或 excitation 不足时降低置信度或冻结；
- 避免把目标横移、变相、跳跃和下落误学成控制响应；
- tracker 只提供可信度、惯性和异常判断，不被当作绝对真值。

## 8. 双时间尺度学习

重点研究：

```text
Fast local model
  最近约 100–300 ms
  快速适应当前有效响应、时延和减速变化

Stable session prior
  最近约 1–3 秒或更长的可信样本
  防止短期异常把 fast model 带偏
```

需要设计：

- forgetting factor；
- robust sample weighting；
- confidence growth/decay；
- change-point detection；
- 慢更新、快降置信；
- 模型切换时的连续输出；
- fast/stable/fallback 的置信度融合；
- 进程内生命周期和 reset 条件。

学习器不能通过武器识别解释环境变化，只能从持续模型失配中降低置信并重新收敛。

## 9. Pending-motion 状态模型

请给出一个因果更新模型，至少包含：

```text
pending_motion_px
pending_motion_velocity_px_per_sec
effective_latency_ms
response_matrix
confidence
unexplained_residual
```

研究如何：

1. 将每个 delivered stick 写入短历史；
2. 根据估计时延预测其未来运动；
3. 用新视觉观测确认已兑现部分；
4. 从 pending 状态扣除已兑现运动；
5. 对掉帧、重捕和低可信目标冻结或衰减；
6. 防止 pending 状态无限积累；
7. 为 ADS/BodyLock 提供置信区间。

## 10. 短时全局控制规划

在 response、latency 和 pending motion 可用后，研究轻量化短时模型预测控制。

不要实现大型通用 MPC。每个 tick 只评估少量候选动作，例如：

```text
0.70 × 当前 controller 建议
0.85 × 当前 controller 建议
1.00 × 当前 controller 建议
提前释放
有限反向修正
```

向前预测：

```text
80–250 ms
约 8–20 个 vision tick
```

只执行最优候选的第一个 tick，下一观测重新规划。

候选代价：

```text
J =
  cumulative target error
+ terminal error
+ pending control burden
+ reverse correction burden
+ output jerk
+ user-fight burden
+ incorrect interruption
+ target-switch/handoff burden
```

ADS 更关注：

- 首次定位时间；
- terminal residual；
- 二次修正；
- settle/brake 时机。

BodyLock 更关注：

- 持续跟踪；
- 保留合理目标惯性；
- 减少错误中断；
- 减少用户对抗；
- 变相前减少未来负担。

## 11. 有益穿心与有害过冲

不要把所有 center crossing 都判为错误。研究如何区分：

```text
有益前置：
目标仍保持同向惯性，穿心降低未来累计误差

中性穿心：
正常闭环误差

有害过冲：
目标已经减速/反向，且 pending motion 仍然过大
```

至少结合：

- radial closing velocity；
- target velocity/acceleration；
- reversal evidence；
- pending motion；
- future cumulative error；
- terminal residual。

## 12. 算法候选比较

至少比较：

1. Delay bank + Exponentially Weighted RLS。
2. Robust RLS + Huber/outlier rejection。
3. Recursive Instrumental Variable identification。
4. Augmented-state Kalman Filter / EKF。
5. ARX/ARMAX with unknown disturbance。
6. Smith predictor。
7. Disturbance Observer / Unknown Input Observer。
8. Lightweight adaptive MPC。
9. Small action-lattice rollout。
10. Hindsight sequence oracle + causal policy distillation。

每种算法比较：

- 是否适合被动闭环辨识；
- 可辨识性前提；
- 是否能处理目标自身运动；
- 是否能估计时延；
- 是否支持左右摇杆联合建模；
- CPU 和内存成本；
- 数值稳定性；
- 参数数量；
- 实现复杂度；
- 对错误样本的敏感性；
- deterministic benchmark 可验证性。

重点评估这个最小候选组合：

```text
Fixed delay bank
+ robust exponentially weighted RLS
+ two-timescale response estimate
+ confidence/change-point gate
+ pending-motion state
+ small causal action-lattice rollout
```

说明它是否足够、可能在哪里失败，以及是否真正需要 recursive instrumental variables、Kalman disturbance state 或其他复杂机制。不要为了完整性把所有算法叠加。

## 13. 模块边界

评估以下边界：

```text
CausalOnlineResponseLearner
  输入历史观测与最终 delivered input
  输出响应、时延、pending motion、置信度
  不做控制

ShortHorizonRolloutModel
  使用 TargetPlan + learner estimate
  预测候选动作未来轨迹
  不直接输出手柄

TargetCoordinator
  保持目标身份、生命周期和短期计划 owner

ADS / BodyLock
  提供当前建议动作
  接收有限的 horizon adjustment

ShadowEvaluator
  比较当前策略、候选策略和 hindsight oracle
```

请明确如何避免：

- estimator 与 tracker 重复；
- rollout 与 TargetCoordinator 重复；
- adjustment 变成第二个 controller；
- pending motion 变成共享 brake；
- learner 直接改变 AutoFire 或 target authority。

## 14. 可使用的数据

```text
monotonic timestamp
controller tick timestamp
vision captured_at timestamp
vision result timestamp
frame_id / source_frame_id
stable target position and body box
TargetPlan velocity / acceleration / lifecycle / reliability
final delivered right-stick vector
physical right-stick vector
physical left-stick vector
AI candidate vector
fusion result
ADS / BodyLock mode
ADS epoch
firing/recoil state
dynamic ROI frame-owned offset
```

在线决策只能使用当时已经可见的因果数据。未来轨迹只能用于离线 hindsight oracle 和训练/评估标签。

## 15. 样本质量与冻结机制

设计三档样本判定：

```text
hard reject
soft weight reduction
normal sample
```

至少覆盖：

- 无目标；
- target identity switch；
- weak/cue-only observation；
- coasting/projected/reused frame；
- 遮挡或低 reliability；
- frame age 超限；
- 开火/recoil；
- 目标高加速度、跳跃顶点和快速变相；
- stable ROI 坐标无效；
- 左右摇杆贡献不可辨识；
- stick excitation 不足；
- stick saturation；
- capture/inference timing 异常；
- observation 与 input 时间顺序异常。

同时分析如何避免 gate 过多导致系统永远没有可学习样本。

## 16. Benchmark A：合成闭环

需要能够设置 ground truth：

- response matrix；
- input latency；
- nonlinear response；
- left-stick contribution；
- target velocity/acceleration；
- jump/fall/reversal；
- slowdown region；
- vision jitter/dropout；
- target switch；
- dynamic ROI offset；
- correct/wrong/delayed/stale/corrective user input。

指标至少包括：

```text
response estimation error
latency estimation error
pending-motion error
50/100/200 ms rollout error
confidence calibration
change-detection delay
false model reset count
ADS acquisition and settle
post-cross burden
BodyLock interruption
user-fight burden
output jerk
CPU P95
```

必须包含 mutation/anti-cheating：

- 错误时延配对；
- 使用 result-arrival time 代替 capture time；
- 把 ROI 位移当成 camera response；
- 把目标变相当成 stick response；
- 使用未来 frame 泄漏；
- excitation 不足仍提高置信度；
- pending motion 不清零；
- learner disabled 仍改变输出。

## 17. Benchmark B：真实日志 replay

要求：

- 只使用 causal 信息；
- 固定 revision、config、engine 和 log schema；
- shadow learner 不能修改原始 runtime output；
- 比较 fixed fallback、当前 estimator 和新 learner；
- hindsight oracle 只计算 headroom；
- 不把 hindsight 成绩冒充可上线成绩；
- 同时覆盖普通、遮挡、目标变相、多目标和混合用户输入；
- 报告不可辨识窗口比例，而不是隐藏被拒绝样本。

## 18. 硬验收门槛

至少要求：

- learner disabled 时现有输出 bit-stable；
- shadow mode 不影响 controller；
- 不产生非有限状态；
- 错误时延配对 mutation 被 benchmark 检出；
- dynamic ROI 移动不被学习成 camera response；
- target motion mutation 不显著污染 response；
- 低置信度 estimate 不影响实际控制；
- 模型变化不造成输出跳变；
- 不可辨识时冻结而不是生成伪精确估计；
- CPU P95 满足实时预算；
- 实际接管后 ADS/BodyLock 至少一项主要指标改善；
- user-fight、jerk、错误中断和 identity switch 不恶化；
- 如果只改善实验室合成场景而真实日志 replay 无改善，则不得启用。

## 19. 分阶段路线

完善以下路线：

```text
G0  Decision journal
    记录当前局部决定、当时可见状态与后续实际结果

G1  Hindsight sequence oracle
    测量局部策略到更优短期路线的理论差距

G2  Shadow CausalOnlineResponseLearner
    估计响应、时延和 pending motion，不影响控制

G3  Causal rollout evaluator
    在线比较少量候选动作，仍不影响控制

G4  Confidence-gated bounded adjustment
    只允许有限比例调整 ADS/BodyLock 建议值

G5  Tail-value decision
    根据证据决定是否需要更长期的 tail-value 学习
```

每个阶段必须：

- 可独立合并；
- 默认不影响当前正常运行；
- 有明确 benchmark 和停止条件；
- 有配置回滚开关；
- 不依赖后续阶段才能正常使用；
- 不因为完成 G2/G3 而要求立即上线 G4。

## 20. 关键问题

研究必须明确回答：

> 在没有主动测试输入、目标自身也会运动、左右摇杆可能同时变化的闭环系统中，哪些响应参数可以可靠在线辨识？当系统暂时不可辨识时，如何检测并冻结学习，而不是产生一个看起来平滑但实际错误的倍率？

同时回答：

> 高频视觉下历史滞后主要来自采样频率、输入—frame 时间配对、tracker 滤波相位、错误响应模型还是重复控制状态？应如何通过可观测实验区分？

## 21. 最终交付物

生成适合 Codex 接入的独立研究包：

```text
README.md
docs/
  CAUSAL_RESPONSE_MASTER_SPEC.md
  IDENTIFIABILITY_AND_DELAY.md
  ONLINE_ESTIMATOR_COMPARISON.md
  PENDING_MOTION_SPEC.md
  SHORT_HORIZON_CONTROL_SPEC.md
  BENCHMARK_SPEC.md
  EXECUTION_STAGES.md
reference/
  causal_online_response_learner.h
  causal_online_response_learner.cpp
  causal_online_response_learner_tests.cpp
  delay_bank.h
  pending_motion_model.h
  rollout_model.h
  Python reference implementation
  synthetic fixture generator
  metrics and mutation tests
patches/
  repository integration guidance
prompts/
  stage-specific Codex prompts
SHA256SUMS.txt
```

交付要求：

- C++ 参考实现使用固定容量容器；
- controller tick 中避免动态分配；
- 所有公式明确单位、坐标方向和时间戳来源；
- 给出算法复杂度和预计 CPU 成本；
- 清楚区分研究结论、参考代码和生产验收；
- 不伪造 Windows/MSVC/CUDA/TensorRT 或实战验证；
- 引用优先使用论文和官方资料；
- 给出推荐最小方案及拒绝其他方案的具体理由；
- 不推荐大型神经网络、持续强化学习、武器数据库或主动校准作为第一版；
- 必须给出可执行测试样例、mutation tests 和停止条件；
- 说明参考实现仍需针对真实仓库接口适配。

建议将完成的研究包保存到：

```text
D:\datasets\causal_online_response_package
```

完成后，我会让 Codex 检查包的完整性、算法假设、参考测试，以及它与现有 C++ 仓库真实接口之间的差异。
