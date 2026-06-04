# 审查和修订记录

## 第一轮审查

初稿完成后，曾开三个 subagent 从三个角度审查：

1. 面试官视角：看工程判断、证据链、风险边界是否够硬。
2. 无相关经验读者视角：看是否能从零读懂，术语是否解释清楚。
3. 有相关技术经验工程师视角：看算法细节、门槛、权限、开火链路是否准确。

第一轮修订补充了术语说明、权限表、典型流程、验证矩阵，并修正了 `target_confidence`、strong observed 开火资格、weak association、cue hold 等细节。

## 第二轮用户反馈

用户反馈认为第一轮正文仍然像项目笔记，不够像正式报告；git 提交 hash 没必要出现在主文；希望报告更专业、更通透，重点解释 vision 因为什么场景选择什么策略，gamepad 因为什么情况做什么特殊优化。

第二轮修订去掉主文中的提交 hash 叙事，改为“场景 -> 问题 -> 策略 -> 效果 -> 边界”。

## 第三轮外部审查反馈

另一个 Codex session 审查了报告包，结论是主文结构已经成立，适合作为项目设计说明基础版本，但还需要补：

1. `research.md` 的事实状态已经过期，需要把 weak association / authority gating 从未提交实验改成已提交事实。
2. 主文缺少最新的宽低框、尸体残留和黄点 double check。
3. `sources.md` 外部资料太薄，需要补实时视觉、低延迟、跟踪、光流、aim assist 相关资料。
4. 主文缺少图，建议加入数据流图和权限/状态图。
5. 推荐标题应换成“从看见目标到可信控制：native vision 和 gamepad 的系统化设计”。

## 第三轮修订

- 更新 `research.md`，去掉过期的 dirty/uncommitted 描述。
- 把 `c4c9982` 和 `9473ca2` 标记为项目内已提交事实。
- 在 `research.md` 和 `post.md` 补充宽低框、尸体残留和黄点 double check。
- 在 `post.md` 加入两张 Mermaid 图：
  - 数据流图
  - 目标权限流图
- 扩充 `sources.md`，加入 TensorRT、DXGI Desktop Duplication、低延迟 FPS、SORT、Deep SORT、OpenCV optical flow、aim-assist HCI 资料。
- 更新 `metadata.md` 推荐标题和摘要。

## 仍然保留的限制

本报告没有新增 live gameplay smoke，也没有新增 native vision 前后延迟实测表。报告里把这些作为后续验证方向，而不是包装成已经完成的结论。
