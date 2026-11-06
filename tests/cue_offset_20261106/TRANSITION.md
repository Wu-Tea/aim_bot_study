# 从完整身体到残框的瞄点接续

2026-11-06，用户指出前一版只保住了 cue-only 阶段：person 框逐渐缩小时，实时瞄点仍按残框的 25%/35% 重算，会先上拉，再在 person 消失后回到保存的偏移。本次将保护提前到残框仍存在的阶段。

## 身体参考

比例用于确定可靠完整身体上的瞄点，不应因为检测框只剩上半身，就重新解释同一个比例。以 cue 位于 y=185、完整框 y=200–300、比例 35% 为例：

| 输入 | 每帧按当前框重算 | 保存身体参考后的输出 |
| --- | ---: | ---: |
| 完整身体 y=200–300 | 235 | 235 |
| 上半身 y=200–260 | 221 | 235 |
| 只剩同一 cue | 前版会返回 235 | 连续保持 235 |
| cue 向下移动 8 px | — | 243 |

保存的是 `可靠身体瞄点 - 对应 cue`，输出跟随当前 cue 位移，不冻结屏幕坐标。完整身体恢复后仍可立即更新实际瞄点。实际比例沿用用户 Profile；测试覆盖 25% 和 35%。

## 实现归属

- `target_selector_fusion.cpp` 在输出当前 person 瞄点之前，复用 `supports_cue_reference_refresh()`。同一目标、当前可靠 cue 下，框收缩且瞄点向 cue 靠近，不能更新身体参考，也不能直接输出残框比例点，立即走原有 cue hold。
- 瞄点保持与位置证据更新分开。原先可用于有效配对的当前人物框仍更新对应位置和真实观察时间，但不改写已保存的身体偏移。无效配对不能延长保留时间。没有新增预测时长、等待帧数或冷索敌开关。
- 独立人物恢复、显式手动换目标、友军拒绝沿用已有规则。通过 cue 推导的残框接续保持 aim-only，不用残框授予开火权限；完全没有当前证据时不输出旧坐标。
- 本次生产改动集中在同一选择器的几何归属层，没有新增图像扫描、逐帧分配、锁或检测降频，也没有修改控制器的输出力度与手动权限。

## 复现与检查

基线为 21:21:52 已部署的前一版 dev，冻结源码保存在 `runs/cue-transition-20261106/baseline/native/`。不是 Git HEAD，也没有回滚工作区。产物统一在 `runs/cue-transition-20261106/`。

同一原录像像素、同一检测框收缩序列中，前版垂直偏移在第 5 帧已从 55.8545 变成 53.6203，第 20 帧降至 20.1076，之后才恢复。修复版从第一个缩框帧起保持 55.8545，完整人物恢复首帧回到 observed。

专项覆盖等比例缩框、宽度不变的下半身遮挡、cue 横纵移动、短缺口恢复、完整人物返回，以及开镜放大后再缩框。选择器使用种子 61106、731、52837、101141，完整控制链使用独立种子 9701、10903、21517、30727，每组包含 40/600 帧。控制链断言从第一个残框帧开始检查原身体瞄点，不再等到 person 消失才检查。

旧的 `cue_extent_scale_and_gradual_truncation` 测试假设等比例缩小必然是完整身体真实远离，要求立即重新计算比例点。这与用户本次确认的收缩保护冲突，因为输入没有身体完整度真值。测试现改为：收缩期间保持身体参考并跟随 cue 移动，增长的完整范围可立即更新，恢复完整身体时仍无延迟；逐像素截短从允许 4 px 漂移加强为保持原身体点。固定/同比例 cue 间距、独立四种子与 120/1200 帧轨迹均保留覆盖。

```powershell
cmake -S tests/cue_offset_20261106 -B runs/cue-transition-20261106/build -A x64 -DBASELINE_NATIVE=D:/work/AI/yolo-study-001/runs/cue-transition-20261106/baseline/native
cmake --build runs/cue-transition-20261106/build --config Release --parallel 1
& runs/cue-transition-20261106/build/Release/cases_before.exe --artifacts runs/cue-transition-20261106/cases-before
& runs/cue-transition-20261106/build/Release/cases_after.exe --artifacts runs/cue-transition-20261106/cases-after
& runs/cue-transition-20261106/build/Release/material_before.exe runs/cue-transition-20261106/material.bin
& runs/cue-transition-20261106/build/Release/material_after.exe runs/cue-transition-20261106/material.bin
python tests/november_acquisition_20261102/replay_video.py --output runs/cue-transition-20261106
python tests/november_acquisition_20261102/replay_adjacent.py --output runs/cue-transition-20261106
python tests/cue_offset_20261106/review_results.py --output runs/cue-transition-20261106
```

受控素材使用真实 cue 像素和人工控制的检测框序列，没有重新推理模型；离线通过不能代替实战验收。仅凭框与 cue 仍无法完整判断人物姿态、真实远离和遮挡，这里落实的是用户确认的身体参考接续规则。

## 最终结果与 dev 更新

- 新的“第一个残框帧保持瞄点”断言在 21:21 版失败；修复后的专项 4/4 通过。
- 原生基础 519/519、功能 101/101、桌面 56/56 通过。与新规则冲突的旧缩放断言已按上文更新；其余既有回归未降低要求。
- 7 段当前录像 2,427 帧、5 段旧素材 42,888 帧，总计 45,315 帧，目标有无序列与本轮基线一致。瞄点与 observed/cue_hold 来源按新规则改变；没有语义真值，不把目标存在帧数当作准确率。
- 无编译/测试并发的最终七段回放，扣除各段前 10 帧：选择阶段平均 0.10765 → 0.10262 ms，P95 0.2644 → 0.2624 ms。单次离线计时不能等同于实战 FPS 改善。结果见 `replay-review.json`、`timing-final.json` 和 `material-offset-comparison.png`。
- 2026-11-06 21:48:13 更新 `native/build/Release/cod_native_runtime.exe`，与通过测试的候选逐字节一致。此前的 21:21 版保存在 `runs/cue-transition-20261106/dev-update-20261106-214813/cod_native_runtime.exe`；同目录 `deployment.json` 记录部署信息。依赖 DLL 沿用相同文件，用户 Profile 未改。
- 安装后默认路径的桌面服务测试 22/22 通过，实际 bo3/apex 配置分组均可正常读回。
