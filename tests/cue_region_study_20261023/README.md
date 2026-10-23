# cue区域跳搜离线实验

工具只生成测试专用detector/selector，不修改助手，不输出手柄。历史区域实验的冻结基线是2026-10-23第二版行段/整数cue优化工作区，保留在runs/cue-region-20261023/reference。生产接入后，为使全新checkout可以复跑，prepare.py默认从已保留提交f4b9cbe读取全图实现；它是第一版，耗时不能与历史第二版表格混用。可传入其他区域优化前的Git revision及新输出目录。

## 固定底部排除

在仓库根目录执行（CMake使用本机VS2022所带版本）：

```powershell
python tests/cue_region_study_20261023/prepare.py
cmake -S tests/cue_region_study_20261023 -B runs/cue-region-20261023/build
cmake --build runs/cue-region-20261023/build --config Release -- /m:1 /nr:false
node tests/cue_opt_20261023/videos.mjs runs/cue-region-20261023/videos runs/cue-region-20261023/build/Release/replay.exe
python tests/cue_region_study_20261023/summarize.py runs/cue-region-20261023/videos
```

测试只限制新区域种子的发现行，分别为512/448/384/320/288，即排除底部0/12.5/25/37.5/43.75%。仍完整展开跨界同色区域，保留真实图像边界和准星中心；不能直接缩短传入height，否则会改变形状和命中标记排除规则。省去的搜索行比例不等于实际像素访问或耗时比例。

所有版本使用独立的持续selector状态、同一帧像素、同一次TensorRT输出和时间戳；六路测量顺序逐帧轮换。全高度测试克隆每帧必须与当前native库一致。CSV分别记录候选增减、完整结果差异、去除检测框列表后的选中状态差异、原版/实验版目标有无和generation。单次generation变化可能使后续大量帧记录不同，因此selected_diff不能当成丢目标帧数。lost_target_frames才表示同帧原版有目标、实验版无目标；它也不是人工标注的真实敌人漏检数。

## 按现有消费者的几何范围跳搜

先运行上面的prepare.py冻结第二版，然后：

```powershell
python tests/cue_region_study_20261023/prepare_relevance.py
cmake -S tests/cue_region_study_20261023 -B runs/cue-relevance-20261023/build '-DBASE=D:/work/AI/yolo-study-001/runs/cue-relevance-20261023/reference' '-DREPLAY_SOURCE=relevance.cpp'
cmake --build runs/cue-relevance-20261023/build --config Release -- /m:1 /nr:false
runs/cue-relevance-20261023/build/Release/geometry.exe
node tests/cue_opt_20261023/videos.mjs runs/cue-relevance-20261023/videos runs/cue-relevance-20261023/build/Release/replay.exe
python tests/cue_region_study_20261023/summarize_relevance.py runs/cue-relevance-20261023/videos
```

搜索区域来自现有规则：每个符合关联基本尺寸/class条件的person，其cue关联外边界；已锁定cue上一位置±42像素；涉及搜索时保留准星±36像素的完整四角命中标记组合。各区域外扩9像素容纳最大18像素cue的完整形状，逐种子行合并区间，其余整段跳过。仍完整消耗命中的连通区域并保留光栅顺序。不对person新增置信度筛选，不降低频率，不用上一帧ROI代替当前新person范围。

生成源码中的全局实验开关/区域只用于单线程离线比较；若移植生产，应由selector传入每次检测的区域，不引入全局状态，且共用几何规则避免两处常数漂移。生成脚本用当前冻结源码和精确替换断言，不能假定适用未来代码。

2026-10-23生产接入已按上述方式实现。relevance现在另外记录native_selector_ms，并逐帧要求当前native输出等于全图参考；geometry也直接检查生产区域接口，覆盖非零图像原点。接入验证输出使用runs/cue-region-sync-20261023/dev-videos，保留历史实验不覆盖。prepare_relevance.py可传入冻结基线目录和新的输出目录；CMake的BASE参数须指向对应目录。

`geometry`以两个独立种子生成4800张图，对照RGB/BGRA、pitch、大小圆环、种子相位、区域边缘及大片同色背景，检查区域内所有有效cue保留且没有新增假碎片。`relevance`同帧比较全图克隆、区域克隆和当前native selector；全图克隆必须等于native，区域版记录所有差异。记录的cue阶段耗时在两份克隆内部测量，包含区域行段生成，但不含selector构造关联区域的成本；后者包含在selector总耗时中。

沿用生产0.40门槛、640×512中心截取框、480×384模型、6个E核和名义120fps时间戳。统计排除前30帧，所有帧参与结果比较。固定录像不能反映控制输出改变后画面的反馈，也不等于游戏FPS或准确率实机验收。脚本拒绝覆盖冻结目录/已有CSV，复跑需指定新的产物路径并相应调整生成目录。
