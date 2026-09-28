# 可见区域选靶与尸体可瞄规则

2026-09-28 用户明确：① target select 根据可见范围决定合法操作区域；② 多目标按可见范围与准星距离选择；③ 尸体允许瞄准，不能剥夺用户拉走准星的能力，已有通用手动权限机制继续使用。

这取代本日此前方案中“扁宽形态加标记丢失就拒绝目标”的假设。先前的 marker-loss 事故证据仍有效，但尸体必须被拒绝的旧测试不再是产品要求。

## 当前实现

- `VisionTargetSelector::apply_detection_policy` 将当前检测框规范到实际采集画面内。瞄点、合法瞄准区域 R、身体尺寸和既有按高度缩放的获取半径都使用该区域；下游收到的 detection list 也采用相同坐标，防止再次拿画面外的身体部分扩大范围。非法浮点值在检测输入边界失去几何/置信度有效性。
- 多目标获取优先比较准星到 R 最近点的距离；差距在原有容差内，再比较配置瞄点距离，最后用现有证据评分决定。准星已经指向的小目标不会单纯因另一人体框更大而被抢走。
- 删除 `TargetEvidence::reject` 及姿态触发的 `should_escape_stale_active_match`。扁宽身体、黄标丢失可以降低优先级和合成开火权限，不能直接否决合法的人体观测，也不能强行释放身份再自动获取邻居。
- 正在跟随的身份继续遵守已有关联与显式 `HandoverTarget` 协议。距离排序不构成每帧自动换人的授权。
- 没有修改 Controller 的手动权限曲线、原始输入透传、区域退出、ADS 时限或 BodyLock 算法。没有增加尸体专用的等待、开关或释放状态机。

“可见范围”的实现边界是**当前检测框与采集区的交集**。它是已有模型输出支持的几何近似，不是逐像素人体分割；不能可靠扣掉检测框内部的墙体、枪械等遮挡。未增设任意的“尸体半径”，获取半径继续按规范后的当前人体高度计算。

## 冻结验证

核心 fixture 为 `native/vision_native/src/visible_selection_contract_tests.cpp`，在生产修改前冻结。基线包含本日已完成的 marker-loss 修复及工作区原有重构，不能称为实机 exe 的逐字节重建。

| 指标 | 修改前 | 修改后 |
| --- | ---: | ---: |
| 区域距离选靶失败，256 例 | 256 | 0 |
| 姿态序列无目标帧，33,792 帧 | 512 | 0 |
| 姿态序列选错身份帧 | 33,280 | 0 |
| 显式手动转火失败，256 例 | 0 | 0 |
| 四个采集边缘的合法几何失败 | 4 | 0 |

错误身份帧是逐帧统计，不代表发生了 33,280 次独立转火。固定种子 `20260929`、`593867`，覆盖双轴镜像、检测顺序交换、120/200 Hz、24/240 帧序列；准星正中目标、友军等反例均有效。

原生基础测试 **333/333**，功能测试 **90/90**。新增端到端测试经过真实 selector → vision adapter → NativeGamepadController，验证扁宽人物产生 ADS 输出、没有合成开火权限、持续手动拉离可退出、松开 LT 同拍恢复原始微小输入。手动拉离测试沿用已有 160 ms 退出验收界限，持续发布新帧，避免靠观测过期或失去目标意外通过；它不声称 ADS 内任意一帧的物理杆量都应全量透传。

两个测试语义修正已明确：旧尸体拒绝用例改为验证“可瞄、无合成开火、身份稳定、可以手动转火”；上一轮 marker-loss fixture 的尸体反例仅 30 px 高，低于既有跟踪几何门限，现改为位于当前目标点的 40 px 高有效身体，避免空洞通过。原事故的连续直立人物 oracle 不变。旧报告留作历史快照，不覆盖成新的结果。

复现：`cod_native_base_tests.exe --suite BaseVisionSelection --case visible_region_and_posture_authority --artifacts <目录>`；端到端：`--suite BaseEndToEnd --case flat_person_ads_manual_and_release`。基线源码、二进制 SHA、逐例报告、冻结 contract 和验证输出保存在 `runs/visible_selection_20260928/`。

## 编译交付与提交范围

2026-09-28 16:55（UTC+08）已完成 Release 编译，更新日常启动器使用的 `native/vision_native/build/Release/cod_native_runtime.exe` 和 `vision_native_cpp.cp311-win_amd64.pyd`。配置加载检查退出码为 0，Python 模块导入成功；未启动游戏控制输出。

- Runtime SHA-256：`28651ce152fe3f56c2d02b6f53ac7629f8ef184d3c4fcdf2bf7cf5511b427c6a`。
- Python 模块 SHA-256：`ba4def230e346f1903bd1e20abbad2f13ca3fc815da08918eda50185b25aadc4`。
- 编译记录、配置身份和旧程序备份：`runs/visible_selection_build_20260928_165413/`。
- 编译命令：`cmake --build native/vision_native/build --config Release --target cod_native_runtime vision_native_cpp --parallel 4`。

交付二进制包含当时工作区已有的生命周期重构，不能把它的 SHA 当成本次单独提交的构建身份。本次提交包含选择器修复、选择器直接依赖的 `SelectionStateMachine` 抽取、相关测试及本报告；其余 Controller/Python 生命周期重构留在工作区，配置、录像、原始日志和编译二进制不纳入 Git。

提交前将 `2fd12d2` 的原生源码与暂存区变更导出到独立目录并重新构建，原生基础测试 **325/325**、功能测试 **90/90** 全部通过，失败与无效用例均为 0。这里不包含未提交生命周期重构新增的 8 个基础测试，因此与完整工作区的 333 例区分记录。新增 marker-loss、可见区域/姿态和 ADS 手动退出用例均包含在 325 例中。导出源码、构建日志及测试产物保存在 `runs/visible_selection_commit_check/`；该验证没有覆盖日常程序。

状态：源码、离线验证与编译交付完成，用户要求按当前阶段收尾提交。尚未完成实机 A/B，不宣称四段素材中的全部漏检、停顿或转火都已解决。第一段后续模型漏检，以及其他片段的 cue 续跟零输出，仍是独立事项。
