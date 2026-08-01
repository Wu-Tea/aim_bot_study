# 四种实机控制链异常：复现、根因修复与验收计划

> 日期：2026-08-01
> 状态：计划已执行并进入后续实机验证。Situation 1–3 已落地；Situation 4 最终采用 contextual manual/AI dual-proposal 仲裁；Release/CTest/权威 matched 矩阵通过，`DE31...` runtime 已按用户指令覆盖。最新 25.5 分钟日志又暴露轻微 ADS 过冲/欠跟残余，已诊断但尚未追加修复。
> 执行顺序：先建立四个稳定复现，再优先修复 Situation 2、3 两个已确认 bug，随后处理 Situation 1、4 的算法行为。
> 工作区约束：当前 checkout 含有用户所有的未提交改动和 Task 5 现场代码，不得 reset、checkout 覆盖、批量清理或把无关改动带入提交。

### 2026-08-01 最终执行同步

- Situation 1：identity hold 与 actuation lease 已分离；`12.5 ms` grace 后连续退场，
  `65 ms` 释放，不缩短 `180 ms` association hold。
- Situation 2：fresh firing position 始终 authoritative；
  `fire_innovation_limit_px` 只约束 velocity innovation。
- Situation 3：same-target `Reacquiring` 不再触发 fuser manual-only 硬切；
  replacement、NoTarget、Manual 和 full escape 边界保持。
- Situation 4：manual 与 AI 都作为绝对摇杆 proposal 进入唯一
  `VectorIntentFuser`。强同向 ADS 和近距 BodyLock 使用完整 AI proposal、
  情境归一化 manual 与 `20%` parallel headroom；切向/反向、近满幅 escape 和
  远距 BodyLock 不缩放。
- 验证：Release 全量构建、focused tests、左摇杆 5/0 harness、CTest `34/34` 和
  `git diff --check` 通过。最终权威矩阵见
  `artifacts/benchmarks/sensitivity-manual-mix-20260801/contextual-headroom-0.20-final-exact/`。
- 当前 runtime：
  `native/vision_native/build/Release/cod_native_runtime.exe`，SHA-256
  `DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590`。
- 验收与设计记录：
  `docs/project/CONTROL_CHAIN_JUMP_STABILITY_ACCEPTANCE_20260801.md`、
  `artifacts/benchmarks/sensitivity-manual-mix-20260801/CONTEXTUAL_DUAL_PROPOSAL_VERIFICATION.md`、
  `.agent-context/decisions/DEC-2026-08-01-002-contextual-manual-ai-dual-proposal-arbitration.md`。
- 最新 `20260801T131000Z_6544_1` session 的轻微 ADS 过冲/欠跟主要指向
  “移动 + 从物理 LT epoch 起算的 220 ms ceiling”，不支持累积学习漂移是主要原因。
  只读诊断见 `docs/project/ADS_LONG_SESSION_DIAGNOSIS_20260801.md`。
- 该残余尚未实现修复。下一步必须先补 target-segment elapsed、handoff reason、
  position/motion contribution 与 production response scale/confidence telemetry；
  不得通过 held-LT ADS re-arm 破坏“一次物理 LT 只强 snap 一次”的合同。

### 2026-08-01 中间 candidate 快照（历史）

- Situation 2：已将新鲜位置观测与开火速度创新分离；新鲜 position 立即生效，`fire_innovation_limit_px` 只约束速度估计。
- Situation 3：已移除同一 `target_id` 在 `Reacquiring` 时的 manual-only 硬切；目标替换、无目标、Manual 模式和 full escape 的安全边界保持不变。
- Situation 1：`hold_ms=180 ms` 继续只负责身份/关联；Coasting 施力在 `12.5 ms` grace 后连续衰减，并在 `65 ms` 退休。
- Situation 4：现有离线 replay 未能稳定证明第一处反向发生在 plan/request/shaper/fuser/recoil 中的哪一层，因此没有为了过指标继续叠加 BodyLock、shaper 或 fuser 补丁；下一步需要实机逐层 telemetry。
- 验证：五个 focused executable 与 CTest `34/34` 通过，`git diff --check` 通过；3-seed、7 场景矩阵已完成。
- 放行状态：相对最接近但 source-different 的同配置 Task4 reference，ADS tracking `-3.1120%`、BodyLock tracking `-2.3364%`、ADS overshoot `+8.1153%`，超过计划门槛；该比较不能严格归因到本轮修复，但也不足以放行。
- 当时用户 runtime 保持未动，SHA-256 为 `7D15B577C4570EB6871ADFF82D2568E4AD899FA08BE208C825B4C8AF282F2012`。该段仅保留为 candidate 历史，完整证据见 `artifacts/benchmarks/four-situation-control-chain-20260801/final-validation/`。

## 1. 这次工作要解决什么

当前控制链是：

```text
Vision / selector
  -> TargetCoordinator
  -> TargetPlan
  -> ADS Acquisition 或 BodyLock Follow
  -> AimDynamicsShaper
  -> VectorIntentFuser
  -> recoil
  -> virtual gamepad
```

四段视频暴露了四类不同问题，不能用一个“整体降力度”补丁处理：

| 情况 | 现场症状 | 当前判断 | 首要责任层 |
|---|---|---|---|
| Situation 1 | 目标突然缩回掩体后，系统继续保持一段旧方向力度并拉飞 | 已确认是丢失目标后的施力生命周期过长；属于算法策略优化 | `TargetCoordinator` 的 Coasting authority，`AimDynamicsShaper` 只负责连续释放 |
| Situation 2 | 约 11 秒时无人工输入却突然向右猛拉 | 已确认 bug：开火期间对 position innovation 也做了 `3.5 px` 限幅，新鲜目标已越过中心但内部位置仍停留在右侧 | `TargetCoordinator` 的位置/速度观测更新 |
| Situation 3 | 击杀后出现一次突然向下猛拉 | 已确认 bug：同一目标短暂丢帧后，shaper 与 fuser 对 `Reacquiring` 的语义冲突，造成 AI 先卸载、再释放隐藏积累 | `AimDynamicsShaper` 与 `VectorIntentFuser` 的生命周期合同 |
| Situation 4 | 近距离 BodyLock 出现摆荡；人工大力同向推时像弹力绳一样甩动 | 分成 4A 系统自身反复反向、4B 人工与 AI 同向叠加两个问题；Task 5 的 cooperative cap 已介入但未消除上游反向 | 先查 ADS/BodyLock request 源，再决定是否需要收紧 fuser 的近距同向重叠 |

这次不做以下事情：

- 不降低全局 ADS/BodyLock strength 来掩盖跳变。
- 不增加第二个 tracker、第二个位置状态或最终输出 brake。
- 不修改 recoil 来补偿目标控制错误。
- 不重写 selector，不给所有新鲜 Vision 观测增加统一 hard clamp。
- 不增加武器数据库、额外 Vision inference 或持久化学习。
- 不在复现失败时凭手感直接调常量。

## 2. 当前现场身份与证据边界

本计划使用以下现场作为 baseline：

- 当前 runtime：`native/vision_native/build/Release/cod_native_runtime.exe`
- 当前 runtime SHA-256：`7D15B577C4570EB6871ADFF82D2568E4AD899FA08BE208C825B4C8AF282F2012`
- 日志 session：`runs/native_perf/sessions/20260801T061208Z_64744_1/`
- 日志基准 sample：`360545203083600 ns`
- 四个视频相对日志的起点：
  - Situation 1：`183.810 s`
  - Situation 2：`228.117 s`
  - Situation 3：`243.617 s`
  - Situation 4：`383.903 s`

视频证据：

1. `Call of Duty Black Ops 7 2026.08.01 - 14.15.29.172.DVR.mp4`
   - 重点窗口：视频约 `10.30-10.50 s`
   - 目标 `77` 丢失后 AI 仍持续约 `74.09 ms`，AI 峰值约 `0.567`，最终向量峰值约 `0.831`。
2. `Call of Duty Black Ops 7 2026.08.01 - 14.16.13.173.DVR.mp4`
   - 重点窗口：视频约 `11.20-11.47 s`
   - 新鲜 Vision X 已从 `+10.7 -> -7.2 -> -15.9 -> -27.5 px` 穿过中心，但内部 plan X 仍为 `+40.9 -> +45.3 -> +41.8 -> +46.0 px`；错误方向输出约 `107.33 ms`，AI X 峰值约 `0.987`，人工 X 接近零。
3. `Call of Duty Black Ops 7 2026.08.01 - 14.16.28.174.DVR.mp4`
   - 重点窗口：视频约 `9.286-9.335 s`
   - 一次 fresh miss 后进入 `Reacquiring`；shaped AI 从约 `(0.160,-0.160)` 继续上升，fused AI 却被切到约 `(0.015,-0.014)`，随后在 Coasting tick 重新爬升到约 `(0.320,-0.320)` 并继续向下；recoil 在窗口内基本恒定，不能解释这次跳变。
4. `Call of Duty Black Ops 7 2026.08.01 - 14.18.49.175.DVR.mp4`
   - 重点窗口：视频 `9 s` 之后两个人物。
   - 第一人用于复现系统自身摆荡；第二人用于复现人工刻意大力推动后的弹力绳效果。
   - Task 5 近距 cooperative cap 在现场确实触发：约 `98` 个 close/cooperative 样本，raw 到 post 的最大削减约 `0.2708`；但同一 track `181` 约 `0.39 s` 内仍出现 X 轴 `4` 次、Y 轴 `3` 次有意义的 AI 反向，说明只限制人工与 AI 的相加还不够。

证据结论必须保持分级：

- Situation 2、3 的根因已经由视频、逐 tick telemetry 和源码路径共同确认，可直接进入 RED/GREEN 修复。
- Situation 1 的异常链已确认，但释放时间常量仍需要与合法短遮挡 A/B 后确定。
- Situation 4 已确认有系统反向和人工叠加两个现象，但 4A 必须先分清 position term 与 motion/feed-forward term，不能提前假定最终修复层。

## 3. 架构与所有权合同

### 3.1 每个状态只能有一个所有者

| 数据或行为 | 唯一所有者 | 允许做什么 | 不允许做什么 |
|---|---|---|---|
| 目标身份、Observed/Coasting/Reacquiring/None | `TargetCoordinator` | 关联目标、保留短遮挡身份、发布 lifecycle | fuser/shaper 不得重新判断目标是谁 |
| 新鲜位置、目标速度、开火扰动、Remaining | `TargetCoordinator` | 新鲜位置立即纠正当前状态；速度只吸收可信持续残差 | 不得让 firing clamp 改写新鲜 position |
| ADS/BodyLock 的 AI 请求 | `AdsAcquisitionController` / `BodylockFollowController` | 根据同一 `TargetPlan` 生成模式请求 | 不得自己建立第二套共享目标状态 |
| AI 请求的 rise/decay/reversal 和 ADS->BodyLock handoff | `AimDynamicsShaper` | 平滑 AI proposal；禁止无观测时盲目增力 | 不得处理人工与 AI 的最终所有权 |
| 人工 + AI 融合和最终向量连续性 | `VectorIntentFuser` | 保留人工、限制冲突/同向重叠、保证最终向量 slew | 不得用 lifecycle 硬切造成隐藏 AI backlog |
| recoil | recoil 层 | 在融合后叠加独立 recoil 输出 | 不得反馈成 target position 或 Remaining 债务 |

### 3.2 三条不可破坏的既有决定

1. 新鲜 Vision position 是当前位置的权威证据；预测只桥接帧间和短遮挡。
2. 开火扰动可以限制其进入 velocity 的影响，但不能把新鲜 position 限制在旧预测附近。
3. `VectorIntentFuser` 是唯一最终 `manual + AI` 连续性所有者；不能再增加一个 post-output limiter。

### 3.3 Situation 1 所需的生命周期拆分

当前 `hold_ms` 同时近似承担两个语义：

- 为同一目标保留 identity，方便短遮挡后 reacquire；
- 在没有新鲜目标时继续允许 AI 施力。

这两个语义必须拆开：身份可以保留较久，施力必须更快退场。计划保留现有约 `180 ms` identity hold，不重启 ADS、不切换 target id；只在 `TargetCoordinator` 内为 `plan.aim_authority` 增加更短的 Coasting actuation lease。

## 4. 复现基础设施

### 4.1 产物布局

执行时创建：

```text
artifacts/benchmarks/four-situation-control-chain-20260801/
  IDENTITY.md
  baseline/
    situation-1-window.jsonl
    situation-2-window.jsonl
    situation-3-window.jsonl
    situation-4a-window.jsonl
    situation-4b-window.jsonl
    metrics.json
  candidate-s2/
  candidate-s3/
  candidate-s1/
  candidate-s4/
  final/
    metrics.json
    VERIFICATION.md
```

`IDENTITY.md` 必须记录：HEAD、`git status --short`、相关 diff fingerprint、runtime/config/engine SHA-256、日志 session、提取窗口和构建时间。原始大日志和视频不复制进仓库。

### 4.2 离线提取器

建议新增：

- Create: `tools/analyze_control_chain_incidents.py`
- Create: `tests/test_control_chain_incident_analysis.py`

提取器只顺序扫描 JSONL，不把 800 MB 日志整体载入内存。它需要输出每个 tick 的：

- sample/time、fresh capture、source frame/observation id、target id；
- Vision aim/error 与 `TargetPlan` aim/error；
- lifecycle、mode、observation age、reliability、aim authority；
- error rate/velocity、remaining work；
- requested AI、shaped AI、fused/post-AI、pre-recoil、recoil、final；
- manual right stick、manual confidence、fire/firing-recently；
- material reversal、wrong-way duration、AI off/on、parallel cooperative demand。

工具必须给出以下统一定义：

- **material reversal**：同一轴前后符号相反，且两边绝对值都不小于 `0.08`；低于此值的零点噪声不计数。
- **wrong-way tick**：新鲜 Vision error 与 plan/request/fused AI 的控制方向相反，且对应输出绝对值不小于 `0.08`。
- **oscillation episode**：同一 target、同一轴、`400 ms` 内至少 3 次 material reversal。
- **hidden backlog**：`|shaped AI - applied/fused AI residual|` 在 lifecycle gate 后积累，并在后续 tick 重新释放。
- **cooperative parallel demand**：人工方向单位向量上的 `manual magnitude + max(0, dot(AI, manual_direction))`。

### 4.3 确定性 C++ 复现

为避免测试依赖用户视频和大日志，执行时把上述短窗口转成最小输入序列：

- Create: `native/controller_native/control_chain_incident_fixtures.h`
- Modify: `native/controller_native/target_pipeline_integration_tests.cpp`
- 如需独立测试 target：Modify `native/vision_native/CMakeLists.txt`；优先复用现有 `cod_native_controller_tests`，避免复制整套 pipeline 链接依赖。

fixture 只保留能驱动控制链的字段：时间、capture freshness、candidate aim/body/size/reliability/source id、manual、ADS/fire、response feedback。输出端每 tick 保存 `TargetPlan -> requested -> shaped -> fused -> recoil` 的关键向量。

复现原则：

1. Situation 2、3 在修复前必须稳定 RED；如果不能 RED，禁止先改源码。
2. Situation 1、4 至少要稳定复现同类指标，不要求像素逐帧完全相同。
3. fixture 必须包含反例：合法横移、真实反向、36 ms 短遮挡、远距人工协作和 full manual escape。
4. 视频只负责证明主观异常；自动化验收以同源 telemetry 和确定性 fixture 为准。

## 5. Task 0：锁定 baseline 与四个 RED 复现

### 5.1 只读锁定

- 记录当前工作树，不修改或清理无关文件。
- 记录当前 runtime SHA-256 `7D15...2012`，并验证 session 时间覆盖四段视频。
- 对 `target_coordinator`、`aim_dynamics_shaper`、`vector_intent_fuser`、`bodylock_follow_controller` 及其测试生成相关 diff fingerprint。
- 将当前 Task 5 cooperative-cap 代码视为 baseline 的一部分，不能拿 Task 4 executable 代替现场 baseline。

### 5.2 RED 合同

| 情况 | 修复前必须复现的失败 |
|---|---|
| S1 | fresh no-target 之后，AI 在没有新观测时仍维持显著能量约 74 ms；`aim_authority`/shaped AI 释放过慢 |
| S2 | firing + same target + fresh center crossing 时，plan error 仍停留在旧侧，wrong-way request 持续多个 controller tick |
| S3 | `Observed -> fresh miss/Coasting -> same-target Reacquiring -> Coasting` 中，fuser 在 Reacquiring 硬退到 manual，而 shaper 继续更新，随后出现 backlog reassert |
| S4A | 零人工输入的近距 BodyLock 窗口出现一个 oscillation episode |
| S4B | 强人工同向输入时 cooperative parallel demand 和释放后的反向能量形成明显摆荡；Task 5 虽削减叠加但不能消除 episode |

Task 0 完成闸：四个窗口、身份文件和 baseline 指标齐全；S2/S3 自动测试明确 RED。

## 6. Task 1：修复 Situation 2——新鲜位置被 firing innovation cap 错误限制

### 6.1 已确认根因

当前 `TargetCoordinator` 在 firing context 中：

1. 计算 `innovation = observed_aim_px - predicted`；
2. 用 `fire_innovation_limit_px`，当前为 `3.5 px`，同时裁剪 `innovation` 和 `velocity_innovation`；
3. 再用 `predicted + innovation` 生成 `measured_position`。

因此这个本来用于抑制开火扰动进入速度的 limit，也把新鲜 position 锁在旧预测附近。目标快速从右侧穿到左侧时，每帧最多只能向左修 `3.5 px`，旧的正向速度预测反而继续把内部 plan 推到右侧。BodyLock 按错误 plan 正常输出，于是形成纯 AI 右拉。

### 6.2 RED 测试

修改：

- `native/controller_native/target_coordinator_tests.cpp`
- `native/controller_native/target_pipeline_integration_tests.cpp`

增加：

1. `firing_fresh_position_remains_authoritative_across_center`
   - 同一 target、Observed、firing recently；
   - 新鲜 observation 从 `+11 px` 跨到 `-8/-16/-28 px`；
   - 修复前断言失败：plan position/error 没有跟随新鲜 observation 的符号。
2. `firing_position_and_velocity_innovation_have_separate_authority`
   - position 必须使用完整新鲜观测；
   - velocity residual 仍受 `fire_innovation_limit_px` 和 firing disturbance observer 限制。
3. `incident_s2_no_wrong_way_bodylock_request`
   - 复放 S2 窗口；
   - 新鲜观测跨中心后，BodyLock request 不得继续在旧方向维持多个 tick。

### 6.3 实现方式

修改：

- `native/controller_native/target_coordinator.cpp`
- 仅在需要命名或测试入口时修改 `target_coordinator.h`

将一个 innovation 拆成两个清晰变量：

```text
position_innovation = fresh observed position - predicted position
velocity_innovation = position_innovation 的副本，供速度观测器使用
```

实现合同：

- 正常 same-target Observed frame 的 `measured_position` 使用完整 `position_innovation`，等价于当前稳定 body geometry 处理后的 `observed_aim_px`。
- firing `3.5 px` influence limit 只裁剪 `velocity_innovation`，不再修改 `position_innovation`。
- firing residual consistency observer 只决定 residual 是否进入 velocity；拒绝的残差不得以后重新释放。
- `learn_player_motion_amplitude` 继续消费已过滤/有界的 residual，不把枪口瞬态当成玩家运动学习样本。
- 现有 `max_reacquire_innovation_px` 暂不在本 Task 顺手修改；它有独立 lifecycle 语义，除非 S3/S1 fixture 证明它参与异常。
- 不在 BodyLock、shaper 或 fuser 增加“最终符号修正”来掩盖错误 plan。

### 6.4 影响与风险

正向影响：所有 firing ADS/BodyLock 都能立即响应新鲜目标位置，尤其是高速穿越中心和近距大像素位移。

主要风险：枪口/box 瞬态会重新出现在即时 position 中。风险由已有 stable body geometry、AI shaper 和 final vector slew 承担；不能继续让 tracker 牺牲位置真实性来滤噪。

### 6.5 完成标准

- 新鲜 observation 与 plan error 的轴向符号错配：`0 tick`。
- 跨中心后 request 旧方向残留：最多 `1 controller tick` 且不超过 `4 ms`，不得形成 material wrong-way output。
- firing velocity disturbance 既有测试全部通过；stationary firing 不新增 velocity lead。
- 普通移动、真实反向、ADS 和 BodyLock tracking 不恶化超过 `2%`；overshoot/continued push 不恶化超过 `5%`。

## 7. Task 2：修复 Situation 3——Reacquiring 跨层硬切与隐藏 AI backlog

### 7.1 已确认根因

同一目标短暂丢帧后，`TargetCoordinator` 发布 `Reacquiring`。当前两层对它的解释不一致：

- `AimDynamicsShaper` 只对 `Coasting` 禁止 blind rise；`Reacquiring` 带有新鲜观测，因此它按新 request 正常更新 shaped AI。
- `VectorIntentFuser` 却把所有 `Reacquiring` 硬切为 manual-only，并保留 re-entry 状态。
- 下一 controller tick 可能又变为 `Coasting`；fuser 从 manual 基线重新追逐已经增长的 shaped AI，于是出现先卸载、再回弹的离散跳变。

这不是 recoil，也不是目标本身突然向下移动，而是同一 lifecycle 在两个层被重复拥有。

### 7.2 RED 测试

修改：

- `native/controller_native/vector_intent_fuser_tests.cpp`
- `native/controller_native/aim_dynamics_shaper_tests.cpp`
- `native/controller_native/target_pipeline_integration_tests.cpp`

增加真实序列：

```text
Observed target 104
-> one fresh no-target frame / Coasting
-> same target 104 Reacquiring with fresh observation
-> controller-rate Coasting ticks between Vision publications
```

断言：

- 修复前 Reacquiring tick 会出现 `shaped AI != 0` 但 fused AI 几乎归零。
- 随后若 shaped AI 未下降，fused AI 会再次以 `0.08/tick` 追赶，形成 backlog。
- 对照用例必须包括 `TargetChanged`、`None`、Manual、full manual escape，证明真正的所有权边界仍能切断旧 AI。

### 7.3 实现方式

修改：

- `native/controller_native/vector_intent_fuser.cpp`
- 视枚举兼容性决定是否只保留而不再使用 `FusionFallbackReason::Reacquiring`
- 通常不需要修改 `AimDynamicsShaper` 实现；只补充测试和合同注释

最小修复：

- 删除 fuser 对 same-target `Reacquiring` 的 unconditional manual-only early return。
- `Reacquiring` 视为“同一目标获得了新的观测”，允许 shaped AI 通过现有 final vector slew 连续进入。
- `Coasting` 仍由 shaper 禁止无观测 blind rise。
- `TargetChanged` 仍必须给新目标一个精确 manual/zero-old-AI admission tick，然后通过既有 re-entry slew 进入。
- `None`/Manual 仍精确输出物理 manual；full manual escape 仍无条件立即归用户。
- 不给 shaper 新增第二个 Reacquiring gate，否则只是把硬切从 fuser 搬到上一层。

### 7.4 影响与风险

正向影响：同目标一帧丢失不会再产生 AI off/on；S3 的向下猛拉失去释放来源。

主要风险：错误关联被当成 same-target reacquire 时，AI 不再自动空一 tick。这个风险仍由 `TargetCoordinator` 的 target identity 和 `TargetChanged` 合同负责；不能让 fuser 通过猜目标身份解决。

### 7.5 完成标准

- Reacquiring 期间 fused residual 不得无理由坍缩到 manual。
- 无强人工 escape 时，相邻 fused output 向量变化不超过现有 `0.08/tick + epsilon`。
- `|shaped AI - fused AI residual|` 不得因为 lifecycle gate 连续增长并在后续释放。
- S3 fixture 不出现 oscillation episode，不出现击杀后向下 material impulse。
- `TargetChanged`、NoTarget、full manual escape 和 ADS->BodyLock handoff 测试不回归。

## 8. Task 3：优化 Situation 1——保留身份，但快速退休无观测施力

### 8.1 问题流程

当前丢失目标后：

```text
fresh no-target
-> TargetCoordinator 保留 target/velocity，进入 Coasting
-> reliability 按约 180 ms hold 线性下降
-> BodyLock 继续按 predicted error/velocity 产生 request
-> shaper 虽禁止 blind rise，但仍保留已有能量
-> fuser 连续输出旧方向
```

这对极短 FOV/遮挡有价值，但对目标已经缩回掩体时会继续拉向旧位置。解决方案不是立即删除 target identity，而是缩短“无新鲜证据还能施加多少 AI”的 lease。

### 8.2 RED 与反例测试

修改：

- `native/controller_native/target_coordinator_tests.cpp`
- `native/controller_native/aim_dynamics_shaper_tests.cpp`
- `native/controller_native/target_pipeline_integration_tests.cpp`

正例：

- S1 的 cover-retreat 序列：已有较强 AI，随后连续 fresh no-target；AI 不得继续增力，必须在短 lease 内退场。

反例：

- `12.5 ms` 单帧缺失：连续跟踪几乎不变。
- `36 ms` 合法 moving-target occlusion：允许预测桥接，不产生 false stop 或明显 tracking loss。
- 遮挡后 same-target reacquire：保留原 target id，不重启 ADS，不走 replacement-target admission。
- 遮挡后 different target：继续走 `TargetChanged` 安全边界。

### 8.3 实现方式

修改：

- `native/controller_native/target_coordinator.h`
- `native/controller_native/target_coordinator.cpp`
- 如需将默认值映射到生产构造：`native/controller_native/native_gamepad_controller.cpp`

建议在 `TargetCoordinatorConfig` 增加两个仅负责 actuation 的参数：

```text
coast_full_authority_grace_ms = 12.5 ms   # 约一帧 Vision 空隙
coast_actuation_release_ms    = 65 ms     # 首轮候选，必须做 50/65/80 ms A/B
```

保持：

- 现有 `hold_ms` 继续负责 identity/association，约 `180 ms`，不缩短。
- `plan.reliability` 继续表达证据可靠性，不重定义。

新增：

- 当 lifecycle 为 Coasting 时，在 `plan.aim_authority` 上乘一个连续的 `coast_actuation_scale`：grace 内保持，随后 smooth decay，在 release 时归零。
- `AimDynamicsShaper` 继续使用现有 Coasting no-blind-rise 规则，将降低后的 request 连续释放。
- fuser 只执行已有最终向量 slew，不新增 lifecycle 分支。

候选常量选择规则：

1. 先用 `65 ms` 对 S1 GREEN。
2. 对 `50/65/80 ms` 做相同 seed 的 36 ms occlusion A/B。
3. 选择能够让 S1 在 `65 ms` 内退休，同时合法 36 ms occlusion tracking 回归不超过 `2%` 的最短稳定值。
4. 如果三者都失败，停止，不把 `hold_ms` 一起缩短；先检查 no-target freshness 与 capture timing 是否被误判。

### 8.4 影响与风险

正向影响：掩体后旧方向能量快速消失，同时 same-target identity 仍可复用。

主要风险：真正的短 Vision 丢帧会暂时降低跟随力度。通过“一帧 grace + 独立 actuation decay + 36 ms 反例”控制风险。

### 8.5 完成标准

- 首个 fresh no-target 后 shaped AI magnitude 不得继续上升。
- S1 cover-retreat 中，AI authority 最迟在选定 release 时归零；不得再出现约 74 ms 后仍有 material old-direction pull。
- identity 在 `hold_ms` 内保持；same-target reacquire 不重启 ADS。
- 36 ms moving occlusion tracking 回归不超过 `2%`，overshoot/continued push 不恶化超过 `5%`，false stop/interruption 不增加。

## 9. Task 4：处理 Situation 4——先消除系统自振，再验收人工协作

Situation 4 必须拆成两个子任务，且在 S2、S3 修复完成后重新跑 baseline。S2 的错误位置和 S3 的 off/on 本身都可能贡献反向；禁止在它们尚未修好时继续叠加 fuser 补丁。

### 9.1 Task 4A：系统自身摆荡定位

#### 复现

零人工右摇杆，近距离同一 target，分别覆盖：

- 静止目标 + 主视角移动；
- 横移目标 + 主视角移动；
- firing 与 non-firing；
- Observed 连续流与一帧 Coasting/Reacquiring。

每 tick 分解：

```text
position contribution = error / arrival horizon / response
motion contribution   = error_rate / response * feedforward gain
requested -> shaped -> fused -> pre-recoil -> final
```

可以先在离线分析器按当前公式重算 position/motion contribution；只有日志字段不足时，才给 `BodylockFollowController` 增加测试/telemetry diagnostics。不要为了观测而改变控制返回类型。

#### 决策树

| 首次出现 material reversal 的层 | 修复位置 | 实现原则 |
|---|---|---|
| 新鲜 Vision/plan error 就反复翻转 | `TargetCoordinator` | 修观测/速度准入，不在下游滤最终输出 |
| plan position 稳定，`error_rate`/motion term 反复翻转 | `TargetCoordinator` 的 velocity admission，或 BodyLock 的近到达 feed-forward 使用方式 | 不把不可信速度变成反向控制债务 |
| plan 稳定，requested 才反复翻转 | `BodylockFollowController` | 约束 motion contribution，不动全局 force |
| requested 稳定，shaped 才翻转 | `AimDynamicsShaper` | 修 reversal 状态机，不能加 fuser gate |
| shaped 稳定，只有 fused/final 翻转 | `VectorIntentFuser` 或 recoil | 只修实际首发层；recoil 必须用 pre-recoil 对照证明 |

#### 预期最小实现（仅在 motion term 被证实时采用）

修改：

- `native/controller_native/bodylock_follow_controller.cpp`
- `native/controller_native/bodylock_follow_controller_tests.cpp`
- 必要时 `response_model_aim_solver` 只提供可复用的 contribution 计算，不在共享 solver 内增加 BodyLock 专用状态

使用无历史、每 tick 常数时间的 near-arrival motion envelope：

- 只在 close BodyLock capture geometry 内生效；远距和大误差保持原行为。
- 使用当前位置 contribution、motion contribution 和 predicted terminal error 判断本 tick 是否已经会穿过目标。
- 当 motion contribution 会让总请求跨过当前位置误差、且 terminal prediction 已显示本 horizon 内穿越时，将该轴 motion contribution 限制到“最多抵消 position contribution，不在同一旧证据 tick 上反向增力”。
- 允许输出到零；下一份 fresh observation 如果已真正越过中心，可立即给出新方向。
- 不增加 N 帧 reversal confirmation、输出历史或另一个 brake owner。

这个实现只是一条证据分支；如果首个 reversal 不在 requested motion term，不得保留它。

#### 4A 完成标准

- 同一 target 的 `400 ms` 窗口内不再出现 3 次及以上 material reversal。
- 允许一次真实穿越后的必要修正；不允许反复左右/上下拉扯。
- close moving target 和主视角移动 tracking 回归不超过 `2%`。
- 远距目标 requested/shaped/fused 输出在数值容差内保持不变。

### 9.2 Task 4B：人工大力同向推的弹力绳效果

#### 复现

在通过 4A 的同一 close fixture 上增加人工序列：

```text
manual 0
-> 0.65-0.90 同 AI 方向保持 100-180 ms
-> 快速释放到 0
-> 可选地向相反方向主动修正
```

同时保留：

- 远距同向协作；
- 人工与 AI 正交，用于证明有用的垂直/横向跟随未被删除；
- 人工与 AI 对抗；
- `>= 0.95` full manual escape。

#### 实现顺序

1. 先仅使用 S2/S3/4A 修复和当前 Task 5 cooperative cap 重放。
2. 如果摆荡已消失，不再改 fuser；说明 Task 5 cap 足够，根因是上游 AI 反向。
3. 只有在 shaped AI 已稳定、但 close cooperative parallel output 仍形成过量飞行时，才修改 `vector_intent_fuser.cpp`。

若需要 fuser 最小改动：

- 继续把 manual 和 AI 当成两个 control proposal，而不是两个可无限相加的力。
- 在 close + strong-manual + same-direction 时，parallel 输出上限从“较强 proposal + 固定弱侧重叠”连续收紧到接近较强 proposal；不改正交 AI 分量。
- close commitment 必须连续，不能新增一个会在阈值两侧开关的 hard gate。
- 人工释放时不得释放历史 AI backlog；final vector 仍只用现有 `0.08` reassert / `0.20` yield envelope。
- 不改变远距协作、反向 manual preservation 和 full manual escape。

#### 4B 完成标准

- close 强人工同向输入期间，parallel output 不得接近简单相加后的峰值；必须满足选定的 proposal-overlap 上限。
- 人工释放后 `50 ms` 内不得出现与当前 fresh error 相反的 material rebound。
- full manual escape 仍为精确物理输入，无额外延迟。
- 正交 AI 跟随保持；远距协作 tracking 回归不超过 `2%`。
- 4A、4B 都不得形成 oscillation episode。

## 10. Task 5：分层验证与 matched benchmark

### 10.1 每个修复单独形成 candidate

执行顺序和闸门：

```text
baseline
  -> S2 candidate：只修 position/velocity authority
  -> S3 candidate：只修 Reacquiring 跨层合同
  -> S1 candidate：只修 Coasting actuation lease
  -> S4 candidate：仅保留被 4A/4B 证实需要的最小改动
  -> final candidate
```

每一步都输出相同 `metrics.json`，这样可以看出改善来自哪一层。某一步没有解释对应 RED，则回滚该步自己的改动，不继续叠加下一层实验。

### 10.2 focused build/test

至少构建并直接运行：

```powershell
& $cmakeExe --build $buildDir --config Release --target `
  cod_native_target_coordinator_tests `
  cod_native_bodylock_follow_controller_tests `
  cod_native_aim_dynamics_shaper_tests `
  cod_native_vector_intent_fuser_tests `
  cod_native_controller_tests

& "$buildDir\Release\cod_native_target_coordinator_tests.exe"
& "$buildDir\Release\cod_native_bodylock_follow_controller_tests.exe"
& "$buildDir\Release\cod_native_aim_dynamics_shaper_tests.exe"
& "$buildDir\Release\cod_native_vector_intent_fuser_tests.exe"
& "$buildDir\Release\cod_native_controller_tests.exe"
```

随后运行：

```powershell
& $ctestExe --test-dir $buildDir -C Release --output-on-failure
git diff --check
```

### 10.3 matched matrix

至少三 seed，ADS/BodyLock 两种模式，覆盖：

| Cohort | 必测原因 |
|---|---|
| stationary firing | 防止 S2 把 firing 扰动重新学成 velocity |
| firing center crossing | S2 主验收 |
| wrong-aim recovery | 保留 7 月 31 日已验收的 firing-disturbance 能力 |
| one-frame miss/reacquire | S3 主验收 |
| 12.5/36/65/80 ms occlusion | S1 身份与施力分离 |
| ordinary moving + true reversal | 防止变 lazy 或错过真实反向 |
| close static + POV movement | S4A 系统自振 |
| close moving + POV movement | S4A 跟随反例 |
| close strong cooperative manual | S4B 主验收 |
| far cooperative manual | S4B 远距反例 |
| opposing and full manual escape | 用户所有权安全边界 |
| ADS->BodyLock、TargetChanged | 保留 Task 1-4 已验收合同 |

统一 guardrail：

- ordinary tracking 不能恶化超过约 `2%`。
- overshoot area / continued push 不能恶化超过约 `5%`。
- false stop / false interruption / false target hold 不得新增。
- 不得用降低 max force 或全局 strength 换取通过。
- 每个 improvement 必须能回指到对应 RED 测试和首发异常层。

## 11. Task 6：candidate runtime、覆盖与实机验收

本计划阶段不覆盖 runtime。只有源码测试和 matched benchmark 全部通过，并得到用户明确的 build/覆盖指令后才执行：

1. 先将当前 `7D15...2012` runtime 复制到新的时间戳 backup 目录。
2. 构建独立 final candidate，记录 executable SHA-256、source diff fingerprint、config/engine hash。
3. 停止或替换 runtime 前再次验证目标路径，禁止删除旧 backup。
4. 覆盖用户当前 runtime，并在新日志 session 中确认实际加载的是 candidate hash。
5. 用户按四段视频顺序复测，每段只看最后一个人物；Situation 4 仍分系统自振和人工刻意加力两个动作。

实机验收观察：

- S1：缩回掩体后是否立即开始卸力，是否仍在约 70 ms 后拉飞。
- S2：开火、目标穿越中心或击杀瞬间是否还有纯 AI 单侧猛拉。
- S3：一帧丢失/击杀后的前 `50 ms` 是否有 AI off/on 或向下回弹。
- S4A：近距离不加人工时是否出现一段多次左右/上下摆荡。
- S4B：人工 `65-90%` 同向推、释放、反向修正时是否仍有弹力绳感；full escape 是否仍干净。

只有 live feel 与新日志共同通过，才能写 acceptance 和同步 `.agent-context/`。用户主观感觉必须记录为用户确认，不能把 AI 推断提前写成 accepted decision。

## 12. 回滚、提交与报告合同

### 12.1 回滚边界

- 每个 situation 的改动要能按文件/commit 独立识别。
- 不使用 `git reset --hard` 或 `git checkout --` 回滚共享工作区。
- 失败实验只通过针对性反向 patch 撤销自己新增的行，并保留所有 benchmark artifact。
- runtime 回滚只复制已记录 hash 的 backup，不重建“看起来相同”的旧 executable。

### 12.2 建议提交拆分

1. `test: add four control-chain incident repro fixtures`
2. `fix: keep firing-time fresh position authoritative`
3. `fix: align same-target reacquire fusion lifecycle`
4. `fix: separate coast identity hold from actuation lease`
5. `fix: bound proven close-bodylock oscillation source`（仅在 Task 4 需要时）
6. `docs: record four-situation verification`

如果现有 dirty source 不适合逐 commit，至少要求最终 diff 能按以上逻辑分组，并且不提交无关用户改动。

### 12.3 执行 session 的完成报告必须包含

1. 四个 RED 如何复现，baseline 数值是什么。
2. Situation 2、3 的首发 bug 层和具体修改合同。
3. Situation 1 选择的 Coasting release 参数以及 36 ms occlusion 反例结果。
4. Situation 4A 首次 reversal 到底出现在 plan/request/shaper/fuser/recoil 的哪一层；4B 是否还需要改 fuser。
5. 每个 candidate 的相关 diff、测试命令、测试结果和 matched metrics。
6. ordinary/far/moving/manual guardrail 的变化。
7. final candidate/runtime SHA-256；若未覆盖，明确写“未部署”。
8. 未解决问题、残余风险和下一次 live 测试动作。

## 13. 针对现有 Codex task 的协调方式

现有 task `019fbaf1-359a-7041-b782-333c7f2ac698` 与本任务使用同一工作目录，可以直接读取本计划和修改同一 checkout。执行时采用单写者规则：

- 该 task 可以作为实现者，按 Task 0 -> S2 -> S3 -> S1 -> S4 顺序工作。
- 当前主 task 负责计划、review、验收和最终 runtime 身份核对。
- 两个 task 不得同时编辑同一文件；实现者开始写代码后，review 方保持只读，直到它明确交回 checkpoint。
- 建议实现者先完成 Task 0、S2、S3 后停在 checkpoint，交回 RED/GREEN 与 focused tests；验收通过后再进入 S1、S4。
- 未获得明确 build/覆盖指令时，实现者不得替换 runtime、终止用户进程或删除 backup。
