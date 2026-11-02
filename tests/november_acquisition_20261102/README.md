# 11 月 2 日录像缺陷复现

输入是 `runs/incident-20261102` 中保留的日志、录像索引，以及 10 月 25 日的五段历史回放。输出保存在该目录的 `fix/` 下。脚本只做离线选择器回放，不连接手柄或发送游戏输入。

## 已修复的边界

- 人框与 cue 错开出现：近期唯一可信人框为稍后出现的唯一 cue 提供局部搜索和临时身体参考。沿用 180 ms 的近期证据窗口，运动投影限制为 40 ms；没有当前 cue 不输出，cue 接续只授予瞄准权限。重新观察到同一 cue 对应的真实身体时，立即替换临时参考。
- 友军误判：颜色占比不再直接否决人物。局部连通标记的完整性、形状和身体上方位置共同提供友军证据；保留友军开关及自定义颜色。
- 远处目标：首次获取与跟踪共用原跟踪几何下限，消除可跟踪但不能获取的差异；保留原跟踪下限，避免头部碎片缩小活动身份。没有更改置信度、确认次数或获取范围。
- box 的 25% 点停在空气：尺寸与原始标定一致、当前唯一 cue 对应的真实人框可更新身体参考。仍拒绝标记残框、逐帧膨胀及歧义配对。

`november_acquisition_tests.cpp` 注册在原生 BaseVisionSelection 中，覆盖上述四类故障、四个随机种子、短/长 cue 序列、缺口、歧义、过期、重置、真实身体恢复，以及小/暗友军圆环和菱形。四组用例在冻结的修复前源码上均失败。

## 复现

先执行 `python tests/november_acquisition_20261102/prepare_baseline.py`，把本次固定的修复前 dev 基线 `e03908193610d5ea5996d21f5a448695ab18f9e8` 导出到输出目录；不会切分支或覆盖工作区源码。提交修复之后重跑也仍使用该基线，不随 HEAD 改变。

使用 Visual Studio C++ 工具链配置本目录 CMake 工程到 `runs/incident-20261102/fix/build`，以 Release 构建后运行：

```powershell
& runs/incident-20261102/fix/build/Release/cases_before.exe
& runs/incident-20261102/fix/build/Release/cases_after.exe
python tests/november_acquisition_20261102/replay_video.py
python tests/november_acquisition_20261102/replay_adjacent.py
python tests/november_acquisition_20261102/inspect_adjacent.py
```

后两个图像回放脚本需要原始本地录像、FFmpeg；本次录像回放还需要 OpenCV 和 NumPy。旧 q1/q2 材料沿用原回放的 HDR→SDR 转换。模型输出固定为保存的框，不重新推理。`inspect_color.py`、`color_probe.cpp`、`check_first_cue.py` 是本次调查的像素探针。

## 解释结果的限制

录像为 120 fps，原采集约 200 fps；对齐使用 −0.1375 s 修正及约 ±25 ms 复核容差。压缩、色彩和帧采样会改变 cue 检测，尤其 E76 回放的首次获取时间与原日志不一致。因此增加的有目标帧数不是准确率，也不能宣称录像中的整个缺口均已消除。E76 的数值故障另用相同记录框/标识坐标独立复现。

性能 CSV 的 `ms` 仅为 CPU 选择器耗时，不包括采集、GPU 推理、回读或控制链；测试成功不代表实战验收。此次没有改变模型、检测频率、控制器、Profile 或手动权限。垂直跟随 E70 尚未证明控制根因，不能用这些视觉修复宣称其已解决。

完整候选程序在 `runs/incident-20261102/fix/runtime-build/Release/` 单独构建，基于当前 dev。录像日志声明的 `1631041` 实验版本只用于历史调查，没有恢复；原 `native/build/Release/cod_native_runtime.exe` 未覆盖。
