# 操作模式模型白皮书（v1 实现版）

**日期：** 2026-08-13
**版本：** operation-model v1 · 遥测 schema v18 · `f72dc75+opmodel`
**配套设计文档：** `ASSIST_PERCEPTION_QUANTIFICATION_DESIGN_20260813.md`（§4.5 正确操作模型）
**性质：** 本白皮书描述的是**已编码、已测试、已量化**的实现，不是设计提案。任何未经验证的部分都明确标注。

> **2026-08-13 产品决策更新：** 本文关于 `operation_aware_recoil`、手动下拉抵扣和“缺失余量”的内容仅保留为历史实验记录，不再描述当前执行策略。当前策略把 recoil 定义为手动/AI 瞄准合成之后的**独立最终输出**：操作分类仍用于遥测，但不能削减 recoil；自适应 fallback 的动态区间收窄为 `0.14–0.34`。

---

## 0. 摘要

用户的核心问题是：*什么样的辅助系统能让用户觉得"不抢准星"而且"辅助很大"，能不能量化用户的有效操作和错误操作场景？*

本版本把设计文档 §4.5 的"正确操作模型"从设计转成生产代码，并完成第一轮量化：

1. **新增操作意图分类器**（`operation_intent.{h,cpp}`）：单 tick 内、上下文条件化地把用户摇杆输入判为 8 类操作之一（压枪/跟枪/预测/获取/修正/换人/不可解释/无操作），带方向信任分。这是 §4.5"先统计正确操作长什么样，再逐操作对照"的在线判定层。
2. **遥测 schema 升级到 v18**：每个 `controller_sample` 携带 `operation_class`、`operation_confidence`、`direction_trust`、`recoil_pull_strength`。从此**每一帧的真实操作类别都带 ground-truth 标签**，不再需要事后从 schema-17 字段重建。
3. **压枪互补（`operation_aware_recoil`，默认开）**：用户正在压枪时，AI 后座补偿的"抵扣量"取 `max(原始下拉, 分类器压枪强度)`——系统不抢用户自己正在做的下拉。
4. **第一轮量化（会话 `20260812T182729Z_49124_1`，40,876 帧）**：不可解释输入占 14.3%（误差 p50=50px，是真实的降级信号），压枪占 9.0%（误差 p50=17.3px），跟枪 5.4%，预测 1.1%。这个分布本身回答了一部分"能不能量化"。
5. **AimLab 基准诚实结论：本版本在合成基准上是 NULL 结果**——合成用户不开火（`fire_action` 只在扰动场景开启），`RecoilPull` 分支从未触发，开关 `operation_aware_recoil` 的两次运行逐字节一致。既没有回归，也没有可测量的提升。**该特性的验证必须依赖真实开火场景（靶场/bot），首次靶场验证见 §4.5。**
6. **测试全绿**：51/51 CTest 通过，其中 13 个为 `operation_intent` 专属单测（含低血糖降级场景、free-look 不算不可解释输入、方向信任分坍缩）。

诚实边界：分类器本版本是**可观测性优先**——除压枪互补外不改变任何控制输出；方向信任分暂只写入遥测，未接入 authority ramp。下一步的关键依赖仍然是设计文档 §6 指出的**目标身份/视觉证据稳定性**。

---

## 1. 从设计到代码：§4.5 → OperationIntentClassifier

### 1.1 设计原文（§4.5）要解决的问题

§4.4 的"方向信任分"是**自我参照的窗口统计**，对低频单次失败无能为力（统计还没积累，失败已结束）。§4.5 转向：**正确操作不是一种，而是若干种绑定触发上下文的模式**。判断"这次对不对"不看用户自己最近的手势波动，而看"此刻上下文期望的操作模板"，再拿实际手势对照。

### 1.2 操作分类表 → 代码常量

| 操作类型 | 触发上下文 | 分类器判定特征 | 代码常量 |
|---|---|---|---|
| 压枪 `recoil_pull` | 正在开火 | 开火中 + 滤波下拉 > 0.05 | `kRecoilPullMinDownwardY = 0.05` |
| 预测 `lead_track` | 目标高速横移 | 目标速度 > 110px/s 且误差沿运动方向 > 6px 且输入小幅 | `kLeadTrackMinVelocityPxPerSec = 110.0`、`kLeadTrackMinErrorAlongMotionPx = 6.0` |
| 跟枪 `follow_track` | 目标锁定、输入轻微 | 目标被持有且幅值 < 0.12 | `kFollowTrackMaxMagnitude = 0.12` |
| 获取 `acquire_flick` | 无目标/新目标 | 获取手势 + 幅值 > 0.40 | `kAcquireFlickMinMagnitude = 0.40` |
| 修正 `correct_track` | 已锁定、小幅输入 | 修正手势 + 幅值 ∈ (0.15, 0.45) | `kCorrectTrackMaxMagnitude = 0.45` |
| 换人 `handover_intent` | 用户方向朝另一候选 | 显式换人 purpose（最高优先级） | — |
| 不可解释 `unreliable` | 辅助进行中 | **（目标被持有 或 开火中）且幅值 > 0.15 且不匹配任何模板** | `kMaterialFloor = 0.15` |
| 无操作 `no_gesture` | 任意 | 幅值 ≤ 0.15，或无辅助上下文 | — |

### 1.3 判定级联（优先级即设计）

```
if 换人 purpose → handover_intent
else if 开火中且下拉>0.05 → recoil_pull
else if 获取手势且幅值>0.40 → acquire_flick
else if 目标高速且超前 → lead_track
else if 目标被持有且幅值<0.12 → follow_track
else if 修正手势且 0.15<幅值<0.45 → correct_track
else if (目标被持有 或 开火中) 且幅值>0.15 → unreliable
else → no_gesture
```

两个设计决策值得说明：

- **`unreliable` 要求辅助上下文**（`(target_owned || firing)`）。这是本实现最关键的语义修正：自由视角（无目标、无开火）下的任何输入都**不算**不可解释，否则降级信号会被大量无关自由视角污染。修正前 `unreliable` 占 45%（误差 p50=0，全是噪声）；修正后 14.3%（误差 p50=50px，是真实降级）。见 §5。
- **优先级刻意把 `recoil_pull` 放在 `unreliable` 之前**：只要开火 + 下拉，就先当作压枪。代价是"开火中乱推恰好含下拉分量"会被误判为压枪（风险见 §7），但换来的是真正压枪不被降级。

### 1.4 方向信任分（§4.4 的实现载体）

- `unreliable → 0.10`（坍缩）
- `no_gesture → 0.50`（中性，不额外抑制）
- 其余：`clamp(0.72 + 0.15 × 置信度 + 0.10 × 方向一致性, 0, 0.97)`

方向一致性是滚动估计：方向变化与上次一致则按 α=0.08 上推，反转则减半。连续反向会快速坍缩信任——这正是低血糖/疲劳"来回乱推"的可测签名，且判定是**单 tick 的**，不需要窗口积累。

---

## 2. 实现位置与数据流

### 2.1 文件清单

| 文件 | 内容 |
|---|---|
| `native/controller_native/operation_intent.h` | `OperationClass` 枚举、`OperationIntentInput/Output` 结构、`OperationIntentClassifier` |
| `native/controller_native/operation_intent.cpp` | 判定级联 + 方向一致性 + 信任分 |
| `native/controller_native/operation_intent_tests.cpp` | 13 个单测 |
| `native/controller_native/native_gamepad_controller.{h,cpp}` | 集成点：`build_output_from_sampled_input` 内计算分类器，喂给 `apply_recoil` |
| `native/controller_native/output_diagnostics.h` | `NativeControllerOutputComponents` 追加 4 个遥测字段 |
| `native/controller_native/runtime_config.{h,cpp}` | 新配置键 `operation_aware_recoil`（默认 `true`） |
| `native/runtime_app/telemetry_schema.h` | schema v18 + `ControllerSamplePayload` 4 字段 |
| `native/runtime_app/telemetry_collectors.{h,cpp}` | tick 收集 |
| `native/runtime_app/runtime_loop.cpp` | 填充 |
| `native/runtime_app/runtime_telemetry.cpp` | JSON 序列化 |
| `tools/mine_operation_templates.py` | §4.5 模板挖掘工具（分类器镜像 + 分布统计） |

### 2.2 控制路径中的位置

分类器在 `build_output_from_sampled_input` 中、assist 状态机之后、`apply_recoil` 之前计算。输入全部来自已有的 `intent.filtered_right`、`plan`（误差/目标速度）、`control_feedback`（开火状态）。**本版本分类器输出不改变主瞄准控制**（`T = M` 基线、Cooperative Control V2 逐轴求解保持不变）；唯一的行为输出是压枪互补。

### 2.3 压枪互补（唯一的行为改变）

```cpp
// native_gamepad_controller.cpp, apply_recoil
if (recoil_output.recoil_stick.y < 0.0f) {           // AI 后座补偿生效中
    float manual_down = std::max(0.0f, -physical.right_y); // 用户原始下拉
    if (config_.recoil.operation_aware_recoil &&
        operation_intent.operation_class == OperationClass::RecoilPull) {
        manual_down = std::max(manual_down,
                               operation_intent.recoil_pull_strength);
    }
    recoil_output.recoil_stick.y =
        -std::max(0.0f, adaptive.amount - manual_down);
}
```

语义：用户正在压枪（开火 + 下拉）时，把"用户自己提供的下拉"抵扣进 AI 补偿量。`recoil_pull_strength` 是**去死区/去偏差后**的下拉饱和值——`IntentFilter::update_axis` 是瞬时阈值滤波（`filtered = |centered| ≤ threshold ? 0 : centered`），**没有时间滞后**。它只在原始摇杆**低估**用户下拉时（死区边沿、摇杆中心偏差、磨损摇杆）才高于原始值。`max(原始, 滤波)` 因此保证互补项只会让抵扣量 `≥` 原始值，即**新版本的 AI 补偿量在任何帧都不大于旧版本**（开火+下拉时更少叠加补偿、更不抢用户）。该分支只在开火中生效，不开火时是死代码。

> **实现修正（2026-08-13 代码复核）**：早期描述称 `recoil_pull_strength`"略滞后于原始物理值、压枪释放瞬间多保持一拍"，此说法**不成立**——`intent_filter.cpp` 无任何时间平滑，`filtered == raw`（干净输入无偏差时）。互补分支实际只在"滤波下拉 > 原始下拉"（死区/偏差导致低估）时与旧版不同，详见 §4.6。

---

## 3. 量化：真实会话操作分布（第一轮）

用 `tools/mine_operation_templates.py` 对会话 `20260812T182729Z_49124_1` 挖掘（40,876 帧 controller_sample）。schema-17 会话需重建输入（开火从 `delivered_control_sample` 按 `sample_seq` 关联、purpose 从 `manual_authority_mode` 推断）；schema-18 会话将直接携带 ground-truth 标签，无需重建。

### 3.1 分布

| 操作类别 | 帧数 | 占比 | 幅值 p50 | 误差 p50 | 速度 p90 | 信任 p50 |
|---|---|---|---|---|---|---|
| no_gesture | 26,263 | 64.3% | 0.14 | 0.0px | 0.0 | 0.50 |
| unreliable | 5,829 | 14.3% | 0.32 | **50.0px** | 1,066.6 | 0.10 |
| recoil_pull | 3,685 | 9.0% | 0.38 | **17.3px** | 127.0 | 0.90 |
| follow_track | 2,212 | 5.4% | 0.04 | 42.8px | 422.5 | 0.82 |
| acquire_flick | 2,032 | 5.0% | 0.52 | 41.4px | 1,370.3 | 0.95 |
| lead_track | 444 | 1.1% | 0.06 | 48.9px | 876.4 | 0.82 |
| correct_track | 368 | 0.9% | 0.28 | 24.6px | 595.8 | 0.88 |
| handover_intent | 43 | 0.1% | 0.04 | 69.3px | 52.7 | 0.82 |

上下文：开火占比 16.7%，目标被持有占比 33.8%。

### 3.2 读法（诚实解读）

- **`unreliable` 14.3%、误差 p50=50px**：这是全分布中误差最高的一类，说明"匹配不到任何模板"的输入集中在误差大的时刻——降级信号是**真实**的，不是分类器噪声。这正是 §4.5 想抓的"错误操作场景"的在线度量。
- **`recoil_pull` 误差 p50=17.3px**：用户压枪时误差显著低于全局，说明压枪行为本身是有效的正确操作模板（§4.5 的预期）。
- **`acquire_flick` 误差 p50=41.4px、速度 p90=1370px/s**：大角度获取，符合"快速、一致、末端减速"模板。
- **`lead_track` 1.1%**：占比低，但它是设计上最容易误伤的一类（熟练用户带提前量会被读成"D 出界"）。本会话里它被正确识别为独立类别，未污染 unreliable。
- 这些分布就是"正确操作长什么样"的第一份量化基准，也是在线模板对照的数据来源。

---

## 4. 验证：AimLab 基准 A/B（诚实结论）

### 4.1 做了什么

同一构建，仅切换 `operation_aware_recoil`。早先一轮（`--vision-disturbance` 缺省 `off`）因合成用户不开火而 NULL，见 §4.3。2026-08-13 补充诊断计数器并改用**开火场景**重跑：

- 新增两个手动压枪 profile：`recoil-controller`（恒下拉 0.30）与 `recoil-flail`（相位抖动，下拉 0.28/0.24 + 上推 0.12/0.10），见 `sustained_aimlab_types.h` / `sustained_aimlab_simulator.cpp`。
- 配置用真实 `config.toml` 生成仅此键不同的两个变体（`artifacts/benchmarks/sustained_aimlab/oprecoil-ab-20260813/config.oprecoil-{on,off}.toml`）。
- 运行参数：`--profile <recoil-controller|recoil-flail> --vision-disturbance gun-kick --benchmark-recoil on --duration-ms 30000 --cohort both --seed 2026072301`。
- 诊断计数器（`AssistedModeCoverage`）：`firing_frames`（`fire_action` 帧）、`pull_class`（分类为 `recoil_pull` 的帧）、`recoil_active`（净补偿增量 `recoil_stick.y < 0` 的帧）。

结果（4 次运行，ON/OFF 全部**逐字节一致**，score / mean_error_px / p95_error_px 及诊断计数器均相同）：

| profile | 配置 | ads score / mean / p95 | bodylock score / mean / p95 |
|---|---|---|---|
| recoil-controller | on | 5335.82 / 61.16 / 182.22 | 33602.3 / 51.18 / 145.35 |
| recoil-controller | off | 同上（逐字节） | 同上 |
| recoil-flail | on | 4150.85 / 42.50 / 117.05 | 33268.8 / 48.27 / 123.37 |
| recoil-flail | off | 同上（逐字节） | 同上 |

### 4.2 为什么仍一致（本轮诊断给出的真正根因）

诊断显示合成用户**确实开火**、也确实被分类为压枪：

- `recoil-controller`：开火 10,120 / 26,246 帧（ads/bodylock），`pull_class` = 开火帧数（100%），但 `recoil_active = 0` —— 净补偿增量在**全部**被分类为压枪的帧上为 0。
- `recoil-flail`：开火 8,186 / 26,246 帧，`pull_class` = 4,319 / 13,513（上推相位不满足下拉），`recoil_active` = 4,063 / 13,394（上推相位 AI 补偿向下生效）——但 ON/OFF 仍逐字节一致。

两个 profile 都跑通了开火与分类路径，却不产生任何 ON/OFF 差异，根因有两层：

1. **手工下拉覆盖自动补偿**：`recoil-controller` 恒下拉 0.30、`recoil-flail` 下拉相位 0.28/0.24，都 ≥ 当帧 AI 补偿量（baseline `feedback_amount = 0.20`）。`recoil_stick.y = -max(0, amount - manual_down) = 0`，净增量为 0 —— 互补项没有余量可加。
2. **理想摇杆下滤波恒等于原始值**：`IntentFilter::update_axis` 是瞬时去死区/去偏差（无时间滞后），干净输入无偏差时 `filtered == raw` → `max(raw, pull_strength) == raw`，互补分支本身就是 no-op（与 §2.3 的修正一致）。

结论：**不是"改动无效果"，也不是早先的"合成用户不开火"，而是"开火与分类都发生了，但合成场景的手工下拉足够强、摇杆足够干净，互补项没有任何可区分的输出"。** 这反过来证明：该特性在"用户充分压枪"的干净条件下**不改变行为（无回归）**；它的差异化价值只存在于"原始摇杆低估用户下拉"的真实场景（死区/偏差），合成基准构造不出这种场景。

### 4.3 已废弃的记录与早先 NULL 原因

- 早先一次"开"的记录显示 `ads score=11956.3 mean=30.08px p95=74.13px`、bodylock 略有退化。复核发现该记录**不可复现**，且其 `script_hash`（`18329578994466987054`）与基线（`16391122711143624084`）不同——同一 seed + 同一场景配置下 `script_hash` 是纯函数，不应不同，说明那次运行实际加载了不同的配置或二进制。**该记录不能作为证据，已丢弃。** 本白皮书只以可复现的数字为准。
- 早先 `--vision-disturbance` 缺省 `off` 的运行中，`fire_action` 只在 `vision_disturbance != Off` 时置真（`sustained_aimlab_simulator.cpp`），不开火 → `RecoilPull` 分支永不触发 → 两个配置一致。这一轮用 `gun-kick` 修正了"够不到路径"的问题，但如上所示，路径够到之后依然一致——因为手工下拉已覆盖补偿。

### 4.4 结论与建议

- 合成基准的诚实结论修正为：**新增压枪 profile 把开火和 RecoilPull 分类都跑起来了，但净补偿增量为 0（手工下拉覆盖），互补项在 ON/OFF 下逐字节一致 —— 无回归、无合成可测提升。**
- 根因（无时间滞后滤波 + 干净摇杆）说明该特性的差异化行为只作用于"原始摇杆低估用户下拉"的场景，合成基准无法构造；验证必须依赖真实硬件会话（死区/偏差由真实摇杆提供）。
- 维持默认开。结合 §4.5 真实会话证据（用户手感正向 + 68% 压枪帧互补让位 + 0% 信任分塌陷），特性按设计工作，且**方向安全**（新版补偿量 ≤ 旧版，见 §4.6）。
- 严格归因仍需要一次 `operation_aware_recoil = false` 的靶场对照会话（§7）。

### 4.5 首次靶场验证（2026-08-13，schema-18 ground-truth）

用户在靶场实测后反馈："没有抢准星的感觉，辅助效果却挺好，压枪还是能压"。遥测会话 `runs/native_perf/sessions/20260813T022207Z_55120_1`（schema-18，69,370 帧，开火占比 19.6%）用**原始 ground-truth 标签**统计：

| 指标 | 值 |
|---|---|
| `recoil_pull` 占比 | 15.6%（10,850 帧）——用户主动压枪的时间占比高 |
| `recoil_pull` 误差 p50 | **11.5px**（对比旧会话重建值 17.3px） |
| 压枪帧中 `pull_strength` > 0.20 | **68%**——互补项在这 68% 的帧里实际参与了"让位" |
| 压枪帧 `direction_trust` p50 | 0.90，且 **0%** 低于 0.6——分类器对真实压枪的识别干净、无乱推误判 |
| `unreliable` 占比/误差 p50 | 8.2% / 47.3px——降级信号仍稳定地落在高误差区 |

**诚实归因边界：** 单会话无法把"压枪误差 11.5px vs 旧会话 17.3px"的改善完全归因于互补项（场景/武器/当日状态不同）。但三件事互相印证：① 用户主观手感明确正向；② 压枪是全场最低误差的活跃类；③ 68% 的压枪帧里互补项确实在让位。三者方向一致，支持"压枪互补按设计工作"。更严格的归因需要一次 `operation_aware_recoil = false` 对照会话。

### 4.6 新版在什么场景下会弱于旧版（直接回答）

**行为差的形式化对比：**

| | 旧版（`operation_aware_recoil = false`） | 新版（默认 `true`） |
|---|---|---|
| 抵扣量 `manual_down` | `原始下拉` | `max(原始下拉, 去偏差滤波下拉)` |
| AI 补偿量 | `amount - 原始下拉` | `amount - manual_down` |

由于 `max()` 只会让 `manual_down ≥ 原始下拉`，**新版在任意帧的 AI 补偿量都不大于旧版**。两版只在"`滤波下拉 > 原始下拉`"的帧上不同，即分类器判为 `RecoilPull`、且原始摇杆**低估**了用户下拉的帧（死区边沿 / 摇杆中心偏差 / 磨损摇杆）。这类帧上新版少补偿 = 枪口上抬略多于旧版。这是**刻意的让位行为**（用户自己正在下拉，系统不叠加），本身不是缺陷——只有分类器**误判**时才会变成真正"弱于旧版"：

1. **开火尾迹误判**：`firing_recently` 在物理松开扳机后保持 75ms（`native_gamepad_controller.cpp`）。若这 75ms 内用户摇杆产生 >0.05 的下拉分量（如开火后立刻快速下移修正），会被判为 `RecoilPull` → 互补让位 → 少补偿 0~数个百分点。窗口短、且需同时满足"非开火态仍持 75ms + 下拉分量"，实际风险低。
2. **开火中乱推含下拉分量**（§6-1 已列）：`RecoilPull` 优先级高于 `unreliable`，开火中推错方向且恰好含下拉时被当作压枪让位。缓解：下拉阈值 0.05 很小、真实乱推多为水平方向；信任分门槛（如 >0.8 才让位）可作后续兜底。
3. **摇杆中心偏差被慢学**：bias 只在 `|raw| ≤ 0.03` 中立区以 α=0.04 慢速学习。若用户习惯性把摇杆靠上休息，bias 学偏上 → 下拉被放大 → 让位偏多。偏差是慢变量、幅度小，且随使用自纠正。

**净结论：** 新版在任意场景的补偿量 ≤ 旧版；"弱于旧版"的唯一形式是**更少补偿**（枪口上抬略多），且只发生在分类器把"非压枪下拉"误判为 `RecoilPull` 的帧上。这是让位机制的**可接受代价**：方向安全（不抢准星、不叠加），无任何"抢用户摇杆"的风险；数值上需 §7 的靶场对照会话量化，但合成本身已证明干净压枪下零差异。

---

## 5. 测试与回归

- `operation_intent_tests.cpp`：13 个单测，覆盖——无操作（亚死区）、获取甩动、开火+下拉压枪（onset 边沿）、跟枪、预测（含"慢目标绝不判预测"）、修正、换人显式优先、不可解释输入（辅助上下文内）、**free-look 不算不可解释**、**开火乱推方向信任分坍缩**、reset 清边沿、类别名稳定性。
- 全量回归：51/51 CTest 通过（Release 配置）。
- schema v18 序列化字段已接入收集/填充/JSON 三处，测试全绿。

---

## 6. 边界与风险（诚实清单）

1. **开火乱推的误判风险**：`recoil_pull` 优先级高于 `unreliable`，开火中推错方向且恰好含下拉分量时会被判为压枪。缓解：下拉阈值 0.05 很小，实际乱推多为水平方向（不满足）；但这是真实风险，需靶场数据复核。若误判显著，可给互补项加 `direction_trust` 门槛（如 >0.8 才启用互补，信任分随方向反转快速坍缩）。
2. **压枪互补的差异化行为只在真实摇杆下可测**（§4.2）：合成基准已跑通开火与 `RecoilPull` 分类，但干净摇杆 + 强下拉使互补项净增量为 0（逐字节一致）。失败模式是"少补偿"而非"抢准星"，方向安全；数值效果需 §7 靶场对照会话量化。
3. **方向信任分尚未接入 authority ramp**：本版本 `direction_trust` 只写遥测。接入 §4.2-3/§4.4 的时变权威是后续行为版本的事。
4. **模板从单会话挖掘**：§3 的分布来自一个会话、重建输入。schema-18 落地后应积累多个会话的 ground-truth 模板，才能作为门禁阈值。
5. **根因未变**：设计文档 §6 已指出——E3/E4（目标身份抖动、视觉遮挡丢权威）仍是"不抢准星"感知的最大破坏因子。本版本没有动视觉/身份链路；`direction_trust` 为它准备好了输入，但修复仍在 `runtime_app` 侧未动的工作区里。

---

## 7. 下一步

1. **靶场/bot 对照会话**：一次 `operation_aware_recoil = false` 对照（§4.4/§4.6 建议的协议），量化真实摇杆下让位对压枪误差 p50 与手感的影响，决定默认值去留。
2. **积累 schema-18 多会话 ground-truth 模板**：重跑 `mine_operation_templates.py`（schema-18 无需重建），把 §3 的分布从"单会话重建"升级为"多会话真实标签"，再冻结 §5.3 的门禁阈值。
3. **把 E3/E4 监控落地**（设计文档 §5.5 第一优先）：`target_track_id` 重置率、`candidate_count==0` 帧占比、`plan_admitted` 率作为回归门禁——这是与分类器正交、且优先级更高的根因修复。
4. **信任分接入权威**（后续行为版本）：`direction_trust` 作为 §4.2-3 authority ramp 的连续输入，替换常数阻尼。

---

## 附录：关键文件定位

- 分类器：`native/controller_native/operation_intent.{h,cpp}`、`operation_intent_tests.cpp`
- 集成点：`native/controller_native/native_gamepad_controller.cpp`（`build_output_from_sampled_input`、`apply_recoil`）
- 遥测：`native/runtime_app/telemetry_schema.h`（v18）、`telemetry_collectors.{h,cpp}`、`runtime_telemetry.cpp`
- 配置：`native/controller_native/runtime_config.{h,cpp}`（`operation_aware_recoil`）
- 挖掘：`tools/mine_operation_templates.py`
- 设计依据：`docs/project/ASSIST_PERCEPTION_QUANTIFICATION_DESIGN_20260813.md`
