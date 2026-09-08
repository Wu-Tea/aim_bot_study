# Mouse BodyLock 目标点纠偏与小距离容差

2026-09-08。对应用户反馈：左右横移、准星仍在 personbox 内时感觉 AI 不发力；目标点附近持续抖动；很小的距离可以忽略。

## 行为和使用

`config.toml` 与 `config.native.example.toml` 新增：

```toml
[mouse]
bodylock_point_tolerance_px = 3.0
```

这是 **每轴目标点误差**的容差，单位为 Vision 原生截图坐标像素，不是模型 tensor 像素、鼠标 counts 或人物框比例。正值范围为 `>0..16`，默认 3；0 恢复原共享 BodyLock 位置/运动策略。修改后重启 `scripts/launch/mouse_start.bat`；加 `--check-config` 可无设备检查生效值。

- 控制继续使用同一 TargetPlan 的 D（最终目标点，包含明确手动修正），不重新选择目标。X/Y 独立判断，偏离 D 超过容差即可请求纠偏，准星在人物框内也一样。
- 容差内，该轴的位置与运动补偿需求都归零；已有 AI 速度通过现有 `bodylock_decel_ms` 减速。不能只把位置项归零，却在零误差处重新释放全部运动项。
- 容差外保留按实际误差计算的位置请求。反向速度补偿最多抵消一半位置请求，较远处还保留原有更强的位置约束；同向补偿最多等于位置请求。最后仍服从视觉权重、二维输出预算和现有加减速，**不强制每 tick 产生一个整数 count**。
- 3 px 边界是明确的接受范围，位置请求在边界外恢复；最终速度由既有时间整形器过渡，没有另加延迟或丢弃源输入。持续目标运动会在容差外形成跟随误差；容差内不再为了预测运动而无限追逐小偏差。这是本次“小距离可以忽略”的策略选择，不是改变鼠标灵敏度。
- 手动慢拖、明确接管、松开 RMB、丢目标和独立压枪沿用各自所有者。容差不削掉压枪计数，不改原物理鼠标的采集/替换路径，也不改变普通手柄或 ADS 的默认策略。

速度仍为 2，BodyLock 范围 180 px、加速 40 ms、减速 25 ms。较大的点容差会减少细小修正，也会接受更大的跟随偏差；这些默认值尚未经过真实游戏的匹配 A/B 优化。

## 实际证据与根因范围

读取了最新完整运行 `1788860541589161-54744`，其 executable SHA-256 为 `f05076c918f477c07c29efce3bba0436bb4add483d07c9b991e92064ef947d6d`，与修改前磁盘 exe 相同。215371 行均写出，summary 无丢失、预算耗尽或写入失败。证据预检文件及输入哈希在 `runs/mouse_target_point_20260908/audit-intake.json`。

预检结果为 **INSUFFICIENT_EVIDENCE**：硬件档案与游戏刷新率未记录。这批资料可检查控制器内部输出，但不能完成游戏画面抖动的独立归因或原生 A/B。鼠标 schema 1 的配置/模型哈希分别叫 `config_sha256`、`model_sha256`，不是旧遥测的同名字段；本报告显式使用 session 元数据，不把未记录字段当作零。

按 `mode=body_lock`、物理 `source=[0,0]` 分段，控制日志中：

- 7593 个 tick 同时满足准星在记录的 R 内、横向误差绝对值大于 3 px；全部有非零横向请求。因此不支持“框内统一停止 AI”这个实现假设。注意旧日志 `box_xywh` 是目标允许区域 R，不是完整 personbox。
- 3087 个 tick 两轴误差均不超过 3 px，其中 373 个仍有 `aim` 计数。这是内部输出特征，不能单凭它认定所有移动都是画面抖动。
- 存在横向误差约 3–5 px、视觉权重约 0.04–0.07 的片段，最长观察到约 95 ms 没有横向整数输出。旧版还会因接近零点的反向速度补偿进一步削弱位置项。弱视觉权重本轮仍受保留，不因主观“不发力”而放开证据约束。

源代码路径为 `TargetCoordinator` 生成 D−准星误差 → `BodylockFollowController` → `solve_response_model_aim` → `AimDynamicsShaper` → mouse 裁决和计数转换。原 solver 没有点容差，接近零点时反向运动可抵消大部分位置需求，而位置恰好为零时又允许全部运动前馈。本次修改放在该位置/运动合成所有者，通过 mouse 参数启用；不在最后输出端遮盖错误，不更改源目标几何。

## 回归与日志

同一份 `mouse_target_point_tests.cpp` 先在旧生产库上建立 RED，再用于候选 GREEN。旧二进制、fixture 哈希和报告保留在 `runs/mouse_target_point_20260908/`。

- 300×700 目标、固定身份、M=0、BodyLock 已建立，1/16/33 ms 源更新，±2 px 点波动。
- 独立控制证明 12 px 偏差时准星仍在 R 内，并在 X/Y 正负方向产生纠偏计数。
- 直接 solver 检查反向运动不吞掉超过一半的位置项，以及零点/容差内不产生运动驱动。
- 附加边界检查覆盖容差边缘、单轴独立、权重撤销、持续位移后停止、1/2/4/8 ms tick、手动慢拖、压枪与同 tick 松键/丢目标。闭环 plant 是明确声明的线性合成输入，不是实战重放。

初版夹具把 1 ms 源更新同时用作 1 ms 噪声换向，整数相互抵消，导致旧版对照也为零。已保留 `red.jsonl` 和初版二进制，并在**改生产代码前**将噪声变化间隔冻结为 `max(16, source_period)` ms，修正的是无效对照条件；正式证据为 `red-v2.jsonl`，后续不改变该夹具/阈值。

逐 tick 日志增加 `source_aim_px`、`desired_point_source`、`point_tolerance_px`、`point_inside`、`bodylock_position_u`、`bodylock_motion_u`、`bodylock_effective_motion_u` 和 `selector_generation`。原 `generation` 是每 tick 生成的 plan 版本；`selector_generation` 是源发布的身份代次，非新源 tick 可为 0，不能将两者混同。用这些字段结合 `requested_u → shaped_u → aim → final → submitted` 可定位目标点、补偿、整形、压枪和实际传输分别做了什么。

离线检查通过不等于实战手感确认。仍需要启动新 exe 后的同条件原生 A/B，以及用户对横移跟随和点附近稳定性的反馈。

本次 Release 构建成功，`ctest -R '^(Mouse_|Base|Feature)'` 34/34 通过；Python 启动与诊断测试共 30/30 通过。正式 26 行目标点夹具全部 GREEN，其中 ±2 px 噪声的 6 个条件由旧版 16–52 counts 降为稳定后的 0；12 个框内偏离控制全部非零。额外 16 组合成移动/停止检查均通过，停止尾段输出为零。配置只读检查确认 `point_tolerance_px=3` 已进入正式 exe。
