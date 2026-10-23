# 当前接续

更新：2026-10-23。最新任务是降低cue检测开销，不降低检测频率和跟踪效果，使用三段视频验证，先放dev。优化已完成并构建，未提交/push，未同步clean；等待用户实机试用。

## dev 当前改动

- 生产代码仅修改cue_identity.cpp及对应头文件：整数必要颜色条件预筛选，保留原浮点分类；按最小合法高度4像素每4行寻找种子，命中后原分辨率完整展开；恢复原光栅候选顺序。检测频率、完整搜索范围、阈值和跟踪/controller保持。
- 原因：独立cue全框CPU扫描约4ms，串行位于person推理之后；GPU回读仅约0.068ms。同步前已有该成本，不能将降帧起点认定为最后的运动参考修复。
- 三段视频39,435帧，116,885个cue候选及最终selector记录全部一致；cue平均4.08–4.15ms→0.87–0.97ms，降低76.5%–78.8%；selector平均4.19–4.26ms→0.96–1.06ms。
- 全部16,777,216种RGB颜色×2格式、两个独立种子4800场景/9095cue全部等价；新增5像素小标记种子相位专项回归。最终CTest51/51，dev Release已构建。
- 启动dev根目录启动助手.vbs，重新启动原生助手加载。没有停止用户会话或启动设备输出。clean仍为9aad8f8，工作区干净；个人配置、模型保持。
- 这是阶段耗时及识别等价验证，不是实际游戏160–170fps恢复保证，也未修复已有误识别/漏检。大块同色区域仍需要完整遍历。

## 证据入口

- [优化报告](../docs/project/CUE_DETECTION_OPTIMIZATION_20261023.md)、[原始耗时诊断](../docs/project/VISION_FPS_DIAGNOSIS_20261023.md)、[复跑工具](../tests/cue_opt_20261023/README.md)。产物runs/cue-opt-20261023/，包含逐帧CSV、summary.json、全色域/随机等价、最终构建和CTest日志。
- 基线4c19029由Git导出为测试专用ReferenceCueDetector/ReferenceVisionTargetSelector，与新版同进程、同原始像素、同一次TensorRT结果比较。6个E核，前后计时顺序交替；结果比较所有帧，耗时统计去前30帧。
- 输入E:/迅雷下载/2026-10-20 00-25-46.mp4与00-29-16.mp4、E:/videoCut/2026-10-22 10-40-12-cut-merged-1792637433471.mp4。完整解码帧数23198/15572/665；两段长视频容器nb_frames各多1，ffprobe实际解码计数与工具一致。
- 本轮使用生产0.40解码门槛，旧下拉研究工具使用0.20；不能跨工具直接比较目标帧数。本轮前后门槛及输入相同，跟踪结果严格一致。

## 已发布基线和仍有效的限制

- dev已发布实现3e2ab8d、交接4c19029，clean9aad8f8均已push。本轮在此基础上有未提交优化；上轮提交/同步记录见[同步报告](../docs/project/DOWNPULL_CLEAN_SYNC_20261023.md)。测试/研究留dev。
- 保留person OR cue、同帧强人体与cue身体参考、可靠同一cue超180ms续跟；cue独立运动参考、弱框可靠纹理位移平移身体参考及之前五项跟踪修复。原始透传、逐轴手动、退出、胸部语义、压枪及32/40ms边界保持。
- 实机突然下拉唯一成因/全部消除仍未证明。固定录像第二段其他时刻峰值增大、近似闭环第一段平均误差退步，以及旧ADS开火走位/固定偏置压力反例仍保留。不要增权、压手动或延迟交接掩盖。
- [两项根因修复](../docs/project/DOWNPULL_DEV_FIX_20261023.md)、[录像诊断](../docs/project/DOWNPULL_VIDEO_REPLAY_20261023.md)、[五项跟踪修复](../docs/project/VISION_CONTROLLER_FIXES_20261020.md)。此前详细交接原样归档[优化前handoff](archive/handoff-20261023-before-cue-optimization.md)。

SyncSet sync-20261023-002：沿用明确上下文维护授权，reviewer=accept_draft；更新dev handoff、追加简短会话记录、归档上版handoff。无新决策、subagent brief、平台记忆、秘密或外部应用写入。推导及离线效果不记作用户实机接受；长session-log保留，后续可单独归档压缩。
