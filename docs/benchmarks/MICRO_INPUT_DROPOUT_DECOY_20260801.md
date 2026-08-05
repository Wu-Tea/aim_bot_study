# 微输入 + 短遮挡 + 诱饵目标 benchmark

## 目的

这套场景复现一个实战缺陷：玩家只给约 3% 的右摇杆微调，BodyLock/ADS 却沿相同错误方向继续加力；与此同时，短时机瞄遮挡会让 Vision 暂时只看到约 200 px 外的候选目标。它用于区分三类影响：

- 人手微输入与 AI 同向叠加；
- 短遮挡和错误候选对 ADS/BodyLock 稳定性的影响；
- Remaining Work 在 Vision 发布延迟下是否形成脉冲式状态。

场景保持 LT 和逻辑目标持续有效，不用 ADS 重触发制造问题。目标保持静止，以免把目标运动预测误差混入结论。

## 固定输入

- 运行时间：60 秒；
- seeds：`2026080101, 2026080102, 2026080103`；
- controller rate：1000 Hz；
- Vision：160 Hz；
- capture-to-publication delay：6 ms；
- 人手输入：水平 `0.03`，320 ms 后反向，640 ms 后归零；
- 遮挡窗口：每个目标槽内 `[110, 180) ms` 和 `[360, 410) ms`；
- 遮挡时：主目标不发布，发布约 200 px 外的 decoy；
- cohorts：ADS、BodyLock；
- target profile：near；
- fusion：vector。

## 运行命令

```powershell
native\vision_native\build\Release\cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml `
  --duration-ms 60000 `
  --seed 2026080101 --seed 2026080102 --seed 2026080103 `
  --profile micro --cohort both --target-profile near `
  --vision-hz 160 --vision-result-delay-ms 6 `
  --vision-disturbance dropout-decoy `
  --intent-fusion vector --remaining-work current `
  --output artifacts/benchmarks/micro-dropout-delay6-current-20260801-3seed.json
```

把 `--remaining-work current` 改为 `integrated`，并更换输出文件名，即可做 Remaining A/B。

## 新增诊断指标

- `wrong_near_center_stack_ticks`：准星距目标不超过 40 px、人手正在推离目标且 AI 同向加力的 tick 数；
- `max_wrong_stack_ratio`：上述时刻最终水平输出相对人手输入的最大放大倍数；
- `fresh_vision_output_jump_events`：新 Vision 结果到达时最终输出跳变至少 0.05 的次数；
- `remaining_valid_ticks` / `remaining_valid_transitions`：Remaining 有效时长与有效性边沿数；
- `max_remaining_work_step_px`：相邻控制 tick 的 Remaining 最大步变；
- `controller_target_switches` / `decoy_vision_frames`：逻辑目标未换时 controller 换目标次数，以及 decoy 发布帧数。

## 2026-08-01 基线结果

| 模式 | Remaining | Tracking points | Mean error | Settled | Stall-ring | Handoff defect | Wrong-stack ticks | Max wrong-stack ratio |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ADS | current | 51,962.9 | 2.760 px | 39.0 | 544.3 ms | 0.3470 | 218.7 | 4.00x |
| ADS | integrated | 51,935.5 | 2.773 px | 39.0 | 530.0 ms | 0.3294 | 236.7 | 3.79x |
| BodyLock | current | 56,142.8 | 1.347 px | 57.0 | 190.3 ms | 0 | 489.0 | 5.13x |
| BodyLock | integrated | 56,159.0 | 1.321 px | 57.3 | 191.3 ms | 0 | 461.3 | 4.67x |

Integrated 模式平均每分钟约有 8.4k 个 Remaining-valid ticks、16.7k 次有效性边沿；current 模式为 0。它证明 Remaining 在当前观测模型中具有高频脉冲语义。但两组核心成绩基本相同，因此不能把整类 ADS/BodyLock 跳变归因于 Integrated Remaining 一个开关。

短遮挡相对无扰动基线会明显恶化 ADS：settled targets 从 56.3 降到 40.7，handoff defect rate 从 0.159 升到 0.365；BodyLock mean error 从 0.862 px 升到 1.253 px，stall-ring 从 6 ms 升到 138 ms。微输入的错误同向叠加在无扰动场景中也存在，因此它不是 decoy 才产生的假象。

## 当前结论

这套 benchmark 已经稳定覆盖实战证据链中的关键现象，但因果结论是组合性的：微小错误人手输入会被 AI 同向放大，短遮挡会显著恶化状态衔接，而 Remaining 是高频状态载体之一。下一步控制修复应让“连续相机位移估计”和“Remaining 是否有权驱动输出”分离，再用本场景验收；不能只调低全局强度或只切换 Remaining 模式。

## 最终修复：capture alignment 与 Remaining authority 分离

根因是 BodyLock 禁用了逐 tick Remaining 闭环后，仍在每个控制 tick 清空 Remaining；下一张 Vision 帧又通过 capture-work 把它重新设为有效，形成约 160 Hz 的 `valid → invalid → valid` 脉冲。修复后：

- ADS 保留逐 tick Remaining，用于一次定位；
- BodyLock 不再积分逐 tick delivered work；
- BodyLock 仍用 capture-work 将旧 Vision 坐标对齐到当前时刻；
- capture-work 不再授予 BodyLock Remaining 控制权；
- ADS→BodyLock 只清理一次权限，之后不在每个 tick 重复 reset。

固定三 seed、6 ms Vision 发布延迟、短遮挡/decoy 场景结果：

| 指标 | 修复前 | 修复后 | 变化 |
|---|---:|---:|---:|
| ADS Remaining valid transitions / min | 16,692.3 | 2.0 | -99.99% |
| BodyLock Remaining valid transitions / min | 16,684.0 | 2.0 | -99.99% |
| ADS handoff defect rate | 0.3294 | 0.2679 | -18.7% |
| ADS wrong-stack output area | 21.20 | 13.73 | -35.2% |
| ADS fresh-Vision output jumps | 1,156.7 | 1,121.0 | -3.1% |
| BodyLock stall-ring | 191.3 ms | 0 ms | -100% |
| BodyLock tracking points | 56,159.0 | 56,000.3 | -0.28% |

ADS tracking points 从 51,935.5 降到 49,698.6（-4.31%），说明旧脉冲确实贡献了额外的实验室追踪力度。没有用新的强度 gate 把这部分错误推力补回去；后续若要恢复 ADS 分数，应调整 ADS→BodyLock 的合法连续控制，而不是恢复 Remaining 脉冲。

修复后证据文件：`artifacts/benchmarks/micro-dropout-delay6-integrated-alignment-only-20260801-3seed.json`。
