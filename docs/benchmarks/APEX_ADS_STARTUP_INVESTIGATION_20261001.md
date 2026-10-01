# Apex / COD ADS 启动抽动：调查未完成

后续：新日志中的虚假运动测量已完成 RED→GREEN 修正，最新证据与待测试程序见 [物理位移测量修正](APEX_PIXEL_MOTION_MEASUREMENT_FIX_20261001.md)。本文保留较早的失败实验，整体实战验收仍未完成。

状态：**OPEN。未证明实战抖动已修复。** 上一版 recoil 回授修正没有解决用户反馈的主要抖动。本文保留失败实验、可复现断点和下一次取证要求，不能作为发布或实战验收记录。

## 曲线与启动响应

配置选择的曲线在 ADS 与 BodyLock 求解中共用，没有“按 ADS 时自动随机换一条曲线”的分支。启动响应初值是另一个参数：它决定给定误差需要多大的命令；曲线负责将该命令转换成虚拟摇杆值。

从原生 BodyLock 未饱和命令与求解器诊断分量重建的曲线签名：

| 原始 session | 样本 | linear 残差 P50 | COD LUT 残差 P50 |
| --- | ---: | ---: | ---: |
| `20261001T034319Z_26276_1` | 138 | 0.100038 | 0.000000345 |
| `20261001T035108Z_28632_1` | 179 | 0.000000117 | 0.101682 |

见 `runs/apex-jitter-20261001/log-curve-signature.json`。这是命令签名的推断；原 session 没有保存完整有效配置快照。第二次命令与 linear 一致，用户仍报告抖动，因此错误使用 COD LUT 不能独立解释两次症状。COD LUT 在接近零的命令上可以放大原始摇杆幅度，但曲线本身连续，不能据此证明启动抽动的根因。

第二段视频实际有 874 帧、约 60 fps，不能把容器的 `r_frame_rate=240` 当成实际录像或游戏帧率。独立背景 KLT / 单应变换对三个 ADS 段的水平响应拟合分别为 1806.9、1716.9、1678.4，R² 约 0.89、0.88、0.90。见 `background-camera-flow-v2.json`、`video-camera-fit-v3.json`。

这些拟合支持“1000 的启动响应可能偏低”的假设，但尚不能用于准确校准：录像/遥测时间偏移和物理输出延迟未分别识别，人物/玩家平移、透视、武器遮挡与开火扰动仍有混杂因素。原 delivered ledger 也是稀疏记录，不能称为精确逐 tick 回放。没有据此改写用户的曲线、灵敏度或四区初值。

## 可复现的学习断点，及被拒绝的修复

`NativeGamepadController` 只在 `AdsAcquire` 阶段更新 ADS 响应估计器。物理 ADS 仍然按着但已进入 BodyLock 时，ADS 模型不再采集该阶段的响应证据。最近运行的学习快照中 Body 模型已获得约 1700–1800 的生效响应，ADS 模型仍为 1000、零样本。

独立诊断目标 `cod_native_held_ads_response_learning_incident` 保留此断点为 RED：

```powershell
native/build/Release/cod_native_held_ads_response_learning_incident.exe --artifacts <new-artifact-directory>
```

固定 7 种观测频率（100、160、200、250、320、500、1000 Hz）、两轴、正常/禁用学习/开火歧义三个条件，共 42 案例。目标静止，已知 synthetic 相机响应 1800，启动四区均 1000，无 recoil、延迟或检测噪声。实际 selector 身份协议、controller、composer、delivery ledger 被执行；命令是明确标记的 scripted delivery probe，不是玩家实战轨迹。

旧实现的 14 个正常学习案例 ADS 样本始终为零；28 个负对照通过。known-bad runner SHA-256：`b4245954c1f45399e5f7fec7cdc2e2caf3ced9bad8d141a5b21d260b2e0ad725`。证据在 `held-ads-red/held_ads_learning.json`，当前独立诊断仍在 `held-ads-open-final/` 复现。

尝试按物理 ADS 上下文继续训练，并排除跨 epoch 的混合命令区间，42 案例全部通过，正常案例拟合到 1800。然而 16 组固定 holdout 对比全部出现受保护指标退步，包括跟踪、中心穿越、反弹和生命周期时间；见 `held-ads-matrix/comparison.json`。不能用局部 GREEN 豁免这些退步。这一生产修改已撤回，候选源码与 runner 留在 `runs/apex-jitter-20261001/`。

因此，学习断点已经被证明；“简单延长现有 ADS 学习即可修复实战”没有通过验证。人物运动与相机响应的分离、估计在不同阶段的适用范围仍需明确。

## 图像锚点与检测框实验

旧锚点的局部相关搜索不检查当前人物框支持，日志中存在高分锚点落到人物框外。保留的最小修正限定锚点中心必须在当前人物框内。框内位置也不能单独证明它属于人体轮廓，故没有把这个检查当成完整的前景分割。

人物像素不动、检测框边缘变化的合成图像测试确实会改变旧版 source aim。将像素锚点位移直接用于 source aim 的实验在 512 个合规随机短/长案例上消除了这种虚假位移；旧实现全部失败，候选全部通过。最早一版随机生成器越过既有 4.5 长宽比准入边界，不能用于同身份保持结论；修正生成器后重新执行了 RED / GREEN，没有豁免越界案例。

随后用第二段真实录像的 874 帧、完全相同且冻结的 TensorRT 检测框做 selector A/B。人物明确可见的 ADS 画面中，候选有些点漂到身体轮廓之外、但仍在检测框内，这是拒绝该候选的直接原因。另有 4 帧 cue continuation 的 authority 与原版不同；这些原检测框盖住武器，缺少真实身份标注，不能把该差异定性为真正丢失目标或改善。真实人物移动和遮挡没有被合成静止案例充分覆盖，故这个 source aim 修改已全部撤回。

录像路径与 SHA 记录在本地 `video-detections-frozen.json`；回放见 `video-pixel-baseline.json`、`video-pixel-candidate.json` 和 `video-pixel-contact.jpg`。这是压缩录像输入回放，无候选游戏相机反馈，不是闭环实战。

只保留锚点人物框支持检查后，`video-pixel-anchor-boundary.json` 中 874 帧的目标身份、body geometry、source aim、aim/fire authority、auto fire 等已导出字段与原始 selector 相同。该检查是证据边界修正，不是主要抖动的验收证明。

## 其他失败实验与当前状态

- 自动拟合相机响应/延迟的两个候选消除了冻结的静止 synthetic 振荡，但分别在 16/16、10/16 holdout 组产生受保护退步，均撤回。
- 将相机响应置信度直接混入 BodyLock 速度的实验使静止案例从 92/18 次穿越恶化到两方向各 131 次，已撤回。其错误 AimLab runner 调用没有生成有效报告，不能作为比较证据。
- 剩余静止 synthetic 响应/延迟失配案例仍是 RED；它证明敏感性，不证明实战物理延迟已被标定。
- 保留源码的十组原生 product CTest 全通过。最终 `retained-owner-matrix/comparison.json` 的 16 组 holdout 受保护退步为零，仍标记 `EXPLORATORY / MISSING COVERAGE`。
- 用户原 GUI 启动的 `native/build/Release/cod_native_runtime.exe` 未在本轮替换，仍是上一版部分修正 SHA-256 `a17cf1e267578cd8fc324eebb7ee161571c0a5b563d658dc0b873e6e03848971`。未通过验证的候选没有部署。

## 下一次实战取证

当前没有 Apex / COD 或 native runtime 进程，详细遥测配置为关闭，最新完整 session 仍是 `20261001T035108Z_28632_1`。需要用户在原 GUI 勾选“记录控制日志”，保持灵敏度与曲线，记录：准星已对准人物，短按 ADS 不开枪并释放，重复几次；随后保持 ADS 开枪，正常停止让日志收尾，提供新 session 编号。

是否开火必须根据实际 firing button / delivery 字段判断。当前 Apex 开火输入是 RB，不可把物理 RT=0 当成不开枪；已有日志还含实际 fire output，所以旧素材不能自动充当纯 ADS 无开火的对照。

还需要 matched live/native A/B 和用户确认抖动消失。离线 product 通过、局部拟合或 synthetic 抖动消失均不能代替这一步。

用户随后明确要求“清除旧方向 AI、新方向从零渐增，物理手动即时响应”。这一信号策略的日志证据、候选及闭环拒绝结果单独记录在 [AI 信号启动调查](GAMEPAD_AI_SIGNAL_ONSET_INVESTIGATION_20261001.md)。候选未部署，实战抖动仍 OPEN。
