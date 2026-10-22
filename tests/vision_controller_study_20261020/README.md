# 2026-10-20 新 Vision / Controller 离线诊断

结论与假设见 `docs/project/VISION_CONTROLLER_STUDY_20261020.md`。工具仅用于 dev，链接现有原生库，不打开采集/手柄设备，不改变运行中的助手或个人配置。生产代码保持不变。

## 复跑

从项目根目录执行。需要已编译的 dev Release 静态库、FFmpeg、Node、VS CMake、CUDA 13.1 与现有 TensorRT；路径写在本目录 CMakeLists / export.mjs。使用 `tests/ads_cooperation_20261019/fixture.toml` 的冻结配置。

```powershell
$studyCmake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $studyCmake -S tests/vision_controller_study_20261020 -B runs/vision-controller-study-20261020/build
& $studyCmake --build runs/vision-controller-study-20261020/build --config Release
node tests/vision_controller_study_20261020/export.mjs
node tests/vision_controller_study_20261020/prepare.mjs
$studyExe = 'runs/vision-controller-study-20261020/build/Release/controller_study.exe'
$studyProfile = 'tests/ads_cooperation_20261019/fixture.toml'
$studyOut = 'runs/vision-controller-study-20261020'
& $studyExe $studyProfile "$studyOut/open.txt" "$studyOut/open" 71020261 open "$studyOut/clean-inputs.txt"
foreach ($studySeed in 71020261,71020279,71020303,71020321) {
    & $studyExe $studyProfile "$studyOut/windows.txt" "$studyOut/final-$studySeed" $studySeed closed - 252
}
& "$studyOut/build/Release/radius_probe.exe" $studyProfile "$studyOut/radius-probe.csv"
node tests/vision_controller_study_20261020/report.mjs
```

`controller_study` 最后参数是额外输出逐 tick 轨迹的 case 编号；默认仅全片中性组输出轨迹。`cases.csv` 保留每一案例，包括纯 cue 冷启动、无目标窗口和明确退出，不用平均成绩掩盖失败。

`export.mjs` 仅对正常完成的导出写 `.records.json` 标记；重复运行跳过已完成素材。缓存属于本轮版本；换算法/模型后使用新的运行目录，不能把缓存当成新版重跑。两个主视频实际解码帧比容器少 1，允许并记录此差异；大于 1 报错。记录保留所有帧和 detection，不过滤空结果。

## 文件分工

- `export.cpp` / `export.mjs`：FFmpeg BGRA → 当前 TensorRT + selector；无 Controller 输出。
- `record.h`：中间原生字段序列化；保留 Controller adapter 使用的几何、身份、证据和时间信息。selector user intent 固定为空。
- `prepare.mjs`：窗口、来源跳变、素材覆盖与近似对齐遥测；没有改写原视频。
- `controller.cpp`：真实 adapter + Controller，开环边界检查及显式模拟相机闭环；原生算法/库不打补丁。
- `radius_probe.cpp`：ADS 先完成、几何冻结、person/cue 单变量切换，验证 BodyLock 半径。修复后要求 0 个 source-only cutoffs；出现来源切换断控或其他不变量失败时返回非零。
- `inspect_cue.cpp`：读取一个 640×512 BGRA 文件，直接列出生产独立 cue detector 的结果。
- `report.mjs`：汇总最终矩阵、来源切换事件及 SVG 轨迹。

## 已接入的录像轨迹与跟踪探针

`trajectories/` 保存 17 条可复用的屏幕轨迹及完整原生观测（2,645 帧）。目录中的 README 说明字段和来源；`overview.svg` 可直接查看路径。运行 `node tests/vision_controller_study_20261020/run_trajectories.mjs` 即可用这批素材执行现有原生 Controller 的 2/20 秒矩阵，不需要重新解码视频或运行模型。

跟踪算法的独立诊断：

```powershell
& 'runs/vision-controller-study-20261020/build/Release/tracking_probe.exe' 'tests/ads_cooperation_20261019/fixture.toml' 'runs/vision-controller-study-20261020/followup'
node tests/vision_controller_study_20261020/summarize_tracking.mjs
```

该探针输出不均匀采样下的速度估计、真实 cue 往返位移、person/cue 到达交接、带人物真值标签的双目标交叉案例。2026-10-20 修复后，零净位移偏置、cue 到达不交接和交叉串换均成为会失败的回归断言。真实 cue 三帧只有屏幕净位移参考；受速度上限影响，估计可以保守缩小，不能把它称为真实世界速度。

修复后的全量验证使用独立目录，保留旧基线：

```powershell
node tests/vision_controller_study_20261020/export.mjs runs/vision-controller-fixes-20261020/vision
node tests/vision_controller_study_20261020/run_trajectories.mjs runs/vision-controller-fixes-20261020/trajectory 71020503 71020513 71020527 71020543 71020601 71020613 71020627 71020643
node tests/vision_controller_study_20261020/summarize_tracking.mjs runs/vision-controller-fixes-20261020/final-probes
node tests/vision_controller_study_20261020/compare_fixes.mjs
node tests/vision_controller_study_20261020/replay_fresh_vision.mjs
```

`compare_fixes.mjs` 对齐原种子、逐案例参数与全片帧，保留改善和退步。`replay_fresh_vision.mjs` 按原 fixture 的帧号范围截取新 Vision 结果，只平移时间原点，避免重新挑选有利片段或改变案例时序。

## 保真边界

闭环是录像几何/证据驱动的压力模型，不是游戏引擎重演。真实像素只在导出阶段运行 Vision，后续不随输出重新渲染；原玩家相机运动未被逆推。没有运行 RuntimeLoop 的实际线程调度、设备量化、输出失联或物理硬件。capture/result 时间使用显式模拟延迟；补充视频按平均帧率近似时间。selector 没有随 Controller 的 ADS 请求/reset/手动 handover 回流重新运行。

clean 遥测只有近似时间对齐，物理 RB 与左摇杆不能完整恢复；只用作开环输入检查。高误差、符号反转、来源跳变与控制缺陷之间需要进一步因果隔离。本轮确认的半径问题已用固定几何去掉这些混淆；HUD 误识别已用原始像素复查。
