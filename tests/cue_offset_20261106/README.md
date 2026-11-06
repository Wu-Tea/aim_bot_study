# 渐进遮挡后的 cue 相对瞄点

本文记录 21:21:52 的 cue-only 修复。用户随后要求残框仍存在时也立即保持身体瞄点；该修正及新的基线、验证记录见 [从完整身体到残框的瞄点接续](TRANSITION.md)。下方旧版边界不代表最新实现。

用户在 2026-11-06 实战反馈：人物和 cue 同时出现，开镜过程中人物逐渐被遮挡，最后只有 cue 时，瞄点会向标识上拉。用户要求保存 targetPoint 到 cue 的相对位置，cue 自身不能成为瞄点。没有该次实战日志；以下是明确控制输入的复现，不声称还原了那一局。

## 复现证据

修复前基线是刚更新到 dev 默认位置的源码，复制在 `runs/cue-offset-20261106/baseline/native/`。没有 checkout、回滚源码或改换控制器。

`probe.cpp` 复现两条路径：

1. 保留当前 cue，把高置信度人物框逐步缩小。旧代码每次把新框的 targetPoint 和 cue 配成新的参考，垂直偏移从 40 px 缩到 16 px；人物完全消失后，cue-only 延续错误偏移。
2. 一个高置信度、底部裁边的人物框 `[240,96,400,512]`，25% 瞄点恰为 `(320,200)`，与 cue 中心重合。旧的近身裁边标定例外允许它建立零偏移，随后 cue-only 直接瞄向标识。

`make_material.py` 使用原录像 185.0565245 秒（03:05.057）的真实像素与初始检测框，控制后续检测框缩小、消失、恢复。没有修改 cue 像素或重跑模型，也没有把真实画面当作合成遮挡后的模型真值。

| cue-only 相对位移 | 修改前 | 修改后 |
| --- | ---: | ---: |
| 垂直 | 17.8734 px | 55.8545 px |
| 水平 | 2.2800 px | 9.2500 px |

修复后与初始可靠配对一致。帧 30–44 为完全无人物检测的 cue-only 输入。输入和结果保存在 `material.bin`、`material-schedule.json`、`material-before.csv`、`material-after.csv`，图为 `material-offset-comparison.png`。

## 修复归属

- `CueBodyReference` 显式保存 `aim_offset` 和对应测量时的身体尺寸。可靠配对记录相对位移；缩小的残框可以继续提供当前人物证据，但不能缩短已保存的 cue 偏移。开镜先放大人物后，比较的是最近一次有效测量的尺寸和偏移，而不是最初的小框。完整尺寸恢复时可向任意方向更新偏移，向下恢复更大间距的身体证据也可更新。
- 最新人物/cue 对应位置继续更新，用于跟踪和短暂丢失后的重定位。cue-only 的点为 `current_cue + aim_offset`，瞄准区域同步移动。没有把整份参考位置冻结。
- `cue_reference_translation()` 区分身体点和 cue 锚点的位移：cue-only 恢复时比较 cue 坐标，不能把保留的身体瞄点减去残框瞄点，再误当成 cue 位移。
- `cue_aim_geometry.h` 集中管理“瞄点在标识下方”和“当前身体证据是否足以更新偏移”。拒绝位于对应 cue 范围内的异常人物瞄点；没有可用身体几何时，只能走已有的 cue-only 临时定位权限。
- 首次 cue 下移配置改为 10–512 px，默认仍为 40。当前 cue 检测最大边长为 18 px；10 px 下限避免设置成零或落入标识，同时选择阶段仍验证实际 cue 范围。

人物准入权重、正常人物的 box 瞄点、手动权限、输出力度和检测频率保持既有规则。完整人物恢复仍直接使用真实 box 瞄点。没有增加图像扫描、图像缓冲或同步，只增加相对位移记录和常数次几何判断。

## 验证方法

专项覆盖渐进缩框、完全无人物、移动 cue、横纵偏移、完整人物恢复、异常裁边零偏移、配置零值/标识范围以及 cue 短缺口恢复。选择器使用种子 61106、731、52837、101141，完整控制链使用独立种子 9701、10903、21517、30727；每组包含 40/600 帧场景。

专项旧版 4/4 失败、修改后 4/4 通过；完整原生基础测试 519/519、功能测试 101/101、桌面测试 56/56 通过。完整身体相对 cue 上移、开镜放大后再缩框也在专项里验证。旧的两个测试夹具曾将瞄点放入 cue 图形，或在移动身体框后把 cue 留在瞄点下方；已修正夹具位置，保留完整人物更新与屏幕裁边的原有断言，异常 cue 瞄点由新增反例覆盖。

```powershell
cmake -S tests/cue_offset_20261106 -B runs/cue-offset-20261106/build -A x64
cmake --build runs/cue-offset-20261106/build --config Release --parallel 1
& runs/cue-offset-20261106/build/Release/cases_before.exe --artifacts runs/cue-offset-20261106/cases-before
& runs/cue-offset-20261106/build/Release/cases_after.exe --artifacts runs/cue-offset-20261106/cases-after
python tests/cue_offset_20261106/make_material.py
& runs/cue-offset-20261106/build/Release/material_before.exe runs/cue-offset-20261106/material.bin
& runs/cue-offset-20261106/build/Release/material_after.exe runs/cue-offset-20261106/material.bin
python tests/november_acquisition_20261102/replay_video.py --output runs/cue-offset-20261106
python tests/november_acquisition_20261102/replay_adjacent.py --output runs/cue-offset-20261106
python tests/cue_offset_20261106/review_results.py
```

七段当前录像 2,427 帧，目标有无序列没有变化。五段旧素材 42,888 帧中，四段有无序列没有变化；v07 新增 6 个有目标帧、5 个无目标帧。其中索引 11047–11049 明确拒绝了与 cue 重叠的人物瞄点；13560–13561 在 cue 短缺口时未接纳弱关联/后续候选，第 13562 帧恢复同一身份。具体索引和原因见 `replay-review.json`，没有语义真值，不能据此宣称准确率提升。

最终无编译/测试并发的七段回放，扣除每段前 10 帧后：选择阶段平均 0.11548 → 0.11417 ms，P95 0.2914 → 0.3082 ms；见 `timing-final.json`。这是单次离线阶段计时，不能证明实战 FPS 改善。没有新增图像扫描或同步；每个 Detection 新增一个 cue 尺寸浮点数，当前 cue/body 参考新增四个浮点数保存偏移及其测量尺寸。

2026-11-06 21:21:52 已更新 `native/build/Release/cod_native_runtime.exe`，与通过测试的候选逐字节一致。原程序保存在 `runs/cue-offset-20261106/dev-update-20261106-212152/cod_native_runtime.exe`，同目录 `deployment.json` 记录部署信息。依赖 DLL 与候选一致，沿用现有文件；用户 Profile 未修改。安装后针对默认路径的桌面服务测试 22/22 通过，实际配置中的 bo3/apex 分组均成功读回 40 px 默认偏移。

## 边界

仅凭 box 和 cue 无法完全区分真实人物变小与模型残框。正常直接人物观测仍采用当前 box 瞄点；本次保证的是残框不会重写 cue-only 的可靠相对偏移，并拒绝对应标识本身作为瞄点。不能据此宣称残框识别、所有开镜阶段的几何误差或 BO7 短血条碎片误识别都已解决。离线回放与仿真不能替代实战验收。
