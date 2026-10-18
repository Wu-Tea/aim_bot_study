> 2026-10-18 后续纠正：用户明确不接受弱证据移动退步，禁止合回clean。进一步生产探针确认7.36%同时被用作位置/持续运动需求增益和最终预算，预算内的维持速度也被压小。原1000个BodyLock tick仅21个触及上限；不能再把预算不足当作主要根因或要求用户接受。当前只做职责分析，未修改生产算法。最新依据：[BodyLock职责分析](D:/work/AI/yolo-study-001/docs/project/BODYLOCK_AUTHORITY_20261018.md)。下文原“待接受发布边界”的下一步已被本条取代。

# clean 当前接续

更新：2026-10-18。**实际 clean 仍为 `86b34ea`，3% 观测降噪候选没有合入。** 当前启动入口与程序仍是前轮整体停稳版本，个人配置、模型和日志未改。

## 候选状态

- DEV 当前 `a11f221`，生产实现 `f9f42d3`。候选 `1cb9a12` / `codex/evidence-noise-20261018`，位于 `D:/work/AI/yolo-study-001/.worktrees/evidence-noise-clean-20261018`，原生 Release 已编译。
- 候选把3%末端输出死区移至capture位置证据降噪，保留配置数值、胸部源点、最低追赶、手动权、曲线、力限和独立压枪。没有加等待/开关或抬高输出。
- 原capture最多32 ms缺帧预测；超过32 ms、未到40 ms只保留身份/未完成ADS，缺帧辅助瞄准和开火权为零；40 ms释放，不续期。
- DEV51CTest，候选446Base+14开火；两端各656短长场景、7,216,000 tick，12份CSV一致。候选81GUI/WebView2及真实模型初始化、Applied热更、IPC、正常停止通过，独立配置且设备输出/震动关闭。曾有一次预览JSON截断和测试异步保存竞态，原始失败与修正记录保留。
- 新版小偏差和慢速移动改善，但高速弱证据组后段误差15.721→21.976 px；明确敌人组1.557→1.505。原因链：旧死区令ADS迟迟未完成，新版到点正常交接后更早受7.36%BodyLock权限约束，反例峰值维持杆量需0.0942，超过0.0736预算。

## 不可跳过的发布条件

用户要求“验证通过后再同步clean”，上述退步尚未获用户接受，不能自行判为整体通过。不能恢复死区、延迟ADS交接或提高BodyLock权限补分。先说明发布边界并取得明确取舍；若接受，再快进合入、实际clean原生/React编译和运行核对。

DEV、候选与实际clean的exe内嵌版本分别匹配a11f221、1cb9a12、86b34ea。8份候选生产文件与DEV文本一致；这不表示实际clean已同步。

[详细报告](D:/work/AI/yolo-study-001/docs/project/EVIDENCE_NOISE_20261018.md)、[DEV交接](D:/work/AI/yolo-study-001/.agent-context/handoff.md)、[候选决策](D:/work/AI/yolo-study-001/.agent-context/decisions/DEC-2026-10-18-004-observation-noise-and-fine-correction.md)。结果：`D:/work/AI/yolo-study-001/runs/evidence-noise-20261018/`。

SyncSet `sync-20261018-005`：用户授权记录，reviewer accept_draft；工程推断与用户确认分开，候选发布状态proposed，无外部消息或敏感数据。仿真不等于实机验收。
