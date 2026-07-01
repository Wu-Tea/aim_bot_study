# CONTRIBUTING

## 基本流程

1. 从一个明确的需求 ID 或 backlog item 开始。
2. 中大型改动先写执行计划。
3. 先加/更新测试，再完成最小实现。
4. 保持提交可构建；不要把格式化、重构和行为变化混成一个不可审查提交。
5. 完成后更新文档、ADR 和第三方通知。

## 分支和提交

建议分支：`feat/M2-direction-baseline`、`fix/ring-buffer-overflow`。
建议提交前缀：`build:`、`feat:`、`fix:`、`test:`、`docs:`、`refactor:`、`chore:`。

提交消息应描述用户可见或架构可见的变化，不写“misc fixes”。

## Pull Request 清单

- [ ] 关联需求/任务编号；
- [ ] 范围与非目标清楚；
- [ ] Debug/Release 构建；
- [ ] 单元/集成测试；
- [ ] 实时回调无锁、无分配、无 I/O；
- [ ] 错误路径可解释；
- [ ] 文档和 ADR 同步；
- [ ] 依赖许可证审查；
- [ ] 无模型、游戏音频、秘密或临时大文件；
- [ ] 安全边界未被削弱。
