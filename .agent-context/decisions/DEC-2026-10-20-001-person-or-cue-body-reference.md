# person OR cue 与同帧身体参考

- Status: accepted（2026-10-20 用户接受当前迭代并明确授权提交、同步 clean；保留已知数值差异和验证边界）
- Date: 2026-10-20
- Confirmed by: 用户提出“这种是否直接做成or的关系，而非and”“新建目标的时候记录一下 cue和person的位置”；针对持续 cue 超过 180 ms 的明确询问答复“是，持续可靠的 cue 可以继续跟踪”，并授权同步本次改动和结论。
- Related sessions: session-log.md 2026-10-20；sync-20261020-001。
- Related files: native/vision_native/src/target_selector_fusion.cpp；cue_identity.cpp；cue_identity_tests.cpp；native/runtime_app/runtime_loop.cpp；docs/project/CUE_PERSON_OR_20261020.md；tests/cue_identity_offline_20261019/README.md。
- Supersedes: 10-19 cue 必需试用策略（docs/project/CUE_IDENTITY_DEV_20261019.md）；DEC-2026-08-11-002-native-irdt-single-owner-v1 的 180 ms cue 几何上限，仅限本轮 dev 手柄融合路径。其他职责及未启用融合的旧路径不由本决策改写。
- Superseded by: none

## Context

用户先要求 cue/person 独立检测，并因两个目标来回跳改为 cue 主导，在 dev 试用后反馈 ADS 定位和跟随不佳。两段新录像支持 cue 必需门槛损失人体可用性；clean 两分钟后又出现人体漏检与 180 ms 身体参考过期。用户修正为 OR，并要求记录 cue 和 person 的位置。

## Decision

可信 person 独立索敌和定位，cue 非必需。同一帧可信人体与唯一关联 cue 提供身体/胸部、cue 坐标与目标 generation 的参考。先有 person 时允许稍后配对；只有未配对 cue 时不凭空创建胸部。

同一个 cue 持续可靠可见时，以参考胸部加当前 cue 位移延续目标，即使人体漏检超过 180 ms。只做参考平移，不递归更新几何，不以弱框重新学习偏移；强人体与 cue 重新配对后校正参考。

cue 缺失或有歧义时不发布旧 cue 坐标；50 ms 仅为匹配身份记忆，过期清除参考。可信 person 可独立维持当前目标；换目标/reset 清理旧参考。保留 cue aim-only、下游 32/40 ms、手动退出/逐轴权限/原始透传，不变更 ADS 算法或个人模型/配置。圆点和死敌骷髅有效，不把骷髅解释为目标死亡。

## Reasons

person 与 cue 是互补观测；可信人体不应被另一个检测器的漏检否决。同帧配对提供可解释的身体参考；持续可靠 cue 可以更新位置，180 ms 人体年龄不再单独撤销这种更新。目标身份和观测时效仍是独立边界。

## Rejected alternatives

- cue AND person / cue 必需：已观察到可信人体被 cue 门槛阻断。
- 所有 cue 在人体超过 180 ms 后一律停输出：用户明确取消此条件，且核心录像片段有持续 cue 可用。
- cue 单独猜胸部，或不断用 cue 推导结果/弱框训练偏移：没有新的可信人体几何依据，易累积偏移。
- 为延续目标而放宽 32/40 ms、压制手动退出、修改权限或 ADS 力度：未获授权，也不解决观测职责问题。

## Evidence

clean 122.20–122.60 秒 48 帧，35 帧无人框；旧 person 基础与上一版 cue 必需均 0 帧有目标，新方案 47 帧 cue 延续。同一人体框的新旧胸部坐标差异为 0。dev 前 60 秒输出 2426/1317/2518 帧，clean 尾段 1281/715/1551 帧（旧 person / 上版 cue / 新 OR）；新方案仍存在 186/53 帧旧方案有输出而本版没有，不能只报增加。

dev Release 构建、CTest 51/51，Vision 58/58、完整链 50/50。随机 cue 身份 168960 tick；同帧参考 4 种子、2/20 秒共 4400 帧；完整链 800 ms cue 延续保留无开火和手动退出。完整数据与复跑方法见报告。

## Consequences

dev 可试用，工作区未提交/推送，clean 未同步。用户正准备实测，不代表接受效果。离线输出覆盖不是准确率；dev 会话无遥测，无法将全部 ADS 退步归因于 cue 门槛。颜色误识别、错误关联和固定偏移对姿态/距离变化的局限仍在。此前 ADS 开火/固定偏置反例仍独立待解决。

## Review triggers

用户反馈跟错人、相邻目标往返、人体恢复后胸部跳变、长时间遮挡时点位失准、ADS 定位仍退步或 cue 消失后仍持续控制时，先用录像与遥测核对身份、参考配对、当前证据和时效。仿真通过不授权同步 clean；用户接受并另行要求同步后再处理。

## 2026-10-20 同步确认

用户“没什么问题，允许提交。同步到clean”取代此前暂不合 clean 的限制。dev 生产提交 a93dbaa / 642dc26，clean 生产提交 91b116a；数值反例仍保留，不据此宣称所有压力场景问题已解决。SyncSet sync-20261020-002。
