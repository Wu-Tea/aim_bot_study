# 数字模拟与测量入口

2026-10-04 已删除旧 SHA256 校验、冻结验收 policy、基线比较器、专用验收测试、审计 skill 和结果材料。当前入口用于数值模拟及测量，没有候选合格或发布验收结论。

- [Sustained AimLab 数字模拟](sustained-aimlab.md)：目标轨迹、ADS/BodyLock、人工输入、延迟与响应 plant。
- [运行时遥测](native-runtime-telemetry.md)：控制器和 Vision 调试记录。
- [Vision 时间窗口](vision-blind-window.md)：采集、发布、消费与响应时钟。
- [本次清理结果](../project/validation-cleanup-scan-20261004/CLEANUP_RESULT.md)：删除范围、保留项及验证。

保留普通单元测试和数字模拟断言，以检查代码能运行、输出有限、状态一致。数字模拟结果不能证明实机体验；后续验收体系由用户按需重新设计。
