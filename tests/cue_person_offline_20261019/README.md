# Cue/person 离线实验，2026-10-19

只在本目录与 `runs/cue-person-offline-20261019/` 新增实验文件。不修改生产 Vision、模型、配置或 Profile，不连接控制设备，不替换运行中的程序。

报告和交互对照页：

- `../../runs/cue-person-offline-20261019/REPORT.md`
- `../../runs/cue-person-offline-20261019/review.html`

## 实验边界

`experiment.cpp` 读取 ffmpeg 提供的 640×512 BGRA 原始帧，调用现有 480×384 TensorRT 模型。当前生产 selector 源码编译到实验 executable，现有 GPU 预处理及推理库仅链接使用。baseline 保留原有 required_color_region、色彩标注和 selector 生命周期。

实验 cue 路径不读取 person 框：对整张截图作原有红/黄 HSV 分类，8 邻域连通域，过滤大小、长宽比、填充率、八方向覆盖和中心偏移。返回独立候选列表。关联层使用同帧 person（沿用 baseline 友军排除），按人体尺度归一化的空间成本作双向最佳匹配，差距不足 0.12 时保留歧义。候选参数在查看验证结果前固定，没有依据验证集调参。

这是 cue 观测和同帧关联实验。候选不接入 selector，不新建身份、不补人体、不生成胸部或控制输出，不宣称目标连续性、首锁速度或实机效果改善。

## 数据与标注

- 开发：`output/cue-review-20261014/` 的 6 张历史问题截图，其中 4 张有已有圆点位置参考。
- 验证：当前 NVIDIA COD 目录下的 9 段录像，共 5,435 帧。完整文件名、帧率、帧数见输出 `sources.json`。
- 固定验证帧：各片从名义 0.5 秒起，每 2 秒取一帧，索引 `round(t * average_fps)`，排除末尾不足 0.1 秒部分，共 46 帧。精确复现按 frame index；不把平均 FPS 时间当原始 PTS。
- 参考标注：助手逐图目视估计，通常±2px，未获独立人工复标。用户确认圆点与COD死敌骷髅均为有效cue后，两张原待定帧均纳入，最终46帧/12个有效cue。`labels.json`保存当前范围、阳性和阴性默认规则，6px为主要定位容差。initial/circle-only输出保留更早的统计范围。没有全片真值。
- 诊断补充：各片第一个“候选有配对、旧方法无 per-person cue”的帧，9 帧；用于寻找反例，不混入固定抽样准确率。结论保存在 `supplementary-labels.json`。
- 旧截图的历史 baseline 来自旧连续回放，不与本轮单帧冷启动结果混用。9 段验证录像则逐帧共享完全相同的 person 推理结果，公平比较当前基线与候选。

## 复跑

Windows，Node.js、ffmpeg/ffprobe、VS 2022、CUDA 13.1、TensorRT 10.15。CMakeLists 中路径对应当前工作站。全部工具为原生 C++ 或 Node 编排，无 Python Vision 路径。

```powershell
$cueCmake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cueCmake -S tests/cue_person_offline_20261019 -B runs/cue-person-offline-20261019/build -A x64
& $cueCmake --build runs/cue-person-offline-20261019/build --config Release
node tests/cue_person_offline_20261019/run.mjs prepare
node tests/cue_person_offline_20261019/run.mjs development
node tests/cue_person_offline_20261019/run.mjs videos
node tests/cue_person_offline_20261019/evaluate.mjs
node tests/cue_person_offline_20261019/inspect.mjs
node tests/cue_person_offline_20261019/run.mjs supplementary
node tests/cue_person_offline_20261019/run.mjs sheets
node tests/cue_person_offline_20261019/run.mjs visuals
node tests/cue_person_offline_20261019/review.mjs
node tests/cue_person_offline_20261019/run.mjs baseline-check
```

`prepare` 的 PNG 输出不覆盖已有图片；已有 sources/samples 时直接从后续步骤继续。录像目录未来增加文件时，重新 prepare 会改变来源编号与样本集，旧 labels 不得沿用；先固定原 sources 或在新目录开展新实验。不要把这次小样本的结果当新视频的结论。

## 验证

Release 构建成功；5,435 帧计数和连续索引一致；全部候选配对满足一 cue 对一 detection、索引有效。第一段 854 帧还通过原有 `cue_video_review.cpp` 独立基线核对：目标存在性、来源、代际、选中索引、检测状态全部一致；坐标序列化最大差 0.00005px，来自 7/9 位输出精度差。见 `baseline-check.json`。

实测 CPU cue 提取中位数约 2.15ms、P95 约 4.03ms，不含实机完整 ROI GPU→CPU 读回，也不是端到端延迟。此候选只为机制实验，没有做性能优化。
