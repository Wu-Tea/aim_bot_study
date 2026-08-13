# 瞄准辅助“不抢准星”控制设计（候选）

**日期：** 2026-08-13  
**状态：** proposed；用于讨论、RED 回归和后续实现，不是已接受决策  
**范围：** 默认 native C++ Vision → gamepad 链路  
**生产代码：** 本文不修改生产行为  

## 0. 目标与结论

本设计回答一个具体产品问题：

> 在 AI 仍能快速定位、持续跟枪和补足玩家操作的前提下，什么情况下必须把准星决定权留给玩家？

候选答案是：

> **玩家拥有目标 `I`、落点 `D` 和退出的否决权；AI 只在“同一个已认可目标 + 当前可行动区域 `R`”成立时拥有执行权。**

“不抢”不是玩家一动 AI 就降权。“辅助大”也不是始终输出大力。正确关系是：

- 玩家和 AI 对 `I/R/D` 一致时，AI 可以按完整配置能力快速执行；
- 玩家正在修正同一目标的 `D`、支持高速追踪、换人或退出时，AI 不得反向削弱该动作；
- 只能记住目标身份、但没有当前可行动 `R` 时，可以保留身份记忆，但目标瞄准力为零；
- 直接证据恢复且确认是同一物理人物时，应恢复跟随，不重新触发一次新目标吸附。

本设计不增加第二个 selector、第二个生命周期 owner 或第二个最终输出 owner。它只明确现有 owner 之间的许可边界。

---

## 1. 与既有项目合同的关系

本设计延续以下已接受约束：

1. `VisionTargetSelector` 唯一拥有目标身份 `I`；
2. `TargetCoordinator` 唯一拥有可打区域 `R` 中的落点 `D`；
3. `AssistControlStateMachine` 唯一拥有最终目标瞄准输出 `T`；
4. manual 与 AI 不是两股物理力相加，AI 提交的是期望总输出；
5. 目标通过 ADS 准入后使用完整配置权威，不按 cue、可见度或可靠度连续降权；
6. 无效或陈旧目标不表示成“较弱的有效目标”；
7. cue 只能短时延续同一身份的瞄准几何，不能创建新身份或获得开火权限；
8. 同一输入样本只能有一种身份语义，不能同时修改 `D` 和为另一个 `I` 投票；
9. 多目标换人由 selector/coordinator 根据可信替代目标处理，最终输出层不选择目标。

本文补充四个缺失语义：

- 身份记忆与动作授权分离；
- 连续手势允许在所有权状态变化后改变下一样本的 purpose；
- 同目标修正和高速追踪输入获得明确的逐轴反向保护；
- 同一身份恢复与新身份获取采用不同生命周期。

---

## 2. 事件证据边界

### 2.1 已确认事实

- **F1：运行身份。** 三段录像对应会话 `20260812T182729Z_49124_1`，提交 `f72dc75e22bace368ce6b4e631ed0ecc5c466ba6`，运行二进制 SHA-256 为 `945e64023e0b8098a76583ae42490e692823383cbd30e8e2b70beff2553148ae`。
- **F2：下降/遮挡事件。** 关键窗口中一个物理交火出现 4 个 controller target ID；有目标覆盖约 51.0%，AI 活跃约 42.5%。玩家向下时存在 AI 向上的提案，但最终 `T_y` 没有反转或削弱关键向下输入。
- **F3：下降事件的 D 语义。** 关键窗口的 `D` 来源保持 `vision_default/cue_carried`，没有进入 `user_corrected`；连续手势仍被保留为 acquisition purpose。
- **F4：横移事件。** 一个物理横移目标出现 5 个 controller target ID；直接观察约 50.8%，AI 活跃约 30.3%；存在约 117、141、164 ms 的无目标/无动作空窗。
- **F5：横移人工受损。** 某些横轴样本中人工与最终输出同号，但有效人工追踪量仍被反向 AI 衰减，最明显样本只保留约 67%。
- **F6：半身位/双目标事件。** 两个相邻交火窗口分别出现 13 与 17 个 controller target ID；AI 活跃约 46.1% 与 34.2%，表现为反复捕获、释放和再次捕获。
- **F7：当前已有动力学整形。** 录制提交已经存在 `AimDynamicsShaper`；默认 rise/decay 为 64/48 每秒，单 tick 最大变化 0.08。问题不能归因成“完全没有 slew limiter”。
- **F8：证据完整性限制。** 会话仍残留 `.active`，遥测尾部有 1 条截断 JSON；上述事件窗口可读，但会话不能作为 clean-close 运行证明。

### 2.2 由事实支持的推断

- **I1：** 第一段的主要控制语义缺陷不是最终融合器把向下输入反转，而是目标建立后，同一连续手势没有及时变成 `D` 修正。
- **I2：** 第二段首先是物理身份连续性和动作覆盖不足；统一降低 AI 权威或大幅降低 slew 只会增加追踪落后。
- **I3：** 第三段的“半黏不黏”主要来自动作授权在 full/reject 和新 acquisition 之间抖动，而不是一个稳定、合理的低权威模式。
- **I4：** 低证据时把目标变弱，不如把“可能还是同一个人”的身份记忆与“当前可从哪里出力”的动作授权分开。

### 2.3 当前未知

- **U1：** 第一段最终画面“拉不下去”中，控制输出、游戏响应曲线、武器动画和视觉遮挡各自占多少；需要控制输出到真实画面位移的桥接证据。
- **U2：** 当前轻量 detector-box `R` 在头皮位、枪模遮挡和姿态变化下与真实可命中区域的偏差。
- **U3：** 身份被动记忆时长、人工抢回 dwell、换人角度和各类 P95 门禁的最终数值；不得从候选输出倒推。
- **U4：** 身份稳定后是否仍需要显式 future-`R`/lead；在身份与动作覆盖修复前不授予新的预测动作权限。

---

## 3. 控制模型：记住不等于可以出力

### 3.1 三个身份/几何量

在现有 `I/R/D/T` 外，不增加新 owner，只明确 selector 内部和公开计划的区别：

| 量 | 含义 | 是否允许目标瞄准力 |
|---|---|---|
| `I_mem` | selector 认为短期内可能仍是同一物理人物的关联记忆 | 否 |
| `I_act` | 当前通过准入、可以发布动作计划的身份 | 只有同时存在 `R_act` 才允许 |
| `R_act` | 当前允许控制器据此行动的区域；直接新鲜 R 或合同允许的同代 cue R | 是 |

核心不变量：

```text
target_aim_force_authorized =
    ADS_request
    && I_act exists
    && R_act valid
    && (R_act is fresh direct geometry
        || R_act is bounded same-generation cue geometry)
```

当该表达式为 false：

- target aim contribution 为零；
- 最终目标瞄准层立即使用 manual 基线；
- `I_mem` 可以继续存在，但不能发布旧坐标、开火或弱吸附；
- recoil 等后置、独立权限不由本表达式重新定义。

这不是把一个目标从 100% 平滑降到 20%，而是把两个问题分开回答：

1. 这个人可能还是不是刚才的人？
2. 现在是否有足够几何证据从某个位置出力？

### 3.2 状态机

```mermaid
stateDiagram-v2
    [*] --> ManualNoIdentity

    ManualNoIdentity --> ActionableDirect: "selector admits new I with fresh R"
    ActionableDirect --> ActionableCue: "direct R lost; valid same-generation cue"
    ActionableDirect --> RememberedPassive: "R no longer actionable; identity memory retained"
    ActionableCue --> ActionableDirect: "same I direct R returns"
    ActionableCue --> RememberedPassive: "cue expires or becomes invalid"
    RememberedPassive --> ActionableDirect: "same physical I reacquired with fresh R"
    RememberedPassive --> ManualNoIdentity: "memory expires / death / friendly / ADS release"

    ActionableDirect --> HandoverSeek: "boundary intent + aligned credible alternative"
    ActionableCue --> HandoverSeek: "boundary intent + aligned credible alternative"
    HandoverSeek --> ActionableDirect: "selector confirms different I"
    HandoverSeek --> ManualNoIdentity: "ADS release or search canceled"

    ActionableDirect --> ManualReclaim: "explicit hard reclaim without alternative"
    ActionableCue --> ManualReclaim: "explicit hard reclaim without alternative"
    ManualReclaim --> ManualNoIdentity: "gesture neutral/released; next gesture may acquire"

    ActionableDirect --> ManualNoIdentity: "identity invalid / ADS release"
    ActionableCue --> ManualNoIdentity: "identity invalid / ADS release"
```

### 3.3 每个状态的可见行为

| 状态 | 身份 | 几何 | 目标瞄准输出 | manual | 开火 | 恢复语义 |
|---|---|---|---|---|---|---|
| `ManualNoIdentity` | 无 | 无 | 0 | 完整 | 无 AI 开火 | 新目标是 acquisition |
| `ActionableDirect` | `I_act` | 新鲜直接 `R_act` | 完整配置能力，按残余求解 | 可修正 D/追踪/退出 | 可按现有安全合同 | 正常 Track |
| `ActionableCue` | 同一 `I_act` | 同代、有限期 cue R | 只允许现有 cue 包络，不盲增 | 可修正 D/退出 | 禁止 AI 开火 | 直接证据回来无新 acquisition |
| `RememberedPassive` | 仅 `I_mem` | 无可行动 R | 0 | 完整 | 禁止 AI 开火 | 同人回来恢复 Track，不吸一次 |
| `HandoverSeek` | 旧 I 不再有动作权 | 候选集由 selector 处理 | 0 | 完整朝候选移动 | 禁止旧目标开火 | 新 I 确认后 acquisition |
| `ManualReclaim` | 当前 ADS 动作所有权已释放 | 不使用旧 R | 0 | 完整 | 只保留物理开火语义 | 手势结束后才允许重新获取 |

语义权威是离散的；输出连续性仍由现有动力学整形和状态边界保证。不得用一个连续置信度把 `RememberedPassive` 变成弱吸附。

---

## 4. 连续手势的逐样本 purpose

### 4.1 当前问题

当前实现把一次连续手势从开始到释放/反转固定为一个 purpose。这样保护了“同一输入不能同时选 I 和改 D”，但把保护范围扩大到了整段手势。

结果是：手势开始时没有目标，所以是 `AcquireTarget`；几毫秒后目标已经建立，玩家仍然持续下拉，但整段手势仍不能修改 D。

### 4.2 候选规则

保留“一个样本只有一种语义”，但允许**下一控制样本**在所有权状态变化后采用新 purpose：

```text
if handover_or_reclaim_already_armed:
    effective_purpose = HandoverTarget / ManualReclaim
else if actionable target was owned before this sample:
    effective_purpose = CorrectCurrentTarget
else:
    effective_purpose = AcquireTarget
```

关键时序：

1. 样本 `k` 以 `AcquireTarget` 被 selector 用来选择新 I；
2. 样本 `k` 不再被 coordinator 二次用来修改新目标 D；
3. 新 I 在样本 `k` 结束时成为已拥有目标；
4. 样本 `k+1` 即使属于同一连续手势，也转成 `CorrectCurrentTarget`；
5. 只有 D 到边界并满足后述换人/抢回条件后，未来样本才进入 handover/reclaim。

这把“不得同样本双重解释”与“不得整段手势改变语义”分开。前者保留，后者取消。

### 4.3 owner 不变

- `IntentFilter` 继续唯一处理死区、偏置、噪声和手势 phase；
- effective purpose 使用调用方已经传入的 `target_owned`、`handover_requested` 状态在样本边界更新；
- selector 只消费 Acquire/Handover；
- coordinator 只消费 Correct 来更新 D；
- 不增加一个并行意图分类器来决定在线控制权。

操作 taxonomy（tracking、lead、recoil、flick 等）可以作为 shadow/诊断标签，但不能覆盖这里的确定性 ownership 路由。

---

## 5. 同目标修正、高速追踪与换人的区分

### 5.1 默认：拥有 I 时，人工是在修改 D

当 `ActionableDirect/ActionableCue` 已经拥有目标，且尚未进入 handover/reclaim：

- 任何经过 `IntentFilter` 的有效轴输入都首先表示同一 I 内的 D 修正；
- D 按轴在 `R_act` 内移动；
- correcting axis 获得反向保护；
- 开火状态不取消 D 修正；
- X/Y 可以拥有不同的修正状态。

### 5.2 D 到边界后不能立刻等同于换人

D 到达 R 边界后，未来样本按以下优先级解释：

1. **运动支持（pursuit support）**  
   当前 I 有新鲜、可信的屏幕速度，人工方向与目标运动方向一致，且没有方向匹配的可信替代目标：继续视为同目标追踪。它不累计换人 dwell，也不得被反向 AI 衰减。

2. **候选对齐换人（handover）**  
   存在可信替代人物，人工方向与替代候选几何一致，并持续达到校准 dwell：旧目标动作权立即归零，进入 `HandoverSeek`。

3. **明确人工抢回（manual reclaim，产品策略待确认）**  
   没有可信替代目标，人工强烈、持续指向当前/运动区域之外，且不属于运动支持时，可以进入 `ManualReclaim`。它与“单一可信目标下 AI 可保持主导”的既有策略存在需要澄清的边界：本候选只主张将其作为独立、显式、可校准的紧急退出手势，不能把普通一次反向或过冲等同于抢回。若用户不接受单目标硬抢回，则此分支仅由 ADS release/target invalidation 触发。旧目标一旦进入该状态，在同一手势内都不能重新吸回。

4. **证据不足**  
   无法可靠判断 target velocity 或候选集合时，默认保护人工：不增加反向阻尼，不凭“误差发散”判玩家错误。

`pursuit support` 只是一条**人工保护规则**：它禁止错误退出和错误衰减，不授权预测、coast 或旧坐标出力。只有身份连续性修复后，才能另行在 shadow 中评估 future-R/lead。

### 5.3 半身位和头皮位

对部分可见目标，不使用“置信度越低、吸力越弱”的默认映射：

- 几何生产者能给出可信可打 `R_act`：目标正常准入，ADS 按完整配置能力执行；
- 只能关联身份、不能给出可信可打 R：进入 `RememberedPassive`，目标瞄准力为零；
- 同一目标重新出现：恢复 Track，而不是创建新 acquisition；
- 真正换成另一个物理目标：才重置 D 并创建新的 acquisition。

当前 detector-box 派生 R 不是遮挡真值。本文只定义 R 的动作合同，不预先决定必须引入 Body/Pose、分割或哪种新模型。几何改进必须由第三个 RED 事件证明需要什么。

---

## 6. 被动期恢复时 D 如何处理

`RememberedPassive` 期间没有可行动 R，因此：

- 不在旧 R 上积分人工输入；
- 不保存一串待恢复后重放的 D 修正；
- manual 直接控制游戏视角。

同一 I 恢复直接 R 时分两种情况：

1. **被动期没有 material manual：** 保留遮挡前的 normalized D，并映射到新 R；
2. **被动期出现 material manual：** 不把准星拉回遮挡前 D。以当前准星相对新 R 的最近点作为恢复 D（clamp/rebase），承认玩家在被动期已经重新放置了准星。

两种情况都满足：

- identity epoch 不变；
- 不产生新的 acquisition id；
- 不重放旧 AI 输出；
- 新 R 首次有效时只恢复同目标 Track。

这既保留无操作短遮挡的连续性，也避免玩家已经手动移动后被旧 D 拉回。

---

## 7. 最终 T 的逐轴许可求解

记：

- `M`：物理 manual 基线；
- `F`：过滤后的意图方向，仅用于语义；
- `A`：AI 对该目标的期望总输出，不是附加力；
- `T`：目标瞄准层最终逐轴输出；
- `P_axis`：该轴的人工保护原因。

`P_axis` 取值：

- `none`
- `d_correction`
- `pursuit_support`
- `handover`
- `manual_reclaim`
- `explicit_overshoot_brake`

### 7.1 许可矩阵

| 条件 | 逐轴输出规则 |
|---|---|
| 无 `I_act/R_act` | `T = M` |
| handover 或 manual reclaim | `T = M` |
| D correction / pursuit，A 与 F 同向 | `|M| >= |A|` 时 `T=M`，否则 `T=A`；AI只补缺口 |
| D correction / pursuit，A 与 F 反向 | `T=M`；不得衰减有效人工 |
| 人工近零、目标可行动 | `T=A` |
| 明确 overshoot brake | 允许现有有上限、永不越零的阻尼；必须有独立几何触发 |
| 只有“方向相反”，无 brake 证据 | `T=M`；方向相反本身不证明玩家错 |

### 7.2 什么才算明确 overshoot brake

反向阻尼不得由 `sign(F) != sign(A)` 单独触发。至少需要：

- 新鲜直接 R；
- 当前 I 未发生关联不确定；
- 基于已校准相机响应估计，manual 延续会从接近侧穿过 R 的远侧；
- 当前轴不是 D correction、pursuit support、handover 或 firing-down；
- 没有方向对齐的可信替代目标。

在没有这组证据前，现有 35% 普通反向阻尼不能被视为天然正确；它只应保留为待 RED/实测验证的最大预算，而不是默认许可。

### 7.3 ADS 完整权威没有被削弱

这里保护的是玩家已被确定解释为 D 修正、追踪或退出的动作，不是按置信度降低 ADS：

- selector 准入后，A 仍使用完整配置响应；
- 玩家与 A 同向时，AI仍可把总输出提高到 A；
- 玩家近零时，AI完整执行 A；
- 只有 AI 想反驳一个已获授权的玩家动作时，该轴才被否决。

---

## 8. 三个录像场景的期望执行

### 8.1 快速下降到枪械遮挡处

事件序列：

1. 玩家在无目标时开始手势：`AcquireTarget`；
2. selector 准入人物和直接 R：进入 `ActionableDirect`；
3. 下一 controller tick，同一连续手势变成 `CorrectCurrentTarget`；
4. 玩家向下：D 向 R 下部移动，Y 轴获得 correction 反向保护；
5. 若 R 仍直接可信：AI围绕新 D 执行；
6. 若只有合规 cue R：进入 `ActionableCue`，D 可继续修正、禁止自动开火；
7. 若连 cue R 都不可信：进入 `RememberedPassive`，目标力为零，manual 下拉完整通过；
8. 同人重新出现：根据被动期是否有 manual 保留或 rebase D，恢复 Track，不重新吸一次。

这里“不抢”的可测结果不是只看 `T_y` 有没有反转，还要看：目标建立后 D 是否在下一 tick 承认向下动作，以及实际画面是否开始向下响应。

### 8.2 高速横向跑动

事件序列：

1. 同一个物理人物保持一个 identity epoch；短时识别变化不能生成一串新 acquisition；
2. 有直接或合规 cue R 时保持动作授权；没有 R 时只记忆、人工通过；
3. AI负责补足目标运动所需的横向总输出；
4. 玩家与目标运动方向一致的横向输入标记为 `pursuit_support`；
5. AI同向时继续补缺口，AI反向时不得衰减该人工输入；
6. 只有出现方向对齐的可信替代目标时，持续外推才成为 handover；
7. 身份和动作覆盖修复后若仍落后，再对 fresh same-identity future-R 进行 shadow 评估。

这里“不抢”不等于 AI 退出；恰恰相反，AI应持续补速，同时不惩罚玩家的有效追踪/提前量。

### 8.3 半身位、头皮位和两个目标

事件序列：

1. 对人物 A，只有身份关联但无可信 R：`RememberedPassive`，不产生半强度吸附；
2. A 的可信部分 R 出现：直接进入同一身份 Track；
3. A 短暂消失再回来：不重置 D、不重启 acquisition；有 material manual 时按当前放置 rebase D；
4. 人物 B 出现但玩家未表达换人：继续保持 A，不按分数自动切换；
5. 玩家把 D 推到边界并持续朝 B 的可信候选几何移动：进入 `HandoverSeek`，旧 A 目标力立即为零；
6. selector 确认 B：创建新 identity epoch、新 acquisition，D 重置到 B 的合法默认点；
7. 如果方向上没有 B：不得把内部 ID 重建当换人；强烈持续输入可进入 `ManualReclaim`。

---

## 9. 量化合同

### 9.1 量化单位

使用三层单位，禁止混为一个总分：

1. **物理人物 episode：** 由视频人工锚定同一个现实人物；内部 `target_track_id` 不是 ground truth；
2. **动作授权 segment：** Direct、Cue、Passive、Handover、Reclaim；
3. **手势/轴 segment：** Acquire、D correction、pursuit、handover、reclaim、brake。

### 9.2 控制权门禁

| 指标 | 正确定义 | 硬性质 |
|---|---|---|
| 同样本双重消费 | 一个样本是否既参与身份选择又修改 D | 必须为 0 |
| purpose 转换延迟 | I 在样本 k 建立后，持续手势何时变成 Correct | 应为下一控制样本；允许的实际时间由采样完整性校验 |
| D acknowledgement | correction 开始后，D 首次按同方向移动的延迟 | 独立门禁 |
| 人工保留率 | 受保护轴上 `sign(T)=sign(M)` 且 `|T|/|M|` | 不得低于 1（数值 epsilon 除外） |
| 无 R 目标力 | `R_act` 无效时 target aim contribution | 必须为 0 |
| 陈旧坐标出力 | 输出引用的 R/D 是否已过动作时限 | 必须为 0 |
| 错误新 acquisition | 同一物理人物恢复时是否新建 acquisition | 必须为 0 |
| 错误 handover | 无对齐可信替代目标时是否换 I | 必须为 0 |
| 旧目标回吸 | Handover/Reclaim 同一手势中旧 I 是否重新出力 | 必须为 0 |

### 9.3 辅助效果门禁

| 指标 | 定义 |
|---|---|
| identity continuity | 同一人工锚定物理人物 episode 内 identity epoch 数、重建数和无身份空窗 |
| actionable coverage | 人工标注“可见且可打”时间中 `R_act` 有效并获得目标动作授权的比例 |
| action chatter | Direct/Cue/Passive 状态往返次数和最短驻留时间 |
| actual region occupancy | 校准后的画面准星点是否位于 `R_act`；不得用本来就被 clamp 的 D 代替准星 |
| along-motion lag | 准星相对 R 沿新鲜目标速度方向的落后分量和持续时间 |
| pursuit support loss | 人工方向支持新鲜目标运动时，T 是否反而小于 M |
| same-I resume step | 同一身份恢复前后 target request/shaped/final 的不必要阶跃 |
| new-I acquisition time | 真正换人后从确认新 I 到进入新 R 的时间 |

### 9.4 “抢准星”不能再用简单同号率代表

必须同时报告：

- 是否反转；
- 是否衰减有效 manual；
- 是否改变 D；
- 是否使用了正确 I/R；
- 是否在 handover/reclaim 后继续使用旧目标；
- 输出是否造成预期方向的真实画面位移。

`target_final` 与 manual 同号只证明没有跨零，不证明没有抢。

### 9.5 counterfactual 的诚实边界

同一实战会话中的 M/T 可用于：

- 重算另一个 fuser 在**同一个已发生状态**下会提交什么命令；
- 比较命令级人工保留、反转、阶跃和授权违规。

它不能仅凭 M/T 推出“只有 M 时后续画面误差会怎样”，因为镜头、检测和玩家后续输入都会改变。闭环因果效果需要：

- 可校准 plant；或
- 确定性闭环 replay；或
- 匹配的 AI-on/AI-off 靶场、bot、AimLab A/B。

---

## 10. 三个 RED 回归合同

数值阈值只能来自已知故障、固定环境和后续接受手感。以下先冻结触发拓扑和零容忍不变量。

### R1：mid-gesture acquisition → downward D correction

**可见症状：** 目标在手势中途建立并快速向下时，玩家持续向下没有成为同目标 D 修正。

**最小表面：** native coordinator + intent + assist integration benchmark。

**固定触发：**

- ADS held；
- 手势在无目标状态开始，最初 purpose 为 Acquire；
- 手势不释放、不反转；
- 中途准入一个直接人物和 R；
- 后续存在 material downward manual；
- 覆盖 firing=false 与 firing=true；
- 随后插入 direct → cue → no-actionable-R 生命周期边界。

**硬 oracle：**

- `O1`：选择 I 的样本不修改 D；
- `O2`：下一 controller 样本 effective purpose 为 Correct；
- `O3`：D 在该样本沿向下方向移动，source 为 user correction；
- `O4`：受保护 Y 轴不反转、不衰减 manual；
- `O5`：无 `R_act` 时 target aim contribution 为零；
- `O6`：同 I 恢复不新建 acquisition。

**counterfactual：**

- 始终无目标：purpose 保持 Acquire，D 不存在；
- target 已拥有后才新开手势：直接进入 Correct；
- 真正替换成另一个 I：D 重置且允许新 acquisition；
- manual 为零：D 不发生 user correction。

**known-bad 预期：** 当前整段手势 purpose 冻结，至少 `O2/O3` 为 RED。

### R2：same-person fast lateral pursuit continuity

**可见症状：** 一个高速横移人物被反复重建身份，AI长期缺席，并在部分样本中削弱有效人工追踪。

**最小表面：** selector/coordinator/assist 的原生序列 replay；若原始候选信息不足，则使用显式 covariate 的 native integration fixture，不伪造闭环画面结果。

**固定触发：**

- 视频人工锚定一个物理人物；
- 新鲜直接观察中允许 source observation ID 变化；
- 插入事件中已经观察到量级的短缺口；
- 一个 credible target，无方向对齐的替代人物；
- fresh target velocity 横向且 manual 同向；
- ADS、无 recoil 作为主夹具；firing 另做边界覆盖。

**硬 oracle：**

- `O1`：整个物理人物 episode 只有一个 identity epoch；
- `O2`：短时 passive → direct 恢复不产生新 acquisition；
- `O3`：无 R 时目标力为零，有合法 R 时没有无法解释的 AI-idle；
- `O4`：pursuit-support 轴 `|T| >= |M| - epsilon`；
- `O5`：单目标、运动同向人工不触发 false handover/reclaim；
- `O6`：任何 cue 行为保持同代、有限期、无开火。

**counterfactual：**

- 人物瞬移/外观与关联证据跨过确定的新身份边界：必须创建新 epoch/acquisition；
- 移除短缺口：Passive 转换数应为零；
- manual 改为朝可信替代人物：允许 handover；
- 移除 R 且无 cue：目标力必须归零。

**known-bad 预期：** 当前事件会在 identity epoch、新 acquisition、action coverage 或 manual retention 中至少一项 RED。

### R3：partial visibility + same-I resume + explicit B handover

**可见症状：** 半身/头皮目标反复吸附和放开；同一人恢复被当成新目标；双目标时定位与换人失败。

**最小表面：** 带人工物理身份标签的 native lifecycle integration fixture。

**固定触发：**

- 人物 A：direct partial R → identity-only/no-R → same-A direct R；
- 被动期分别覆盖 manual=0 和 material manual；
- 人物 B 后续成为可信替代候选；
- 玩家先不朝 B，随后明确朝 B；
- ADS held；cue 与 direct/no-R 边界显式声明。

**硬 oracle：**

- `O1`：identity-only/no-R 段目标力和 AI fire 均为零；
- `O2`：same-A 恢复保持 identity epoch，不新 acquisition；
- `O3`：被动期 M=0 时保留 normalized D；
- `O4`：被动期有 material manual 时按当前准星 rebase D，不拉回旧 D；
- `O5`：玩家未朝 B 时 A 不因分数自动切换；
- `O6`：对齐 B 的 handover 被确认后，旧 A 目标力同 tick 归零；
- `O7`：B 获得新 identity epoch、新 acquisition，且不继承 A 的 D；
- `O8`：不存在可信 B 时，内部 ID 变化不得被当成换人。

**counterfactual：**

- A 的身份关联证据明确失败：回来后必须是新 acquisition；
- B 只有 rejected/friendly/corpse 证据：不得触发 handover；
- 松开 ADS：无条件回 ManualNoIdentity；
- cue 超时：不得继续使用 cue R。

**known-bad 预期：** 当前事件会在 same-I acquisition count、无 R 出力、D resume 或 candidate-aligned handover 中形成 RED。

---

## 11. 最小实现映射

在 RED 之前不改生产代码。RED 建立后，候选实现应尽量局部化：

### 11.1 `VisionTargetSelector`

- 保留一个不携带动作坐标的 `identity_epoch/I_mem`；
- 区分“同 I 恢复”和“新 I 获取”；
- 只有 direct 或合同允许的同代 cue 才发布 `I_act/R_act`；
- 在候选集合中完成 alternative-aligned handover；
- 不让 raw detector box count 或内部 source ID 抖动直接创建新物理身份。

### 11.2 `IntentFilter`

- 保留连续滤波和 gesture phase；
- purpose 不再从 onset 冻结到整段结束；
- 在 `target_owned/handover_requested` 状态边界后，从下一样本重新派生 effective purpose；
- 不引入在线“用户对错”或身体状态诊断作为控制授权。

### 11.3 `TargetCoordinator`

- 继续唯一更新 D；
- direct/cue 可行动时处理逐轴 D correction；
- passive 时不在旧 R 上积分 manual；
- same-I resume 根据 passive 期间 material manual 选择 retain/rebase D；
- 发布 `manual_protection_reason`、actionability 和 transition reason；
- pursuit-support 首先只作为防误退出/防衰减保护，不创建预测 R。

### 11.4 `AssistControlStateMachine`

- 保持唯一 T owner；
- 按第 7 节许可矩阵求解；
- correction/pursuit/handover/reclaim 轴禁止 generic opposing damping；
- 只有 coordinator 显式证明 `explicit_overshoot_brake` 时才允许有上限阻尼；
- target actionability 失效时同 tick 使用 manual，不保留旧目标力。

### 11.5 `AimDynamicsShaper`

- 第一阶段不新增第二个 slew limiter，也不先改 64/48/0.08；
- 先统计真实 requested/shaped/final 的 P50/P95/P99 delta 与状态边界；
- 只有 RED 证明现有包络造成速率错位时，才用不变 fixture 冻结候选参数；
- 不能用大幅降速掩盖 identity/action chatter。

### 11.6 建议新增或明确的遥测

- `identity_epoch_id`
- `remembered_identity_id/age_ms`（不携带可行动坐标）
- `target_actionability = none/direct/cue`
- `actionability_transition_reason`
- `same_identity_resume`
- `effective_intent_purpose`
- `manual_protection_reason_x/y`
- `overshoot_brake_authorized_x/y`
- `passive_material_manual_seen`
- `D_resume_reason = retained/rebased/reset`

这些字段描述已有 owner 的决定，不创建新的决定者。

---

## 12. 落地顺序

1. **先建立 R1/R2/R3 的 known-bad RED 包。** 固定运行身份、触发、covariates、oracle 和反事实；不以 candidate 输出调整门禁。
2. **修正逐样本 purpose。** 这是第一段最小、已定位的 owner 语义缺陷。
3. **加入 identity memory / actionability 分离。** 先消除同人新 acquisition 和 full/reject chatter，不添加预测。
4. **加入人工保护原因和许可矩阵。** 让 D correction/pursuit 不再被 generic opposing damping 衰减。
5. **复核真实 R。** 用第三段 RED 判断轻量几何能否满足头皮/半身位；失败后再选择几何技术。
6. **最后评估横移 future-R/lead。** 仅在 identity、action coverage、manual protection 已 GREEN 后进行 shadow 与闭环 A/B。

每一步都要求：同一个 RED 从失败变通过；三段其他事件不退化；完整原生测试通过；运行二进制 SHA-256 与结果报告留档。

---

## 13. 明确非目标

- 不通过统一降低 AI gain 来制造“不抢”的错觉；
- 不新增 0.5–1.0/s 的通用硬 slew cap；
- 不把低置信度映射成弱磁铁；
- 不允许 `direction_trust` 或 operation-template mismatch 默认反向接管玩家；
- 不用当前 I/D 的误差发散循环证明“玩家错了”；
- 不恢复通用 coast、旧目标坐标投影或输出 carry；
- 不让 operation classifier 成为新的意图、目标或最终输出 owner；
- 不用 `desired_aim ∈ R` 冒充实际准星位于 R；
- 不用同一条闭环实战轨迹中的 M/T 冒充 AI-off 因果结果；
- 不把三个事故平均成一个总分。

---

## 14. 待用户确认的产品选择

本文提出但尚未被用户确认的核心选择有四项：

1. **低证据语义：** 保留身份但停止目标力，而不是连续弱化目标；
2. **连续手势语义：** 新 I 建立后的下一 controller 样本即可从 Acquire 转为 Correct；
3. **反向 AI 许可：** D correction 和 pursuit-support 轴完全禁止反向衰减；普通反向阻尼必须升级为显式几何 overshoot-brake 才能使用。
4. **单目标抢回边界：** 是否接受“D 到边界后持续、强烈、非 pursuit 的区域外动作”作为单目标下的紧急 manual reclaim；若不接受，单目标只能由 ADS release 或身份失效释放。

确认后应将它们写成 proposed/accepted decision，再开始 RED fixture 和生产实现。未确认前，本文只是候选设计。
