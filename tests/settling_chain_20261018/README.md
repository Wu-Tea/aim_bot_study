# 整体停稳链路回归

本夹具链接目标 checkout 的实际生产库，覆盖 selector → adapter → coordinator → ADS/BodyLock → 输出整形/仲裁 → 交付回执 → 合成相机。原生 BaseAds/BaseBodyLock 另有五项回归：开火不能擦掉接近速度、已交付输出与整形余量必须消耗剩余位置预算、ADS 完成不能切换物理相机先验、响应改变不能改变已估计目标的物理速度，以及有目标漂移时的响应/时差联合辨识。

`recorded_settling.csv` 来自 clean `20261018T123137Z_25292_1` 最后一次 ADS，保留 93 个 capture、原始行号、源几何及时间；对应 `20.31.45.02.mp4` 的最后三秒，视频和日志通过画面运动相关性对齐。将真实观测输入生产 TargetCoordinator，验证开火状态下输出的接近速度仍来自相邻 capture。旧 `e08cb92` 必须失败，仍继续输出模拟 CSV；不能把这个预期失败称为原版测试通过。另复用前轮 92 个生产 solver 最低追赶/刹车组合反例。

日志未记录完整历史配置、内部模型和逐 tick 游戏状态，所以这不是完整实机重演。首次持续 50 ms 位于 4 px 内只是离线分析指标，生产控制中没有新增 50 ms 等待。

## 复跑

先编译目标 checkout 的 Release，然后在 DEV 执行：

```powershell
./tests/clean_incident_20261016/build.ps1 -ProductRoot D:/work/AI/yolo-study-001 -BuildName settling-replay -SourceRoot D:/work/AI/yolo-study-001/tests/settling_chain_20261018
Copy-Item native/build/Release/SDL2.dll runs/clean-incident-fix-20261016/settling-replay/Release/SDL2.dll
New-Item -ItemType Directory -Force runs/settling-replay | Out-Null
foreach ($trialSeed in 2026101829,49979693,86028157,104395337) {
  ./runs/clean-incident-fix-20261016/settling-replay/Release/braking_checks.exe $trialSeed runs/settling-replay
  if ($LASTEXITCODE -ne 0) { throw "seed $trialSeed failed" }
}
python tests/settling_chain_20261018/summarize.py runs/settling-chain-20261018
```

汇总脚本读取 `development-before` / `development-final` 和 `independent-before` / `independent-dev` 四个结果目录，保留全部逐案例前后值。开发种子 2026101829/49979693；实现冻结后的独立种子 86028157/104395337。每种子 32 类 × 2/20 秒、704,000 tick；每版本合计 256 例、2,816,000 tick。原 incident 夹具另跑相同四种子的 128 例、1,408,000 tick，覆盖几何、0.65 边界、32/40 ms、释放和手动退出。

## 场景与保真边界

- 固定 1000 Hz 控制；1575 ms 目标槽位 + 50 ms 空隙，1500 ms 松开 LT；不改变生产生命周期。
- 四组各八种：静止/慢开镜、二维移动、开火/遮挡/框形变、逐轴人工介入。相机基础响应 900–2800，真实输出生效延迟 6–30 ms，慢开镜 250–650 ms，开镜响应比例 0.45–0.90；这些不是该武器的实测标定。
- 两种生产曲线；最低追赶参考 0.2/0.5/0.7（静止固定 0.5）。移动速度 X 为 75 或 140 px/s 正弦，Y 为 30 px/s 余弦；弱证据场景可能超过既有 BodyLock 可用力限，不能提高权限来改善结果。
- 捕获 7–12 ms、结果延迟 2–12 ms；夹具 FIFO 与生产 latest-only mailbox 有差异。投影放大 1→1.4，与目标真实坐标独立。开火物理模型含持续上跳与每 80 ms 的 2.5 px 冲击，固定压枪参与实际输出；不是实测弹道。
- 缺帧 0/22/35/75 ms、框形变、友军拒绝、轻推/强推/反向/释放。逐 tick 检查胸部来源不被手动改写、物理输出、原始透传、手动退出、ADS/BodyLock 所有权，以及原 capture 的 32 ms 预测和 40 ms 身份边界。
- 误差对真实投影点计算。平均误差取每周期 100–1500 ms，后段取 400–1400 ms；首次到 20/2 px 与首次持续 50 ms 在 4 px 内仅统计第一周期，未达到记 -1。汇总同时报告有效均值与缺失数，不能只比较成功子集。
- `post_handoff_peak` 是每次交接后 100 ms 内的最大误差，无交接时为 0；因此组均值只作辅助，交接次数也一并保留。短长场景等权；穿越次数跨持续时间不能当频率。

仿真证明数值约束和行为趋势，不代表实机验收。目标形变、突然机动、FOV/响应辨识误差以及有限手动/视觉权限仍可能产生残差。
