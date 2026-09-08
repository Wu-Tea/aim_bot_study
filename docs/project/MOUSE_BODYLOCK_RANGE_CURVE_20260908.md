# Mouse BodyLock 辅助范围与加减速

2026-09-08。用户提出 ADS Snap 常态化，或增加 BodyLock 辅助范围，并希望对持续移动目标有类似摇杆的加减速过程。本轮采用独立 BodyLock 范围与共享整形器的时间参数，保留 ADS 初次拉入和已有目标/手动接管生命周期。

## 配置与行为

当前 `config.toml`、`config.native.example.toml` 已加入：

```toml
[mouse]
bodylock_range_px = 180.0
bodylock_accel_ms = 40.0
bodylock_decel_ms = 25.0
```

- `bodylock_range_px`：已选目标持续辅助的基础半径，16..2048 px；0 继承 `[gamepad.bodylock].activation_range_px`。实际范围仍采用共享几何公式 `base × (1 + 0.75 × normalized_target_size)`，近距离大目标更宽。像素单位与识别结果/控制误差一致。它不扩大截图，不改变新目标的 Vision 选择范围，也不赋予不可见目标辅助权。
- `bodylock_accel_ms`：AI 从零到该轴配置最大跟随速度的时间，1..250 ms；0 保留原共享加速率。小幅速度变化用时按幅度缩短。40 ms 是可调初值，不是已完成游戏 A/B 的最佳值。
- `bodylock_decel_ms`：AI 从该轴最大速度降到零的时间，1..250 ms；0 保留原共享减速率。默认 25 ms，制动快于加速；反向先卸掉旧方向，再按加速率进入新方向。

本轮范围/加减速修改保留了零误差时的目标运动前馈。随后按用户“小距离可以忽略”的反馈增加了 [目标点容差策略](MOUSE_TARGET_POINT_CONTROL_20260908.md)：默认 3 px/轴内停止位置与运动追逐，容差外持续纠偏；设 `bodylock_point_tolerance_px=0` 才保留原零误差前馈。这里的加减速仍控制 AI 输出随时间的变化，鼠标 counts 的灵敏度换算保持线性。

当前其他默认值保持：`speed=2`、`breakaway=4`、空间死区 0.5、压枪 30 counts/秒且默认要求 ADS。改配置后重启；`scripts/launch/mouse_start.bat --check-config` 可无设备读回。

如果要回到本轮修改前的范围/加减速，将以上三项设为 0。仅使用原来的 `--mouse-speed 1 --mouse-breakaway 1 --mouse-bodylock-deadzone 0` 不会重置这三项。

## 为什么没有把 ADS Snap 常态化

ADS 负责一次瞄准请求中的目标拉入，BodyLock 负责到达后的持续跟随。反复启动 ADS 会改变完成/消耗、手动 D 修正和目标切换的语义。扩大 BodyLock 并调整它的速度过渡可以满足这次持续辅助的目标，而无需重新解释 ADS 生命周期。

检查发现 BodyLock 原本已有范围和 `AimDynamicsShaper` 加减速。当前鼠标沿用的基础范围是 120 px；原共享整形器使用固定归一化 slew 值和每 tick 上限，按鼠标速度预算在 1 ms 控制周期下通常几毫秒便到顶。本轮将范围从鼠标配置送到同一 coordinator/reacquisition 所有者，并在同一个整形器内按 BodyLock 轴速度上限和 elapsed dt 计算时间坡度；没有增加第二个瞄准控制器或输出通道。

加减速只控制已获授权的 AI 提案。目标丢失、松键、手动接管仍由最终裁决立即生效，不让旧 AI 因减速而继续移动；视觉权重下降时，旧速度立即受新的输出预算限制。Cue continuation 不能在缺少直接观察时盲目加速。普通 gamepad 的默认整形参数以及 ADS 曲线不启用此次 mouse 参数。

## 日志与验证范围

`session.json` 新增三个生效参数；逐 tick 日志新增 `bodylock_range_px`（动态半径）、`aim_authority`（实际辅助权重）、`decision_reason`（共享枚举值）。已有 `requested_u`、`shaped_u`、`aim`、`final` 可将速度需求、坡度和整数 counts 对齐。

离线 feature baseline 与候选证据在 `runs/mouse_bodylock_curve_20260908/`。基线相对这次新增功能预期是 RED，不代表精确复现了一段未记录的游戏故障。

- 小目标偏离 160 px，原 120 px 基础范围输出为零；180 px 候选在同一 BodyLock 状态产生 X/Y 跟随 counts。
- X/Y、1/2/4/8 ms 控制周期验证 40 ms 加速、25 ms 减速；24 ms 时均到达最大轴速度的 60%。
- 全鼠标 facade 在 1/16/33 ms 视觉更新下验证坡度约束、非零实际输出与同 tick 松键/丢目标原量透传。
- 共享组件覆盖反向先制动、权重收缩、Cue 不升速、目标更换、ADS 不变，以及零误差移动目标的持续前馈和停止制动。

这些是离线行为与回归检查，尚未完成真实游戏的匹配 A/B 和手感验收。更大的范围可能让辅助介入更远；更慢的加速会增加追上目标的时间。需要结合新日志和实际手感调整，不能仅凭平滑曲线宣称追踪更准确。
