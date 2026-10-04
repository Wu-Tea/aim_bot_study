# Apex / Battlefield 6 cue 调研

调研日期：2026-09-30。本文记录公开规则和适配建议；没有修改识别代码或配置，未测量识别率及耗时。

## 可确认的公开规则

| 游戏 / 提示 | 规则和形态 | 识别上的含义 |
| --- | --- | --- |
| Apex 敌人血条 | 官方词条说明：造成伤害且目标处于视线内时，头顶血条显示约 3 秒；失去视线后消失。[官方词条](https://help.ea.com/en/articles/apex-legends/terms-guide/) | 不能要求每个人体都有血条；未命中前也应允许识别人体。 |
| Apex 普通敌人高亮 | 当前官方词条列出 50 米内、有视线时的高亮，并列红、紫、黄三个强度层级。2024 年引入公告描述红色敌人轮廓、蓝色队友轮廓，可关闭高亮和血条。[引入公告](https://www.ea.com/games/apex-legends/apex-legends/news/shockwave-patch-notes) | 轮廓属于人体边缘证据，不能套用头顶小区域；精确 RGB 和颜色模式映射仍需样本验证。 |
| Apex 遮挡 | 2026-05-04 Overclocked 更新加强了血条视线检查，烟雾和植被更可靠地阻断显示。[补丁](https://www.ea.com/games/apex-legends/apex-legends/news/overclocked-patch-notes) | 血条消失不能单独证明人体消失或目标死亡。 |
| Apex 技能扫描 | 2026-08-03 Marked 补丁将 Bloodhound 战术扫描改为 3 秒全身扫描，之后 4 次、间隔 1.5 秒的快照扫描。[补丁](https://www.ea.com/games/apex-legends/apex-legends/news/marked-patch-notes) | 必须区分当前人体和快照位置；不能把所有扫描提示当作当前人体几何。 |
| Apex 手动敌情 ping | 官方操作表列出双击 ping 的“enemy here”。[操作说明](https://help.ea.com/en/articles/apex-legends/pc-and-controller-settings/) | 这是玩家报告；不能只凭该提示就确认可见人体。具体图案和位置绑定待验证。 |
| BF6 侦察兵标记 | 官方职业指南说明 Recon 在 ADS 观察敌人时自动标记；官方示例为头顶橙色菱形，并向队伍共享位置。[职业指南及实机图](https://help.ea.com/en/articles/battlefield/battlefield-6/class-guide/) | 可以研究头顶菱形与 person 的关联，不能把所有橙色图标都判为敌方步兵。 |
| BF6 HUD 设置 | 官方支持敌人、队友、小队、目标、中立和 ping 图标的尺寸及透明度调整，以及色盲预设和自定义 RGB。[无障碍说明及设置截图](https://www.ea.com/able/resources/battlefield-6) | 图标缺失不是敌我结论；颜色、尺寸、ADS 显示变化须纳入适配。该说明注明基于美版英语 PS5 / Xbox，PC 具体表现仍需确认。 |

## 和当前实现的差距

`native/vision_native/src/target_selector.cpp` 当前共用固定红/黄敌方、绿色友方 HSV 范围。直立目标主要搜索头顶；宽矮目标使用不同 ROI。延续函数名 `scan_yellow_window` 虽然包含 yellow，实际同样调用 `is_enemy_hsv`，支持既有红/黄范围。

现有 Apex 数据集 `artifacts/game-specialists-20260914/apex/dataset.yaml` 只有 person 类，专属人体模型不等于专属 cue 识别。

## 建议的适配边界（尚未实现）

- 在现有 `games.apex` / `games.battlefield6` 分块中选择 cue profile，使用同一可执行程序。
- Apex 分开处理人体边缘高亮、头顶血条和位置提示。血条/轮廓增强当前人体证据，位置提示单独记录语义。
- BF6 先处理人体头顶的敌方菱形及友方图标；颜色阈值配合形状、局部位置与人体关联，排除目标点、车辆和普通 ping。
- 初次识别与短暂延续必须使用一致的游戏规则。沿用当前同目标代际的关联约束；不得用 cue-only 输出更新人体偏移，或由 cue-only 产生自动开火权限。
- profile 热重载时，应清理旧规则产生的 cue 偏移与关联状态。学习数据清理遵循现有热重载语义。
- 复用已有画面和局部颜色分析，先不增加模型推理；扩大 ROI 和形状检查的实际耗时仍需测试。

## 尚不能作为已验证默认值的内容

准确 RGB/HSV 范围、色盲模式映射、不同 HUD 缩放下的图标尺寸、BF6 菱形空心/实心的完整状态语义和持续时间，以及线上模式之间的差异，公开材料不足以直接定标。

验收应覆盖无 cue 人体、敌友标识重叠、多人关联、同色场景/特效、ADS/腰射切换、遮挡、扫描快照、目标死亡及配置切换。用匹配的原版与适配版样本比较 cue 误判、person 漏检、身份错配和处理耗时；网页研究不是识别成功率证明。

## 2026-10-01：BF6 专属模型的数据下载候选

用户确认目前没有本地 BF6 数据，先由代理找公开来源，由用户下载后再训练。
以下是网页元数据核验，尚未下载、逐图审查、训练或测量本项目识别率。

| 下载候选 | 网页确认内容 | 后续用途与限制 |
| --- | --- | --- |
| [Battlefield 6 players / DZSS v1](https://universe.roboflow.com/dzss/battlefield-6-players-zvu5a-sgnci/dataset/1) | 665 张；单类 player；466 train / 133 valid / 66 test；自动方向校正、拉伸至 640×640；无预生成增强。项目标示 CC BY 4.0。 | 首选人体标注候选。下载后检查是否完整标注所有可见人物；既有拉伸限制原始比例和远处小目标细节。 |
| [Battlefield 6 / bf6 v1](https://universe.roboflow.com/bf6-pszfx/battlefield-6-5aepi-avm2i/dataset/1) | 676 张；单类 Enemies；478 train / 126 valid / 72 test；无预处理、无预生成增强；CC BY 4.0。 | 补充候选，保留原图。敌人框不能直接证明所有人体均完整标注；须核对队友、尸体及目标口径，必要时补标。 |

两者选择 **YOLO26 → Download ZIP to Computer**；如果界面没有 YOLO26，
可以选择 YOLOv8 的 TXT 框与 YAML 格式。保留整个压缩包及 README / 许可说明，
建议放在 `D:/datasets/roboflow_candidates/battlefield6/`，解压到不同子目录。

检索发现多份相同项目短名或同为 665 / 676 张的分叉。这些不能默认视为独立数据源。
拿到文件后先核对类定义、漏标和重复原图，以原始截图和近重复分组检查跨 split
泄漏；按验证集选模型，保留独立测试集。不得将这些分叉的测试集直接当作外部泛化证明。

[Update / Testing](https://universe.roboflow.com/testing-e4ptg/update-s0vuo) 另有
2,289 张，说明写 BF6，但类名仅为 0 / 1，含义不明，暂不作为首选。
[Players / Battlefield 6](https://universe.roboflow.com/battlefield-6/players-bxwac)
仅 297 张且含异常类名，暂不采用。Hugging Face 的 GameplayQA 等包含 BF6 视频或问答，
不等于可直接训练的人体检测框数据。

下载后先在相同输入尺寸和阈值下测现有 `models/best.pt`；随后做有限本地微调，
分别比较远处小人、遮挡、ADS / 腰射、多人、无目标背景等场景的漏检和误检。
候选保持现有单 person 输出及 480×384 TensorRT 接口，另存 BF6 产物。验证集
成绩不能代替独立测试、原生推理耗时及用户实战确认；人体模型也不提供新的敌我 cue 权限。

## 2026-10-01：已完成 BF6 采样加权实验

用户提供 Testing `Battlefield 6 players` v3 Roboflow Instant 2 [Eval]（665 张）及
bf6 `Battlefield 6` v1（676 张）。v3 实际为多边形轮廓行，不是五字段检测框；
旧测试工具错误读取前几个顶点，首轮输出已明确标记为无效。修复在
`python/tools/benchmark_vision_dataset.py`，轮廓以外接框参与人体检测评估；
混合框/轮廓及非法坐标回归测试先失败、修复后通过。

两份数据共 1,341 张、676 个原始身份，按原图身份、字节 SHA 及传递 pHash 距离 ≤6
合并为 672 组。所有分组先冻结，已有 test 优先于 valid，valid 优先于 train，得到
325 训练 / 214 验证 / 133 测试组。同组优先 player 轮廓；对同一截图的不同标注口径
保留限制说明。公开素材缺少录像身份，分组不证明不同录像/地图的独立泛化。

从当前 `models/best.pt` 分别微调 50% BF6 和 80% BF6 两组。其余素材为本地 COD
旧素材池中的人体框，排除 head 类，325 张训练、另留 167 张诊断保留集。两组均
16 轮、每轮 1,625 张、batch 8、相同 seed、AdamW / lr0=0.0003，均覆盖同一素材池；
BF6 整图及中心裁切视图只在所属训练组生成。80% 组每轮 1,300 BF6 / 325 旧素材；
50% 组为 812 / 813。训练产物独立保存，生产模型和配置未替换。

原生验证集通过精度/召回门槛后，按 F1 在读取候选测试集前锁定 80% 组。
下表是 **133 组保留测试、171 个公开标注人体、IoU 0.50、置信度 0.40** 的原生结果。

| 模型 | 整图精度 | 整图召回 | F1 | 中心区召回 | 整图小目标召回（输入高度 <24） |
| --- | ---: | ---: | ---: | ---: | ---: |
| 通用 | 76.6% | 34.5% | 47.6% | 37.4% | 3.9%（2/51） |
| BF6 50% | 77.9% | 47.4% | 58.9% | 52.6% | 19.6%（10/51） |
| BF6 80% | 77.5% | 50.3% | 61.0% | 55.0% | 19.6%（10/51） |

80% 组整图新增命中 31 个、丢失原有命中 4 个，FP 从 18 增至 25。按完整图片配对
bootstrap 的召回增量 95% 区间为 +9.3 至 +22.3 个百分点；精度增量区间跨零。
在更接近现有索敌入口的 **0.65 门槛** 下，整图精度从 92.9% 降为 88.3%，
召回从 22.8% 升为 39.8%；50% 组精度同样下降。因此两组均未通过完整保留测试门槛，
只保留为探索候选，不作为默认替换。

旧 COD 诊断集在 0.40 下，通用精度/召回 80.0% / 58.1%，50% 组 80.1% / 65.4%，
80% 组 78.4% / 60.9%。它来自已用于旧模型的相关来源，不是外部泛化集；80% 加权
没有全面优于 50% 对照，不应推广为其他游戏的通用替换。

40 张同图、每模型 8 次、随机交错顺序且训练全部结束后的孤立原生测时：
infer P50 通用 0.714 ms、50% 组 0.523 ms、80% 组 0.513 ms；P95 为
0.772 / 0.678 / 0.563 ms。调用总耗时 P50 为 0.858 / 0.672 / 0.660 ms。
这是本机导出产物的离线 `infer_rgb`，没有证明全链路 FPS 或手感提升。

限制：只有 4 张名义无目标测试图；v3 的可视检查还发现疑似同人碎片/重复框，
程序筛出 8 张图、9 个被较大框包含的小框作为人工复核候选，v1 此类候选为零。
这些不是自动判定的标注错误数量，正式清理需要逐图核对。所有成绩均依赖原有公开标签，
并非独立人工真值。未根据候选结果修改标签、重训或更换测试集。

证据目录：`artifacts/bf6-specialist-20261001-v2/`，包含 frozen-contract、manifest、
locked-selection、原生逐图检测、comparison.json / comparison.png、同图得失示例、
原始许可说明和两份 `.pt` / `.onnx` / 480×384 `.engine`。原始目录的基线/80% 复测
在 `runs/bf6-20261001/baseline-original-corrected.json` 和 `weighted-original.json`。

复现入口：`python/tools/training/bf6_specialist.py` 的 prepare / train / export / evaluate，
以及 `compare_bf6_specialists.py` 的 select / timing / compare；使用新 `--out` 目录，
既有冻结实验拒绝覆盖。相关 Python 回归测试共 21 个通过。实际 BF6 敌友 cue、身份关联、
控制器及实战表现仍未验收；后续应先补人工审核标注和独立背景/录像数据，而不是用现有
保留测试集继续调参数。
