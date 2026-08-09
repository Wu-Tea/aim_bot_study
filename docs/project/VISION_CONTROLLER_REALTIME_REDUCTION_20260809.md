# Vision → Controller 实时链路筛查（2026-08-09）

## 目标与边界

这项工作的产品指标是：从游戏画面对应的 `source_present` 到 ViGEm 完成发送，稳定运行时达到 `P99 <= 7 ms`。同时单独观察 `Vision publish -> controller consume/send`，避免 controller 自己再增加一个完整 tick。

本轮不重写控制器，也不删除已经证明有价值的目标优先输出、响应估计和 200 ms 因果运动台账。重点是移除在“Vision 低频、controller 不够稳定”时期引入、现在会覆盖 160–200 Hz 新快照的历史补偿。

## 当前基线结论

- 旧性能摘要中 `result -> controller` P99 约为 1.125 ms，controller pipeline 与 ViGEm 通常低于 0.2 ms。
- `source_present -> result` P99 约为 11.875–13.625 ms，`source_present -> ViGEm` P99 约为 12.625–14.125 ms。
- 因此 7 ms 不是只优化 controller 就能宣称完成。第一阶段先清掉 ViGEm 前的可避免工作并修正旧坐标权限；正式验收必须使用新版本、身份完整且正常关闭的实时日志。
- 最近的详细会话尾部截断且游戏刷新率未知，只能作为诊断素材，不能作为正式 PASS 证据。

## 筛查结果

| 模块 | 当前问题 | 决策 | 实现边界 |
|---|---|---|---|
| Selector 通用 hold | 当前帧有候选但未选中时，仍输出旧人物坐标 6–8 帧 | 删除执行权 | 可保留内部身份，但结果必须无 aim/fire authority |
| Selector 跳变拒绝 | 当前检测移动较大时拒绝新点并沿用旧点 | 删除 | 当前帧可信检测优先；多目标身份切换仍可确认 |
| Selector 点平滑 | 对连续小位移做位置低通，天然造成跟随滞后 | 删除 | 输出当前帧目标点，平滑属于 controller 输出域 |
| 单目标路径 | 唯一可信目标仍受旧目标遮挡等待影响 | 简化 | 唯一可信当前候选立即取得坐标权 |
| 多目标切换 | 切换确认期间可能继续执行已失效旧点 | 改为身份保留、执行归零 | 当前活动目标仍在帧内时可用当前点；完全失配时不输出旧点 |
| TargetCoordinator coast | selector 明确“有候选但无选择”仍被当成图像 dropout | 修复 | 这种歧义帧 aim/fire authority 为 0；真正 `count == 0` 的短缺帧另行评估 |
| ADS cue 遮挡补偿 | 瞄准镜瞬间遮住人物框，但头顶 cue 仍可能可见 | **保留** | 同一 ADS epoch、同一 selector generation、cue 当前证据连续；aim-only、禁止开火、不能获取/切换目标 |
| Fusion/遥测/诊断/viewport | 位于 controller build 与 ViGEm 之前 | 移出关键路径 | 先发送 ViGEm，再做 best-effort 观测与 UI 工作 |
| GPU Service 抓帧相位差 | 200 Hz 轮询可能刚好早于约 180 Hz 游戏的新帧，扑空后再等完整周期 | 有界等待 | 仅独立 Vision 线程允许 DXGI 最多等待 1 ms；controller 直连回退仍为非阻塞，不做忙轮询或帧同步 |
| Causal memory / response estimator | 解决已提交运动与游戏减速响应，不是主要 CPU 热点 | 保留并测试 | 不允许它覆盖新 Vision 位置；行为回归单独验收 |
| W3 光流 | 已不链接进 production `vision_native_core` | 保持关闭 | 不重新引入第二条实时 CV 链路 |
| 固定跳跃/滑铲预测 | 来自低频 Vision 时期，可能与高频新点竞争 | 测试后决定 | 默认不扩大权限；只有回放数据证明净收益才启用 |

## Cue 补偿协议

Cue 不是“记住旧坐标”。它使用当前帧 cue 的位置变化，加上同一目标最近一次人物框与 cue 的相对偏移，得到新的目标点。controller 只在以下条件全部成立时接受：

1. 当前仍处于同一轮 ADS；
2. selector generation 与已拥有目标完全一致；
3. selector 没有报告目标切换；
4. 当前帧只有一个 cue continuation 候选，且没有 frame-local person id；
5. cue 证据连续、搜索半径受限、与预测位置的关联距离受限；
6. 只授予瞄准延续，始终关闭自动开火。

人物框重新出现时，直接回到当前 Vision observation；cue 消失或超时后立即失去执行权，不回退到通用旧坐标 hold。

## 验收流程

1. 单元回归：唯一目标大位移必须在当前帧输出新坐标；多目标失配确认帧必须无旧坐标权限；cue 遮挡仍能按当前 cue 移动且无 fire authority。
2. 关键路径遥测：记录 `result -> publish -> consume -> submit -> plan -> final -> ViGEm`，并保留 `source_present -> ViGEm` 产品指标。
3. 低开销实测：只开 perf summary，不开详细 telemetry，至少采集多个持续 ADS 窗口。
4. 门槛：`publish -> ViGEm` 不应增加一个 controller tick；`source_present -> ViGEm P99 <= 7 ms` 才能正式标记完成。
5. 若仍超标，按 `source_present -> capture`、GPU、result mailbox、controller/ViGEm 分段数据继续处理，禁止用删除安全判定来伪造延迟改善。
