# DEC-2026-10-18-005 — 有效目标维持速度单次消费（dev候选）

- Status: accepted（仅用户明确的职责/实施范围；行为验证未通过，不代表用户接受退步）
- Date: 2026-10-18
- Confirmed by: 用户本轮明确要求dev修复重复缩小，保持原预算/最低追赶/胸部/手动/32–40ms，不除法补偿/提高权限/延迟交接，位置增益与70px仅分析，不合clean。
- Related sessions: session-log 2026-10-18 有效维持速度单次消费
- Related files: native/controller_native/response_model_aim_solver.cpp；bodylock_follow_controller_tests.cpp；docs/project/SUSTAINING_MOTION_20261018.md；tests/sustaining_motion_20261018/
- Supersedes: 无；补充DEC-2026-10-18-004，纠正把弱证据跟随不足主要归于硬预算的解释。
- Superseded by: 无

## Context

有效总目标速度已是维持居中所需的物理量，却在solver中与位置项一起乘身份权重，再接受最终预算限幅。零误差、60px/s需求.06在.0736预算内，旧仅给.004416。该损失不能归因手动仲裁或预算饱和。

## Decision

只在canonical delay-aware BodyLock有效总运动路径用a×position+bounded_motion；位置、lookahead、legacy/mouse语义不变，所有输出仍受原曲线后二维权限/Profile包络。不得用除以a补偿、最低输出、放大权限、延长观测或延迟ADS交接代替职责修复。只在dev保留候选，实际clean不变。

## Reasons

总运动负责维持相机与目标同速，位置项负责消除残差，身份策略负责限制允许的输出；不能为维持运动先制造位置误差。预算不足时仍应明示无法跟上，而不是偷偷增权。

## Rejected alternatives

- 速度除authority：掩盖消费契约、零权限奇点，不采用。
- 增加权限/最低输出或延迟交接：违反用户边界，不采用。
- 顺手移除位置增益/70px策略：范围不同、噪声和证据含义未解决，不实施。
- 因51CTest及夹具上界通过就宣称整体通过：不接受。逐例相对退步仍属未通过。

## Evidence

生产15a292d；新增反例先失败后通过（.004416→.06），51CTest/447Base、93capture/92solver通过。四种子每版本928例、10,208,000tick。独立预算内全链30.683→.226px、20秒38.142→.105；旧高速弱证据23.848→6.623。

## Consequences and unresolved behavior

整体停稳尚未通过：独立.0736噪声RMS .0421→.2941，明确敌人开火8.246→8.603，随机慢移2.281→3.075。以上为工程测量，不是用户接受结果。

运动估计入口的原始位移率噪声以前被身份增益遮住；位置滤波和像素对应运动的坐标域必须分别审查。另有非线性压枪合成不一致的生产反例。两者本轮仅定位、不增加新开关/延迟，也不将位置增益或70px变化混入修复。

## Review triggers

后续调整运动估计降噪或总响应/压枪合成，必须同时覆盖恒速无偏、噪声、二维、开火、逐轴手动、交接与独立短长；不能以重新缩小有效维持速度解决噪声。位置增益和70px规则须单独确认设计；合clean或实机验收须有新的明确依据，本轮未提供。
