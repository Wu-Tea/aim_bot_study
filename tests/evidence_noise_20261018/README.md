# 观测降噪与小修正回归

本夹具链接目标 checkout 的实际生产库。`braking.cpp` 的随机组经过 selector、adapter、coordinator、生产 ADS/BodyLock、输出整形、手动仲裁、交付回执与合成相机。另有已知相机响应的聚焦组，区分测量职责与相机辨识误差。

开发种子为 2026101841、32452897；控制实现冻结后才运行独立种子 49979711、104395367。每种子 80 个随机场景与 20 个已知相机场景，时长各 2/20 秒，合计 1,100,000 个 1 ms tick；四种子每版本 400 例、4,400,000 tick。

## 场景与假设

- 随机组五类各八种：居中噪声、小偏差、慢速二维移动、开火/遮挡、人工输入/阶段交接。两条生产曲线，最低追赶 0.2/0.5/0.7，权限、生命周期和模型完全使用生产实现。
- 随机相机响应 900–2800，真实生效时间 6–30 ms，捕获间隔 7–12 ms，处理延迟 2–12 ms。先验与真实模型可以不一致。末组还包含 250–650 ms 开镜投影变化、弱证据、手动反向及友军拒绝。
- 固定 1575 ms 目标槽位与 50 ms 空隙，1500 ms 松开 LT。噪声为独立均匀位置抖动；慢速移动为 X=8/-18、Y=3 px/s；开火包含连续上跳和每 80 ms 的 2.5 px 冲击，另有框形变和 0/22/35/75 ms 缺帧。
- 已知相机组固定响应 2500、生效 9 ms、capture 10 ms，关闭学习以隔离误差来源。持续开镜 2/20 秒，验证居中 ±0.35 px 测量噪声、小偏差 (1.5,-2.2)、带 ±0.2 px 噪声的 8/-3 px/s 移动、固定压枪和 ADS→BodyLock。小偏差的非线性曲线另有 ±0.1 px 噪声。
- 聚焦组数值断言：居中 RMS <0.5 px，小偏差后段均值 <0.5 px，慢移 <1 px，远点发生 ADS→BodyLock。开火记录全部数值；胸部、手动退出、时效、透传与物理输出由全链和原生单测检查。这些不是实机验收标准。
- `--noise-off` 只用于外置夹具的消融，产品不新增开关。此对照将现有配置设为 0 来测量观测修正的实际作用；发布配置仍保留 3%。
- 沿用实机日志的 93 个 capture 与 92 个生产 solver 组合反例。旧版 3% 卡住小修正的反例位于原生 `credible_small_correction_is_not_output_noise`，以及已知相机小偏差/交接闭环，修复前应失败。

## 复跑

先编译目标 checkout，再在 DEV 执行：

```powershell
./tests/clean_incident_20261016/build.ps1 -ProductRoot D:/work/AI/yolo-study-001 -BuildName evidence-replay -SourceRoot D:/work/AI/yolo-study-001/tests/evidence_noise_20261018
Copy-Item native/build/Release/SDL2.dll runs/clean-incident-fix-20261016/evidence-replay/Release/SDL2.dll
foreach ($trialSeed in 2026101841,32452897,49979711,104395367) {
  ./runs/clean-incident-fix-20261016/evidence-replay/Release/braking_checks.exe $trialSeed runs/evidence-replay
  if ($LASTEXITCODE -ne 0) { throw "seed $trialSeed failed" }
}
python tests/evidence_noise_20261018/summarize.py runs/evidence-noise-20261018
```

聚焦组后段从 500 ms 开始；随机组均值取每槽 100–1500 ms，后段取 400–1400 ms。首次进点统计缺失为 -1，不能只保留成功例；穿越次数随时长变化。逐案例 CSV 与汇总 JSON 同时保留，包含退步场景。

## 保真边界

日志没有完整内部状态，不能称完整实机重演。相机、噪声、后坐力与开镜模型是明确的合成假设；FIFO 交付和浮点杆量也没有覆盖真实 latest-only mailbox 与设备量化。仿真成功不代表硬件能执行任意微小输出，也不等于实机验收。参考前轮整体停稳夹具做相邻路径回归，但不以多跑场景数掩盖单例退步。
