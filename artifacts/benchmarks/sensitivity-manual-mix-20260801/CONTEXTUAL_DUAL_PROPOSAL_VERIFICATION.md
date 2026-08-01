# 2.4 灵敏度下的 manual / AI 双候选仲裁验收

日期：2026-08-01（Asia/Hong_Kong）

## 结论

本次落地没有把“过度尊重人手”简单反转成“忽略人手”。manual 与 AI
现在都作为绝对摇杆提案进入同一个 `VectorIntentFuser` 仲裁：

- ADS 全距离、近距离 BodyLock 的强同向输入进入 AI-priority 仲裁；
- AI 提案完整保留，manual 同向分量先按实机操作倾向做情境归一化，再保留
  `20%` 的平行 headroom；
- ADS 的同向 manual 等效为游戏灵敏度 `1.9`（`1.9 / 2.4`）；
- 近距离 BodyLock 的同向 manual 等效为 `2.0`（`2.0 / 2.4`）；
- 原始物理摇杆仍负责判断输入强度、方向、逃逸和 `0.45..0.70` 平滑过渡；
- 反向纠偏和与 AI 正交的横/纵跟随不缩放；远距离 BodyLock 保持旧行为；
- 非协作的近满幅反向输入仍能立即、精确地交还给 manual。

因此输出不再是“几乎完整 manual + 逐步衰减 AI”，也不是纯 AI；它是：

```text
原始 manual ──> 强度 / 方向 / 逃逸分类 ──┐
                                         ├─> 两个绝对提案仲裁 ─> 最终摇杆
AI shaped proposal ──────────────────────┘

仅在强同向协作路径：
manual_parallel = manual_parallel * contextual_scale
output_parallel = AI_parallel + 0.20 * manual_parallel
output_tangent  = manual_tangent（完整保留）
```

## 截图中“大幅提升”的来源

截图里的成绩不是这次 `1.9 / 2.0` 情境归一化跑出来的，而是第一阶段
AI-priority 修复相对严格归档的旧加法基线：旧实现会把 manual 与 AI 当成两股
可相加的力，并且强 manual 基本保留、AI 反而衰减；第一阶段把它们改成竞争的
绝对摇杆提案。

所有单元使用相同配置、相同 seeds（`1337, 2026, 7331`）、相同脚本 hash，
游戏/plant 灵敏度保持 `2.4`，没有降低 ADS 或 BodyLock 的 AI 增益。压力输入是
`recover`：先用 `0.65..0.95` 的强错误方向推杆，再用相近力度回拉。

| 场景 | Tracking | 过冲 | 平均误差 | P95 误差 |
|---|---:|---:|---:|---:|
| 静止 ADS | +7.8% | 15 -> 3 | -32.5% | -33.7% |
| 静止 BodyLock | +19.4% | 34 -> 11 | -49.1% | -52.1% |
| 移动 ADS | +7.7% | 17 -> 10 | -17.6% | -35.9% |
| 移动 BodyLock | +16.4% | 26 -> 11 | -26.0% | -41.5% |

提升大，是因为修掉的是输出所有权错误：旧链路在强同向输入时直接制造额外
相机脉冲。它不是通过降低游戏灵敏度、降低 AI gain 或改变目标选择“刷分”。

另一个把整个 benchmark manual 向量统一缩小的实验，曾得到约 `+14.8%`
总 tracking、平均误差 `-36.4%`、P95 `-56.3%`。那会连错误反向和有效切向操作
一起缩小，只能当成合成上界，不能当成生产策略成绩；本次生产实现明确不这么做。

## 本次情境归一化相对第一阶段的变化

本次只重分配强同向路径的控制权，目标是改善实机“人手和 AI 同向发力”的手感，
不是再追求一次大幅离线分数增长。因此在严格匹配的矩阵中，多数 tracking / error
变化保持在约 `±1%`；这正是保留反向、切向和纯 AI 行为后的预期结果。

| 场景 | Tracking | 过冲 | 欠跟 | 平均误差 | P95 误差 | 圆外摆出 |
|---|---:|---:|---:|---:|---:|---:|
| Recover 静止 ADS | +0.57% | 3 -> 1 | 0 -> 0 | -1.61% | -0.83% | 22 -> 22 |
| Recover 静止 BodyLock | -0.31% | 11 -> 8 | 0 -> 0 | +1.46% | +1.32% | 7 -> 8 |
| Recover 移动 ADS | +0.76% | 10 -> 11 | 22 -> 22 | -0.89% | -1.29% | 23 -> 25 |
| Recover 移动 BodyLock | -0.26% | 11 -> 13 | 28 -> 27 | +0.43% | +0.12% | 29 -> 28 |
| Arc 静止 ADS | +0.03% | 0 -> 0 | 0 -> 0 | -0.88% | -1.74% | 12 -> 11 |
| Arc 静止 BodyLock | -0.02% | 14 -> 14 | 0 -> 0 | +0.08% | +0.18% | 14 -> 14 |
| Arc 移动 ADS | +0.15% | 6 -> 6 | 32 -> 32 | -0.33% | -0.02% | 25 -> 25 |
| Arc 移动 BodyLock | +0.03% | 24 -> 24 | 43 -> 43 | 0.00% | +0.02% | 33 -> 33 |

`pure` 的静止/移动 x ADS/BodyLock 四个控制单元，在 tracking、acquire、过冲、
平均误差和 P95 误差上均为精确零差异；说明没有改变无 manual 混合时的 AI 链路。

## 最终版本相对严格旧基线

| 场景 | Tracking | 过冲 | 欠跟 | 平均误差 | P95 误差 |
|---|---:|---:|---:|---:|---:|
| Recover 静止 ADS | +8.4% | 15 -> 1 | 0 -> 0 | -33.6% | -34.2% |
| Recover 静止 BodyLock | +19.0% | 34 -> 8 | 0 -> 0 | -48.4% | -51.5% |
| Recover 移动 ADS | +8.6% | 17 -> 11 | 28 -> 22 | -18.3% | -36.8% |
| Recover 移动 BodyLock | +16.1% | 26 -> 13 | 33 -> 27 | -25.7% | -41.4% |
| Arc 静止 ADS | +80.4% | 0 -> 0 | 0 -> 0 | -48.4% | -26.4% |
| Arc 静止 BodyLock | +77.2% | 45 -> 14 | 0 -> 0 | -67.1% | -57.2% |
| Arc 移动 ADS | +127.7% | 5 -> 6 | 30 -> 32 | -56.4% | -43.7% |
| Arc 移动 BodyLock | +71.3% | 36 -> 24 | 52 -> 43 | -62.3% | -53.9% |

Arc 移动 ADS 的离散过冲和欠跟仍略高于严格旧基线，虽然 tracking 和误差面积大幅
改善；它是实机验收时仍需观察的残余风险。

## 20% headroom 的选择

同一矩阵对 `10% / 20% / 30%` 做了 sweep。`20%` 的总 tracking 与 `10%`
相当，但总过冲更低（`79 -> 77`）、平均误差、P95、handoff tail 和 rebound
均为三者最佳；`30%` 开始出现 BodyLock、circle exit 和 wrong-way 的回退。
因此选 `20%`，让 manual 确实参与，同时不重新制造显著的同向加法。

## 权威 artifacts 与参数

最终权威矩阵目录：

`artifacts/benchmarks/sensitivity-manual-mix-20260801/contextual-headroom-0.20-final-exact/`

每个单元均使用：

- seeds：`1337, 2026, 7331`
- duration：`15000 ms`
- target slot：`1000 ms`
- target profile：`near`
- camera response：`727.272727 px/(stick*s)`（游戏灵敏度 2.4）
- manual input scale：`1.0`（原始全幅脚本，不靠 benchmark 全局降手感）
- intent fusion：`vector`
- 静止场景：`baseline`
- 移动场景：`compound_directional`

中间曾生成若干用于发现参数不匹配的目录；它们分别缺少固定 target slot、使用
legacy fusion，或把移动单元误设为 baseline，不参与任何结论。只有上述
`final-exact` 目录是最终权威结果。最终结果与先前 20% sweep 的四个 recover/arc
文件逐项一致，且脚本 hash 全部匹配。

## 测试与二进制

- `VectorIntentFuserTests`：PASS
- `TargetPipelineIntegrationTests`：PASS
- ADS/BodyLock dynamics、TargetCoordinator、simulator、response estimator：PASS
- 左摇杆现场 harness：`PASS harness scenarios=5 defects=0`
- Release 全量构建：PASS
- CTest：`34/34` PASS
- `git diff --check`：exit `0`（仅现有 LF/CRLF 提示）

当前已覆盖 runtime：

`native/vision_native/build/Release/cod_native_runtime.exe`

SHA-256：

`DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590`

候选归档：

`artifacts/runtime-candidates/20260801-contextual-dual-proposal-headroom20-DE31FF53/cod_native_runtime.exe`

覆盖前 runtime 的可回滚备份：

`artifacts/runtime-backups/20260801-pre-contextual-dual-proposal-0E5E9A3B/cod_native_runtime.exe`

旧 SHA-256：

`0E5E9A3BBDFB98BBE564E6C72704A0E5F400A6A0C09B24C7E33E1DD412105053`

## 实机 follow-up

用户随后用当前 `DE31...` runtime 完成了约 25.5 分钟 bot 测试。强同向
dual-proposal 本身没有在日志中呈现为新的持续加法失控，但用户报告后半段仍偶有
ADS 拉过头或欠一点。

只读分析没有发现随会话时间单调增长的误差/响应漂移；更强的解释是目标晚出现或
移动穿过中心时，ADS 仍按物理 LT epoch 的 `220 ms` ceiling 强制交给
BodyLock。该问题不通过继续调整 `1.9 / 2.0 / 20%` 参数处理，也不改变本文件的
matched 仲裁结论。详细证据与后续 telemetry/fixture gate 见：

`docs/project/ADS_LONG_SESSION_DIAGNOSIS_20260801.md`。

## 实机验收重点

1. 灵敏度 2.4、近距离 ADS，同向大力推杆并快速松手，观察是否还出现弹力绳式加速。
2. 近距离 BodyLock、静止目标加主视角移动，确认 AI 跟随不被 manual 完全吞掉。
3. 移动目标切向跟随，确认横/纵操作没有被缩小。
4. 近满幅反向推杆，确认可以立即退出 AI 所有权。
5. 远距离 BodyLock，确认手感与上一版本基本一致。
