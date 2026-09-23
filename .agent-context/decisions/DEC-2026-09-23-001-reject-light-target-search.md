# DEC-2026-09-23-001：否定光斑目标方案，保留点位与友军配置

Status: accepted
Date: 2026-09-23
Confirmed by: 用户明确要求“这个功能先回滚吧，否定了”，随后要求删除回滚记录、只保留 decision log。
Related sessions: 2026-09-23 光斑干扰人体识别、光斑索敌及性能对比
Related files:

- `native/vision_native/src/target_selector.cpp`
- `native/vision_native/src/vision_engine.cpp`
- `native/controller_native/runtime_config.h`
- `native/controller_native/runtime_config_tests.cpp`
- `native/vision_native/src/target_selector_tests.cpp`
- `config.toml`

Supersedes: none
Superseded by: none

## Context

曾尝试在 person 检测失败时，通过 RGB 光斑提案、局部额外推理和独立光斑目标扩大搜索能力。实验优先级为 person、scope_glint＋cue、普通光斑；光斑来源只允许搜索移动，不授予自动开火权限。

用户要求对照增加此功能前的代码测量开销。同进程离线对比显示，原视觉路径在这批固定画面上约为 0.75–0.84 ms/帧；优化后的实验路径在普通 person 场景约 0.95 ms，但光斑扫描与人体重检场景仍约 1.57–2.58 ms，全白压力图约 2.85 ms。以上为 P50，不包含截屏、游戏争用或控制输出，也不是实战验收结果。

## Decision

- 否定并移除光斑目标、整图光斑扫描、额外人体重检、对应控制输出分支及启用接口。不能将其作为默认关闭的生产功能保留。
- 保留用户独立提出的 `friendly_filter_enabled` 开关和人体点位比例配置。记录时实际配置为 `friendly_filter_enabled=true`、`target_height_ratio=0.35`、`target_wide_low_height_ratio=0.65`；这些值是当前状态，并非永久固定偏好。
- 删除为回滚专门建立的备份、脚本及单独说明，决策记录只保留本文件。
- 原有生成光斑的数据工具、模型、测试图片及其它既有工作区改动不属于此次回滚清理范围。

## Reasons

用户在查看旧版与实验版本的性能对比后明确否定此功能。实验还有普通亮点误搜索、假人体及错误自动开火请求反例，不能以性能优化后的行为等价性替代识别验收。

## Rejected Alternatives

- 仅关闭开关、保留生产光斑分支：不符合撤销功能的决定。
- 继续优化此实验并默认推进：用户已经否定，当前不再作为待完成方案。
- 一并删除友军开关和点位配置：它们是独立需求，继续保留。

## Evidence

- 用户明确指的是“提出光斑也能作为检测目标前的版本”，对照采用 Git 中尚无该功能的 selector，保留当时友军判断和 0.40 点位比例。
- 回滚后 `VisionEngine::poll_once` 与加入光斑前的代码一致，恢复单次主推理及原有局部颜色回读。
- 回滚构建完成，14 组 Base / Feature / Mouse 回归通过。旧光斑配置项已未知且无效，Python 光斑入口已移除。
- 保留两条防回归检查：旧开关不能启用功能；亮色像素及非 person 类别不能独立取得目标控制权限。

## Consequences

本轮保留的业务改动限于友军过滤和人体点位配置及其入口、校验和测试。光斑实验不是已接受能力，不能因为历史测试文件仍存在而重新接入生产。

## Review Triggers

只有用户提出新的明确需求时，才重新评估独立光斑目标方案；本次决定不授权继续实施。
