# ADS 刹车与最低追赶参考回归

最低位置输出用于避免尚未到点时过早减速。这里验证的是：它不能在刹车之后重新放大剩余需求，也不能把取消的 X 需求转移到 Y；人物框内追赶和远距离饱和拉枪仍保留。原生默认 BaseAds 另有四项回归，覆盖两条曲线、取消边界连续性、正交相机需求、最低参考及远距离力限。

`recorded_approach.csv` 提取 clean 会话 `20261018T100543Z_31772_1` 两次开镜、开火前的 50 对新鲜几何观测，保留原 JSONL 行号及 capture 时间。录像为 `Call of Duty  Black Ops 7 2026.10.18 - 18.05.47.01.mp4`；日志激活时间为 18:05:49.547 和 18:05:51.089。误差由记录的直立框及 0.20 胸部比例计算，速度为相邻 capture 的位移差。它们不是日志中缺失的生产内部响应/速度估计，因此不是完整实机数值重演。

记录回归扫描 Linear/COD Dynamic 与 500/1500/3000 px/(stick·s) 响应假设，共 92 个处于最低追赶参考控制范围的接近案例。饱和的二维力限可能合法释放另一轴的需求，故该反例只隔离最低参考重分配；饱和与范围上界另由原生随机回归检查。原版此回归失败，修复版通过。

## 复跑

先构建目标 checkout 的原生 Release。在 DEV 根目录执行：

```powershell
./tests/clean_incident_20261016/build.ps1 -ProductRoot D:/work/AI/yolo-study-001-clean -BuildName ads-braking-check -SourceRoot D:/work/AI/yolo-study-001/tests/ads_braking_20261018
Copy-Item runtime_deps/windows-x64/SDL2.dll runs/clean-incident-fix-20261016/ads-braking-check/Release/SDL2.dll
foreach ($seed in 2026101819,32452867,104395303,67867979) {
  ./runs/clean-incident-fix-20261016/ads-braking-check/Release/braking_checks.exe $seed runs/ads-braking-20261018/replay
  if ($LASTEXITCODE -ne 0) { throw "seed $seed failed" }
}
python tests/ads_braking_20261018/summarize.py runs/ads-braking-20261018
```

汇总脚本读取该目录的 `final-before` 和 `final-after`，比较逐案例行为，不生成发布裁决。原版预期因日志反例退出 1，但仍输出仿真 CSV；不能把这个预期红灯误报成原版回归通过。开发种子为 2026101819/32452867，最终实现冻结后独立验证使用 104395303/67867979。早期探索的其他种子不计入最终独立验证。

## 闭环模型与指标

实际 selector → adapter → controller → 输出交付回执 → 合成相机，1000 Hz。每种子 32 类 × 2/20 秒，共 256 例、2,816,000 tick。另用旧 incident 夹具复跑 128 例、1,408,000 tick，覆盖原日志几何、缺帧身份、0.65 连续性和混合手动场景。

新场景分静止、移动、遮挡/开火、人工介入四组；最低参考取 0.2/0.5/0.7，静止组固定 0.5。输入参数随种子生成：相机基础响应 900–2800、输出生效延迟 6–30 ms、慢开镜 250–650 ms、开镜响应比例 0.45–0.90。投影放大 1→1.4，与真实目标坐标独立；不是对该武器的实测标定。两种生产响应曲线用于合成相机。捕获 7–12 ms，结果延迟 2–12 ms，FIFO 交付；实际 runtime 使用 latest-only mailbox，两者有保真差异。

保持 1575 ms 目标槽位 + 50 ms 空隙，1500 ms 松开 LT。遮挡组含 22/35/75 ms 缺帧及固定框形变；手动组含逐轴轻推、反向、强推、释放和友军拒绝。静止组有明确敌方证据，其他组一半没有；这会影响后续原有 BodyLock 权限，不能混为同一种表现。

均检查来源年龄、32 ms 预测、40 ms 身份、原始透传、手动退出、胸部参考不受手动改写、物理输出与 ADS/BodyLock 生命周期。误差取真实投影点，不取有噪声的检测点。`mean_error` 使用每周期 100–1500 ms，`mean_late_error` / `within_4_fraction` 使用 400–1400 ms；记录失去辅助时的误差。首次进入 20/2 px 仅统计第一周期，未达到记 -1；汇总排除 -1 并单独报告缺失数。中心穿越计数使用 ±2 px 分析区间，不是生产死区。

全部逐案例输出保留。仿真检验数值不变量和行为趋势，不代表游戏效果验收；移动目标、弱证据 BodyLock、实际相机响应与遮挡检测仍有残余风险。
