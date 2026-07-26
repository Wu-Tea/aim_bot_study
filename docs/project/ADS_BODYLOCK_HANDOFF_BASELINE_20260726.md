# ADS→BodyLock 交接缺陷基线（2026-07-26）

## 结论

当前控制器的 ADS→BodyLock 交接缺陷可以稳定测出，并且实战配置
`activation_range_px=120 / tolerance_px=16` 明显比此前验证过的 `80/8`
更容易在交接后继续沿旧方向输出。

本轮只扩展 benchmark 和修复 benchmark 的观测盲区，没有修改生产控制策略。

## 测试定义

- 运行时间：每个 seed 60 秒
- seeds：`2026072601`、`2026072602`、`2026072603`
- 输入：mixed manual
- cohort：ADS acquisition
- camera response：500 px/(stick·s)
- slowdown：
  - default：圆周 0.50，圆心 0.40
  - strong：圆周 0.40，圆心 0.30
- 对照变量仅为：
  - baseline：`activation_range_px=80`、`tolerance_px=8`
  - live：`activation_range_px=120`、`tolerance_px=16`

每个真实 ADS→BodyLock 交接建立一个 1000ms episode：

- 局部窗口：0–159ms
- 尾部窗口：160–999ms
- 缺陷判定：局部回弹不小于 8px，或最终输出连续错误方向不少于 10ms

“错误方向”由最终送入虚拟手柄的输出相对当前目标误差方向计算，不把单纯的
中心穿越直接等同于缺陷。连续值和逐目标记录全部保留，阈值以后可以重算。

## 汇总结果

| 配置 | 交接数 | 缺陷数 | 缺陷率 | 局部误差面积/交接 | 尾部误差面积/交接 | P95 回弹 | 最坏回弹 | P95 错向输出积分 | Tracking points |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 80/8 default | 110 | 7 | 6.36% | 2338.89 | 9986.20 | 0.78px | 7.49px | 0.81 | 58900.72 |
| 120/16 default | 104 | 12 | 11.54% | 2452.81 | 10756.40 | 0.84px | 8.00px | 1.06 | 50687.91 |
| 80/8 strong | 108 | 6 | 5.56% | 2473.19 | 11204.81 | 1.13px | 8.22px | 0.52 | 50587.16 |
| 120/16 strong | 102 | 9 | 8.82% | 2581.94 | 12361.39 | 1.13px | 8.27px | 0.84 | 43649.11 |

相对同减速环境：

- default：`120/16` 缺陷率增加 81.32%，局部误差面积增加 4.87%，尾部误差
  面积增加 7.71%，Tracking points 下降 13.94%。
- strong：`120/16` 缺陷率增加 58.82%，局部误差面积增加 4.40%，尾部误差
  面积增加 10.32%，Tracking points 下降 13.71%。
- default 下 circle exits 从 37 增至 41；strong 下从 40 增至 48。

逐 episode 分类显示：

- `80/8 default`：7 个缺陷全部来自错误方向输出持续至少 10ms，最坏 37ms。
- `120/16 default`：12 个缺陷全部来自错误方向输出持续至少 10ms，最坏 49ms。
- `80/8 strong`：5 个错向持续缺陷和 1 个 8px 回弹缺陷，最坏错向 35ms。
- `120/16 strong`：8 个错向持续缺陷和 1 个 8px 回弹缺陷，最坏错向 50ms。

这说明当前主要问题不是传统的“大幅越过圆心次数”，而是模式交接后仍在消费
ADS 遗留速度/输出债务。恢复全局重 brake 会把目标跟随速度和用户微调一起刹掉，
因此后续应验证一次性、状态感知的无扰切换，而不是增加常驻 gate。

## Benchmark 观测盲区修复

旧模拟器只在 tracking score 已开始后检查 BodyLock 边沿。实际控制器经常在进入
目标圆、评分窗口开启前几毫秒完成 ADS→BodyLock，因此旧输出会出现
`bodylock_active_ms > 0` 但 `handoff_count = 0`。

新逻辑只缓存满足以下条件的边沿：

1. 当前目标先出现至少一个非 BodyLock（ADS）tick；
2. 随后同一目标进入 BodyLock；
3. 在首个可评分帧把该边沿交给 scorer。

从目标出生就始终处于 BodyLock 的样本仍不会被误算为 ADS 交接。

## 证据位置

- 原始 JSON、生成配置和汇总：
  `artifacts/benchmarks/sustained_aimlab/ads-bodylock-handoff-baseline-20260726/`
- 汇总：
  `artifacts/benchmarks/sustained_aimlab/ads-bodylock-handoff-baseline-20260726/summary.json`
- 矩阵运行器：
  `scripts/verify/run_ads_bodylock_handoff_benchmark.ps1`
- 汇总器：
  `scripts/verify/summarize_ads_bodylock_handoff.ps1`

## 下一步判定

该 benchmark 已足以作为无扰切换方案的验收基线。候选策略至少应同时做到：

- `120/16` 的缺陷率不高于 `80/8` 当前基线；
- 局部与尾部误差面积都下降，不能只把过冲推迟到 160ms 以后；
- Tracking points、circle exits、P95 jerk 不恶化；
- 不新增常驻 brake owner，不吞掉明确的用户变向。
