# cue continuation与未选中诊断框的观测职责

- Status: accepted（修复范围及2026-10-19用户接受本轮结果、授权同步clean；不表示所有实机问题已解决）
- Date: 2026-10-19
- Confirmed by: 用户明确要求在dev修复cue hold被未选中残框阻断，保留原权限/胸部/最低追赶/手动/32/40ms，暂不合clean。
- Related sessions: session-log.md 2026-10-19 cue残框修复；sync-20261019-001。
- Related files: native/controller_native/native_gamepad_controller.cpp；target_pipeline_integration_tests.cpp；docs/project/CUE_CLUTTER_FIX_20261019.md；tests/cue_clutter_20261019/。
- Supersedes: none
- Superseded by: none

## Context

Vision已选择同代际cue时，原始检测列表仍可含未选中残框。adapter保留诊断候选，controller只在count==0时构造cue，导致新鲜cue被挤掉；coordinator拒绝普通候选后误入缺帧。第一案例0.333/2.017秒存在这种输入组合，生产反例可重现。

## Decision

正式控制观测以selector明确的cue continuation为准；观测转换层不把未选中残框作为该cue帧的控制候选，按既有规则构造唯一cue。原始检测和诊断保留。coordinator继续验证已有身份、generation、几何、距离和时效；无效cue不能借残框获取权限。

## Reasons

选择身份和证据类型属于selector，转换层不能用诊断列表是否为空重新决定cue是否存在。此处修复移除错误缺帧的起因，普通人体路径及既有控制策略不变。

## Rejected alternatives

- 丢目标后继续压手动、续期32/40ms：掩盖错误并破坏手动权。
- 提高cue权限、放大输出、延迟ADS交接：不修观测混用，超出授权范围。
- 为维持某些仿真较低误差保留残框阻断：依赖异常ADS生命周期，不是合理职责。
- 删除全部视觉残框：损失诊断信息，修复范围应在控制观测转换。

## Evidence

先红后绿的原生反例与边界检查；449/449 Base、51/51 CTest；视频9个cue接收7→9。独立3%/1%条件共36,815个授权cue全部接收。所有384个配对场景修复后的残框输出与无残框参考逐tick一致，参考前后不变。细节及保真限制见报告。

## Consequences

dev已编译，修改未提交，clean不变。修复让部分场景回到既有正常cue的阶段/手动行为，但不是整体误差都改善：独立3%有22例、1%有19例变差；最坏1%长仿真189.956→262.144px，原无残框参考原本262.144px。用户未接受这些差异，不能沿用前轮小幅退步接受。阶段/持续手动与ADS模型等问题仍独立待查。

## Review triggers

若实机仍在强人体/弱关联/cue切换时出现30%手动突变，先记录真实输入、证据、权限、阶段与source age，建立独立反例；不得以本决策授权更改权限/时效/交接策略。若selector契约变化，重新核查唯一cue与普通候选边界。

## 2026-10-19 同步授权

用户在获知cue修复、独立仿真及剩余差异后明确表示“没问题了，先整理一下提交然后同步到clean”。当前dev包含后续维持速度和cue修复，授权同步的是这一组合，不是重新接受此前未修复的降噪候选。原权限、胸部、最低追赶、手动与32/40ms继续保持；已报告差异保留，不推定所有实机问题已解决。此前暂不合clean的状态已由本条取代。
