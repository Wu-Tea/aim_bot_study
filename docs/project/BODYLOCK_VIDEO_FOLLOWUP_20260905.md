# 第二段录像：BodyLock 跟不上，简要分析

结论：录像对应日志中至少存在三条不同路径，不能统一解释为 BodyLock strength 不够。最值得优先追查的是释放输入后的反向仲裁，以及目标选择与继续跟踪范围之间的配合。

沿用 [ADS 分析的会话、配置和完整前缀审计](D:/work/AI/yolo-study-001/docs/project/ADS_ONSET_200MS_INCIDENT_ANALYSIS_20260905.md)。本次范围为墙钟估计 14:31:20–14:31:35、物理 LT ≥ 0.80；这是覆盖第二段视频的宽定位窗口，不是逐帧同步。以下为描述性现场证据，没有做生产修改或宣称完整因果回放通过。

| 片段，本地时间估计 | 实际记录 | 负责层与判断 |
|---|---|---|
| 14:31:25.004–25.606，target 162 | 54 条 controller 采样均仍有当前目标，但 aim authority 被拒绝，AI X/Y 请求均为 0。一个精确 frame/observation 配对的源样本误差为 226.48 px、normalized size 为 0.265951，对应继续跟踪半径约 143.94 px。 | TargetCoordinator 的范围判断将 aim authority 置零；因此求解器算得出位置/运动需求，最后仍产不出 AI 请求。 |
| 14:31:28.365–28.822，target 166 | 共 41 条 controller 采样，其中 32 条有权限、9 条被拒绝。代表样本水平误差 -138.68 px，visual authority 仅 0.157852，AI 请求和平滑后均为 -0.152182，最终也是 -0.152182。 | 该样本没有当前敌人提示、没有确认的敌人身份且已做过提示检查，进入低权限搜索分支；权限同时是最终出力预算，强度增大仍绕不过该上限。 |
| 14:31:31.347–31.793，target 172 | 共 38 条采样。31.564 时误差 +31.92 px，请求和平滑后均为 +0.484945，最终却为 -0.0274658，恰好等于物理输入；过滤后的 X/Y 输入均为 0，visual authority 为 1，无主动退出或瞄点修正。 | 请求已充分产生，损失发生在平滑之后的手动仲裁；这与第一段“旧手势阻断反向 AI”的机制吻合。 |

**1. 目标检测与跟踪权限是两件事。** 第一类明确解释了“识别到了但没有出力”。[范围计算](D:/work/AI/yolo-study-001/native/pipeline_contract/target_acquisition.h:12) 为 `base × (1 + 0.75 × normalized_size)`，匹配会话的 BodyLock base 为 120 px。[范围外清零](D:/work/AI/yolo-study-001/native/controller_native/target_coordinator.cpp:1024) 是硬条件。这段日志能证明该门限生效，尚不能证明应该无条件扩大门限：仍需核对选择器为什么保留这个外侧目标、是否应交接其他候选，以及该目标是跟丢后变远还是本来就选在范围外。

**2. 第二类的小力是请求生成阶段的权限预算。** [敌人证据分支](D:/work/AI/yolo-study-001/native/controller_native/target_coordinator.cpp:992) 在缺少当前提示且未确认身份时使用较低 authority；[共享求解器](D:/work/AI/yolo-study-001/native/controller_native/response_model_aim_solver.cpp:97) 又将 authority 作为输出上限。这里的 `assist_authority=full` 只代表有正常目标权限，并不表示数值 authority 为 1。提示为什么未被确认尚未逐帧验证，不能把所有无提示人物都默认视为可靠敌人。

**3. 第三类与强度无关，值得优先修复。** 下图中 AI 请求已经朝右，最终输出仍随左向手柄输入；随后突然恢复较大右向输出。

![BodyLock 请求与实际输出](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/bodylock-followup/bodylock-output-block.png)

31.347 起用户确实有约 -0.106 的左向输入，不能把整段当作 M=0；但到 31.554–31.564，过滤后的双轴输入已经归零，AI 仍被挡住。31.579 的下一条记录中，物理 X 只从 -0.0275 变到 -0.0196，最终输出却由 -0.0275 恢复到 +0.536。该相邻采样现象说明手动释放附近存在敏感的权限交接，不能简单归咎于用户一直反向操作。

[IntentFilter](D:/work/AI/yolo-study-001/native/controller_native/intent_filter.cpp:78) 会在失去目标时把手势目的设为 AcquireTarget，归零本身没有在该段逻辑里清除目的；[NativeGamepadController](D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:1040) 再把该目的传为 carried acquisition gesture；[仲裁提前返回](D:/work/AI/yolo-study-001/native/controller_native/assist_control_state_machine.h:378) 可能让很小的 bias-centered 余量继续否决反向 AI。现场没有直接记录该标记和 neutral bias，因此具体触发仍是由输出特征和源码推断；已确认的事实是“请求生成正常、平滑后仍在、最终输出被替换为手柄值”。

优先顺序建议：先把手动释放后的 AI 接管边界做成回归；再处理选择器、范围外拒绝与目标交接的一致性；最后检查敌人提示为何反复降低 authority。只调大 strength 无法解决零权限和末端直接透传手柄的情况。

**覆盖与限制。** 三个片段均沿用去重后的原始 sample 时间；controller 记录本身提供同次采样的请求/平滑/最终值，不使用最近行补配。target 162/166/172 的 ADS trace 与 observation 精确配对覆盖分别为 76/82、55/61、52/62；未配对项不补造。target id 是 native 内部身份，不能仅凭 id 变化断言游戏里换了人。各段详细采样、原始行号与连接统计保存在 [分析数据](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/bodylock-followup/analysis.json)，复现脚本为 [analyze.py](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/bodylock-followup/analyze.py)。本次只新增分析资料，没有改变生产代码、配置或测试程序。后续同步项目上下文时，建议将这三类情况分开记录。
