# 正常多人辅瞄：实战改善检查点

用户反馈：**“在多人游戏正常辅瞄的场景下没碰到抖动和摆荡的情况了。”** 本次将实际测试版本及相关修复提交为改善检查点。没有把这一反馈扩大为强辅瞄、所有地图/武器或所有离线失配场景已经通过。

日志审计结论为 **INSUFFICIENT_EVIDENCE**，含义是不能给出完整实战验收或因果 A/B 结论，不是发现了输出安全故障。完整会话 intake 因最后一行截断而 BLOCKED；以下检查明确只使用前两个完整分卷，未修补或删除原日志。

## 测试版本与覆盖

- 实战程序 SHA-256：`14b5bdb4435f8d20c8fe3a766a724a82e26e3e4da335df813f8dd4d801fd58e8`。
- 编译基点为 `e68f76eb5386866ab1ef8188af8718d9aa16db43` 加当时工作区修改；不能只用这个旧 commit 代表测试版本。构建记录在 `runs/log_capture_build_20260926_191850/build-manifest.json`。
- 配置 SHA-256：`298e84ee9a80a878480e5789b1b25f8f69983bf1d9ba34141da7a4dcf10357cf`；引擎 SHA-256：`45fc56274ff3bbc659e534c3b7833065b0483ef8022ac5d7657cd6da7dbdeb21`。
- schema 18、详细遥测开启、配置采集 500 Hz；记录的 active capture 为 200 Hz、controller tick 为 1000 Hz。这些不是实测游戏刷新率。
- 前两个分卷共 315,288 条 JSON 记录，均可解析；controller 样本覆盖 658.245 秒。完整会话还包含最后分卷的 21,852 条可解析记录及 1 条截断行，该分卷整体不参与本次统计。
- controller 样本 64,770 条，按本生产者 `sample_seq` 验证内容并去除 3,004 条重发后为 61,766 条：Manual 52,129，ADS 6,138，BodyLock 3,499。
- 事件环会延后发出旧样本，文件顺序有 416 对逆序；按同生产者的序号恢复后时间严格递增。包裹层 `tick_id` 会复用，因此没有拿它跨表关联。不存在按最近行、最近时间猜测对应关系。

## 检查到的现象

在上述完整分卷内，controller 和 142,981 条发送回执均未出现发送失败；controller 输出错误码非零、输出越界、输入/输出非有限数值均为 0。这里检查的是有记录的样本，不能推断未记录 tick 的情况。

74,654 条 acquisition trace 使用各行自身的已校准 source-present steady 时间到 ViGEm 提交完成时间：P50 6.369 ms，P95 11.057 ms，P99 12.088 ms，最大 29.200 ms。百分位采用线性插值。该统计混合 active/idle trace，仅描述记录链路，不是纯瞄准延迟，不是游戏看到画面变化的延迟，也不与无日志场景比较。

同目标、直接 observed BodyLock、相邻样本间隔不超过 50 ms 的连续片段共有 507 段。固定筛查条件为：1 秒内误差越过 ±3 px 至少反向 4 次，同时 pre-recoil 输出越过 ±0.03 至少反向 4 次。该条件是诊断筛查，不是已标定的玩法失败阈值。

筛查保留两处观察点，未隐去或写成“零异常”：

1. target 632、sample 80722–80769，约 217 ms 的开火片段：横向误差范围 -5.33 至 +12 px，输出范围 -0.222 至 +0.342，存在短促反转。手动 X 小于 15%，但目标自身运动、画面几何变化及开火扰动没有独立真值，不能直接认定为控制器自激。需要在后续复现时优先对照。
2. target 937、sample 117596→117598，约 9.986 ms：pre-recoil X 从 -0.268315 变为 +0.291631。模式原因从 `target_acquisition_gesture_coop` 切换为 `ads_extension_cooperative`；同期玩家 Y 约 0.69，X 约 0.29。跳变与 ADS 阶段/人工权限交接相符，但是否符合期望手感仍是开放项，不能据此宣称交接问题全部解决。

因此可以记录用户本场正常多人手感改善、未见发送/数值安全异常，并提交当前检查点；不能记录“日志证明所有摆荡消失”。未知游戏刷新率、硬件画像、未正常收尾、无匹配旧版实战及无同步视频，均保留为证据限制。

## 本次提交的代码范围

- gamepad 人工意图改为 15–30% 的连续权重；原生输入保持 0 软件死区，mouse 原有策略分开保留。
- 最终仲裁使用原始人工提案及同一权重，避免重复衰减；实现移到 `.cpp`，减少公共头中的算法实现。
- 相机运动账本由实际发送成功的 DS4 解码值和提交时间更新；失败/重连打断旧区间。没有把未发送的 AI 提案当成游戏实际输入。
- 修复高频短观测区间反复重置导致响应学习/运动估计停止的问题。
- Vision adapter 将检测置信度与颜色排序分数分开，保持正确单位和有效性检查。
- 保留非回归、DS4 全量字节回读、发送边界、高频 RED→GREEN、随机扫描与配对比较工具。

离线 410 项 Base/Feature 测试通过；高频回归合同完成检查通过；保留代码的 16 包、864 个目标机会未新增退化，1,944 个开发随机案例保持相同。细节见 [离线修复报告](CONTROLLER_OFFLINE_REPAIR_20260926.md)。响应/延迟失配 RED 和未通过的预测/校准候选仍保留失败状态；本次实战反馈不会将这些门禁改绿。

## 证据与复现

原会话目录为 `runs/native_perf/sessions/20260926T112155Z_26272_1/`。两个参与分卷的 SHA-256 分别为：

- `a5267b6686d2250b4d19e85ebd5dd916d307671d9f5a9e4ea2c67a6f1402daf1`
- `4a2f16b1abab4968b033ab510903aad956ea7983cdc71a0fa6e3c5030c086518`

最后分卷 SHA-256 为 `503bafb0faaa0c5bc59fd2dc2fda227400c1216a4aa5cccd6e2382242e660758`，最后第 21,853 行截断，原样保留。所有原始日志、程序备份、失败回归二进制和最终证据均不进入源码提交。

本地构建记录目录还保存 `live-audit-manifest.json`、`live-audit-intake.json`、`live-audit-bounded-manifest.json`、`live-audit-bounded-intake.json`、`live-audit-metrics-ordered.json` 和筛查片段。提交的 [审计脚本](oscillation-20260926/audit_multiplayer_capture.py) 只接受非 BLOCKED 的有界 intake，并核对参与源文件的 SHA-256。

```powershell
python .agents/skills/native-telemetry-audit/scripts/audit_evidence.py check --manifest runs/log_capture_build_20260926_191850/live-audit-bounded-manifest.json --output runs/log_capture_build_20260926_191850/recheck-intake.json
python docs/benchmarks/oscillation-20260926/audit_multiplayer_capture.py --manifest runs/log_capture_build_20260926_191850/live-audit-bounded-manifest.json --intake runs/log_capture_build_20260926_191850/recheck-intake.json --output runs/log_capture_build_20260926_191850/recheck-metrics.json
```

清理仅涉及已被 `baseline_trace_v2.csv` 替代的早期扫描 CSV，以及可重建的探针 `.obj`；删除清单与哈希保存在同一构建记录目录。最新实战日志和所有引用中的 RED/GREEN 证据保留。与本次 controller 工作无关的 GPU contention、训练、Flash 工具及研究文件不纳入提交。
