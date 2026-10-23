# 当前接续

更新：2026-10-23。用户要求“改动提交并push，然后同步到clean”。dev 实现 `3e2ab8d`、clean 同步 `9aad8f8` 均已推送；本交接收尾另行提交，最终 HEAD 以 Git 为准。

## 当前状态与下一步

- 本次交付包括此前五项跟踪修复，以及 cue 独立运动参考、弱框身体参考两项根因修复。clean 从临时分支验证后快进合回 `codex/runtime-clean`，13 个生产文件同步，实验/测试/轨迹留在 dev。
- clean Release 和 React 资源已构建；入口 `D:/work/AI/yolo-study-001-clean/启动助手.vbs`，重新启动原生助手加载新版本。未停止用户会话，验证仅使用独立配置并关闭输出/震动。
- 下一步等待实机反馈。用户授权发布，不等于本次下拉现象已实机验收；保持已有手动权限、原始透传、胸部参考、ADS/BodyLock、压枪和个人参数。

## 本次修复与证据

- selector 发布当前已配对 cue 的稳定身份和坐标，用于运动估计和相应响应学习；同一 cue 不因 person/hold 切换重建，cue 更换重建测量对，缺失不发旧坐标。person 仍决定胸部；鼠标适配不消费新增通道。
- 弱框在同一目标、当前纹理匹配可靠时，用当前纹理位移平移最近强人体身体/胸部参考；强 person 返回立即校正，无可靠纹理保持原弱框路径。弱框不获得新的开火权限。
- 两项回归均在旧实现下失败。视频 frame182→183 胸部跳变 +29.526→+1px；第二段同一4.718s事件开火Y幅值 .404936→.065486。这不是全片段峰值或实机准确率。
- 前五项修复：cue 保留已验证身体范围、命中十字排除及强人体反证、运动观察窗口统一限幅、ADS cue 到达需当前同源证据、交叉人体运动预测/歧义处理。详见下方报告。
- person OR cue 及可靠同一 cue 超过180ms延续身体参考的已确认方案继续保留；未配对cue不猜胸部，缺cue/歧义停旧cue输出，下游32/40ms边界不变。

## 验证和限制

- dev 最终 CTest 51/51。clean 库 Base 473 项及开火14项通过；首次缺SDL测试依赖的7项，经补齐runner旁DLL后所属套件67/67，其他406项沿用首次通过结果。
- 新两个独立种子23102301/98102713、128个2/20秒场景、1,408,000 tick通过，覆盖横纵运动、不同视觉节奏、响应曲线、轻手动及几何噪声。
- 两片段665帧，clean/dev records完全一致（有目标358帧、cue_hold87帧）；固定像素与近似闭环CSV分别一致。180共有native文件相同，4个CMake文件保留产品/测试差异。
- clean桌面81项、隔离WebView2、真实隐藏GUI模型初始化/IPC/热更新Applied/正常停止通过。产物 `runs/downpull-20261023/`，没有修改个人配置和模型。
- 尚无同步真实手柄输出证据证明突然下拉唯一成因或全部消除。固定像素第二段另一时刻峰值 .404936→.471552；近似闭环第一段平均Y误差16.646→20.181px（旧clean对新dev，包含前五项差异）。模拟缺少真实重新渲染及变焦/视差，不能据此承诺实机效果。
- 既有ADS开火走位/固定偏置压力反例、cue错关联和姿态/尺度变化风险仍保留；不要增权、压手动或延迟交接掩盖。

## 接续入口

- [本次同步](../docs/project/DOWNPULL_CLEAN_SYNC_20261023.md)、[两项根因修复](../docs/project/DOWNPULL_DEV_FIX_20261023.md)、[录像诊断假设](../docs/project/DOWNPULL_VIDEO_REPLAY_20261023.md)、[前五项修复](../docs/project/VISION_CONTROLLER_FIXES_20261020.md)、[轨迹夹具](../docs/project/TRACKING_TRAJECTORIES_20261020.md)。
- 用户指定录像 `E:/videoCut/2026-10-22 10-40-12-cut-merged-1792637433471.mp4`，当时运行clean、手柄模式。后续按用户要求直接跑Vision，不再无目的追查日志。旧基线保留 `runs/downpull-20261023/before-fix/` 和原clean记录；本次同步验证在 `sync-video/`，勿覆盖旧基线。
- 上版交接完整归档 [dev](archive/handoff-20261023-before-motion-sync.md)、[clean](archive/clean-handoff-20261023-before-motion-sync.md)，旧ADS与OR决策仍有效，历史见session-log.md。
- SyncSet `sync-20261023-001`，沿用用户上下文维护授权，reviewer=accept_draft。两份handoff、dev会话日志及两份旧handoff归档；无新决策、subagent brief、平台记忆或秘密。session-log偏长，保留历史，后续可独立归档压缩。
