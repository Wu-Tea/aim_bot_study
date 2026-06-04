# Research Notes

## Topic

正式报告性质的技术复盘，主题是 `yolo-study-001` 的 native vision 和 gamepad 协同设计。

## Audience Assumptions

- 读者可能不了解这个仓库。
- 读者可能懂一般软件工程，但不了解 FPS 视觉辅助、手柄控制、目标权限这些上下文。
- 报告需要同时服务三类读者：
  - 面试官：关心工程判断、证据链和风险边界。
  - 无经验读者：需要先看懂应用是什么。
  - 有相关经验的工程师：需要看到具体场景、策略选择和边界。

## Report Thesis

这个项目的重点不是“YOLO 是否能识别人”，而是“视觉证据能被信任到什么控制等级”。因此报告主线应从 `has_target` 这种粗粒度表达，推进到目标来源、权限分层、gamepad 输入融合和 auto-fire fail-closed。

推荐标题：

> 从看见目标到可信控制：native vision 和 gamepad 的系统化设计

## Current Project Facts

- 当前系统是 Windows/COD/FPS 场景下的 native YOLO/TensorRT 目标识别 + Python gamepad controller 实时闭环。
- native 侧负责视觉热路径：
  - 中心 ROI 截图
  - DXGI/D3D11 画面获取
  - CUDA 预处理
  - TensorRT 推理
  - native target selector
  - native aim enhancement
  - native auto-fire recommendation
- Python 侧保留：
  - 启动和配置
  - runner 字段映射
  - controller 状态
  - gamepad host
  - AI aim、auto-fire、recoil 插件
  - 测试和调参
- weak association / authority gating 已经作为版本提交，不再是未提交实验。
- 最新相关提交包括：
  - `c4c9982 Improve native target authority and gamepad hold`
  - `9473ca2 Guard stale target escape with yellow cue`
- 报告包本身仍是文档工作，不代表运行代码还有同样的未提交状态。

## Latest Design Point: Wide-Low Box And Yellow-Cue Double Check

最新系统补了一个细分场景：旧 active 目标突然变成宽低框时，不能直接判断它是活目标，也不能直接判断它是尸体残留。

当前逻辑是：

- 如果旧 active 从正常站立框变成宽低框，selector 会把它看成需要 double check 的可疑状态。
- 如果这个宽低框仍然有黄色 cue 或敌方颜色证据，系统优先按滑铲、趴下或低姿态活目标处理，不切走。
- 如果这个宽低框没有敌方证据，而附近有强站立候选，系统可以在确认后从旧 active 逃逸，切到新的站立目标。
- 对应代码集中在 `native/vision_native/src/target_selector.cpp` 的 `should_escape_stale_active_match`。
- 对应测试包括：
  - `test_stale_wide_low_active_switches_to_upright_challenger_after_kill`
  - `test_stale_wide_low_active_with_yellow_marker_does_not_switch_to_challenger`

这个点适合写进主文的“多目标问题”或 “vision 的策略选择”，因为它很好地说明了：同样是宽低形状，系统还需要结合黄色 cue 和站立候选来决定是继续锁、还是切走。

## Confirmed Vision Strategies

- 中心 ROI：减少每帧处理面积，把注意力放在准星附近。
- native 热路径：把截图、预处理、推理、选目标等固定高频工作放到底层。
- detector-led selection：以人形检测框为目标权威基础，不先上完整身份跟踪。
- 多因素 selector：综合准星距离、检测分数、框大小、颜色、连续性。
- 出生/切换确认：避免一帧误识别造成新目标或切目标。
- low-score weak association：低分框只允许 active-only 续住，不能 birth/switch/fire。
- yellow cue hold：黄色 cue 只做短暂续住，不能创建目标或开火。
- wide-low yellow double check：宽低旧目标是否继续锁，要看是否仍有敌方证据。
- target authority：目标结果带 `target_tier`、`aim_authority`、`fire_authority`、`association_stage`、`target_confidence`。

## Confirmed Gamepad Strategies

- host/plugin 分工：真实手柄输入先进入 `GamepadFrame`，插件再叠加 AI aim、auto-fire、recoil。
- ADS snap：只处理开镜初期快速拉近，只允许强目标触发。
- body lock：处理贴近身体后的稳定，按上半身点和目标来源调整力度。
- source-aware lock：weak/cue 可以轻量 body lock，但不能 ADS snap。
- target projection：vision 帧之间短时间补手感，但预测目标不能拿权。
- velocity refresh boundary：只有强到强观测才刷新速度，weak/cue 只衰减速度。
- manual arbitration：玩家同向输入保留，反向输入谨慎抑制，玩家手动开火时 auto-fire 让路。
- auto-fire gates：强 observed 只是必要条件，还要过 native fire zone、runner ADS delay、freshness、AIAim settle、manual takeover。
- recoil 合并：压枪要和玩家输入、辅助瞄准、auto-fire 在同一输出帧合并。

## External Research Anchors

这些资料用来支撑报告的“技术背景”和“为什么不盲目上重算法”，不直接作为本项目性能结论：

- NVIDIA TensorRT Best Practices：支撑推理性能、benchmark、profiling、硬件/软件环境的讨论。
- Microsoft Desktop Duplication API：支撑 DXGI/D3D11 桌面帧获取和 GPU 处理路径。
- A Case Study of First Person Aiming at Low Latency for Esports：支撑 FPS 瞄准对本地输入到输出延迟敏感。
- SORT / Deep SORT：支撑多目标跟踪和 ReID 是可选方案，但会带来身份管理、特征和调参成本。
- OpenCV Optical Flow：支撑光流属于另一类运动估计方案，不应默认进入热路径。
- Aim-assist HCI 研究：支撑 aim assist 在真实 FPS 场景中会受游戏元素、可感知性和玩家输入影响，不能只看静态目标场景。

## Important Tradeoffs

- 不优先 full MOT：当前输出是 controller-facing 单目标，不是全场身份管理。
- 不允许 cue-only birth/fire：黄色 cue 有用，但不具备人体几何权威。
- 不允许 weak birth/switch/fire：低分框只服务短续住。
- 不把 prediction 当事实：projection 可以补手感，但不能开火。
- 不先全 controller C++：手感策略仍然需要快速测试和调参，当前更重要的是 native hotpath 与权限契约。

## Open Risks

- 当前 live 反馈显示版本很强，可能略粘；后续应优先调 weak/cue 力度、body lock 范围和目标点。
- 最新报告仍没有新增 native 迁移前后延迟对比表。
- 需要更多 replay/log 来验证 weak/cue 次数、stale wide-low escape 次数、fire request 与最终 fire output 的关系。
- 后续字段改动必须保持 native bridge、runner、controller 测试同步。
