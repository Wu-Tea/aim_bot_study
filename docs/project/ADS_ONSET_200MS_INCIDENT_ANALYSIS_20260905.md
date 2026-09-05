# 2026-09-05：开镜前 200 ms 抖动与日志定位

已确认第一段录像存在真实的 AI 出力反复换向，并定位到两条不同的控制路径：约 8 秒处是 ADS 求解器在误差过零时突然释放速度项；约 11.6 秒处是越过中心、转入 BodyLock 后，旧开镜手势的保护分支阻断了反向纠正。两者不能统称为 FOV 缩放或游戏辅瞄减速。

**证据结论：INSUFFICIENT_EVIDENCE，适用于完整现场因果回放与产品验收。** 已有证据足以确认实测波形、定位负责模块，并在直接调用生产源代码的独立探针中复现两个机制；目前尚未形成忠实的现场 RED 回放，也没有修改生产控制行为。探针是 `EXPLORATORY / MISSING LIVE REPLAY COVERAGE`，不能冒充实战回放或修复验证。

## 对应视频、会话与时间基准

两段视频均对应会话 [20260905T062921Z_51244_1](D:/work/AI/yolo-study-001/runs/native_perf/sessions/20260905T062921Z_51244_1/session.json)。启动记录为本地时间 14:29:21，停止记录为 14:31:42。视频 NTFS 创建时间均为 14:32:49 左右，晚于实际录像保存时间，不能当成画面开始时间。

| 视频 | 视频轨时长 | 文件最后写入时间，本地 | 按文件名结束秒推算的起点 | 对应日志 |
|---|---:|---|---|---|
| Replay 2026-09-05 14-30-03.mp4 | 14.708333 s | 14:30:04.062798 | 14:29:48.291667；文件名秒内位置及保存延迟未校准 | native acquisition 6、7；约 14:29:57 与 14:30:00 |
| Replay 2026-09-05 14-31-34.mp4 | 13.925000 s | 14:31:34.935109 | 14:31:20.075000；同样存在秒内与保存误差 | 同一会话 14:31:20–14:31:35 的宽定位窗口 |

视频没有嵌入 `creation_time`。第一段画面两次起镜动作大致位于 8.2 s、11.65 s，相隔约 3.45 s；日志对应两次物理 LT 事件相隔 3.447 s。该动作序列用于核对事件身份，不把文件名估算当作逐帧同步。视频与日志的绝对偏移仍是推断值。

**200 ms 主图以物理 LT 越过 0.05、触发 `aim_started` 的输出 tick 为零点。** Native 取得目标并开始求解分别晚 20.945 ms、12.937 ms。物理 LT 就绪阈值为 0.80，不能把轻按、就绪和目标接纳混为同一个时间点。

| 事件 | LT 事件的 steady ns | 对应墙钟估计 | native acquisition 起点 steady ns | 接纳相对 LT 延迟 |
|---|---:|---|---:|---:|
| 约 8 秒 | 99662089116600 | 14:29:57.247 | 99662110061500 | 20.945 ms |
| 约 11.6 秒 | 99665536117100 | 14:30:00.694 | 99665549054100 | 12.937 ms |

墙钟估计使用本机 QPC 与精确系统时间的回溯桥接，假设同一启动周期内没有系统时间跳变；当前读数的窄误差带不代表历史视频同步精度。控制曲线内部时间差直接来自同一 steady clock，独立于该墙钟假设。

## 实测曲线

![物理 LT 上升沿之后的 200 ms](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/ads-lt-first-200ms.png)

左右两列分别是两次事件。第一行是日志中的目标误差；第二、三行分解 AI 请求、平滑后的 AI、最终输出、物理右摇杆及 recoil；第四行是检测框尺寸。横轴单位 ms。X 正方向为右，目标误差 Y 正方向为下，而手柄输出 Y 正方向为上。

所有曲线均来自未平滑的实测采样点，连线仅用于阅读。200 ms 内两次事件分别有 43、41 条去重后的 controller 样本，最大相邻采样间隔分别为 5.568、9.823 ms；对应新 Vision/ADS trace 分别为 24、25 条。配置中的 1000 Hz controller、500 Hz telemetry 不代表已经保存了每一个 1 kHz tick，不能从这份日志恢复未记录的 tick。

可下载 [SVG 曲线](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/ads-lt-first-200ms.svg)、[带原始行号的 CSV](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/ads-lt-first-200ms.csv)。另有 [从 native 接纳目标开始计算的 200 ms](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/ads-first-200ms.png)，其时间起点不同，不能混用。

## 事件一：过零时的速度项突变

**实测事实 F1：水平 AI 自身发生三次有意义的反向，右摇杆没有同样反复换向。** 这不是仅由图像框标注或遥测补写形成的假波形。

| 相对 LT 时间 | 水平目标误差 | 水平 AI 请求 | 水平最终输出 | 含义 |
|---:|---:|---:|---:|---|
| 21.0 ms | -33.25 px | -0.446 | -0.064 | 开始向左接近 |
| 51.0 ms | -9.92 px | -0.135 | -0.135 | 仍在中心左侧 |
| 59.0 ms | +1.25 px | +0.208 | -0.087 | 请求已反向，平滑器仍在卸掉旧方向出力 |
| 66.0 ms | +9.83 px | +0.358 | +0.271 | 右向请求明显增大 |
| 126.0 ms | +1.00 px | +0.00044 | +0.010 | 再次回到中心附近 |
| 133.0 ms | -3.67 px | -0.192 | 0 | 再次反向 |
| 156.0 ms | +4.75 px | +0.039 | +0.039 | 第三次有意义的反向 |

该窗口的 native target id 始终为 23，selector generation 始终为 6；没有观察到身份切换。物理右摇杆 X 范围为 -0.192 至 +0.035，逐渐释放到小幅正值；它没有复现 AI 的左、右、左、右序列。第一轮换向早于日志中的开火/recoil 脉冲。

**负责模块与算法机制 F2：** [ADS 控制器](D:/work/AI/yolo-study-001/native/controller_native/ads_acquisition_controller.cpp:55) 将 `error_rate_px_per_sec` 作为速度项传入共享求解器。[求解器的 `bound_opposing_axis`](D:/work/AI/yolo-study-001/native/controller_native/response_model_aim_solver.cpp:44) 在速度项与当前位置误差相反时限制速度项；误差过零以后，两项突然变为同向，完整速度项立即放行。误差恰好接近零时还有直接返回完整速度项的分支。

这是一个可以独立确认的连续性问题。直接编译、调用生产 `solve_response_model_aim`，固定水平速度为 +1400 px/s、响应参数 500、horizon 135 ms，并使用当前配置的 COD Dynamic LUT，只改变位置误差：

| 水平误差 | 有速度时的 AI 请求 | 速度为零的对照 |
|---:|---:|---:|
| -0.01 px | 约 0 | -0.000498 |
| 0 px | +0.337083 | 0 |
| +0.01 px | +0.337197 | +0.000498 |

0.02 px 的位置变化对应约 0.337 的请求变化。该现象在 X/Y 两轴、正负速度及多个响应参数下都检查过。[探针结果](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/mechanism-probe-results.json) 记录了所有数值。这里的速度、响应参数是冻结的机制实验输入，**不是声称现场恰好使用了这些内部参数**。

现场第一次过零附近，由相邻 source-present 时间及源误差计算的水平位移速度约 +1081 px/s，确实存在较大速度项的触发条件。但日志没有直接保存每 tick 的 ADS 响应估计值、限幅前后的速度分量，因此目前不能声称已经用完整现场输入重放出相同幅度。

传播路径是：源位置穿过中心 → 速度限幅分支改变 → 请求突然增大或换向 → 平滑器延后卸掉旧输出 → 画面继续运动后再次纠正。第一处已经定位到这个算法放大机制；FOV、目标本身移动与相机运动各占多少，仍需要补充回放证据。仅凭框尺寸变化不能把它们分离。

**附带现象 F3：** 第一处日志还记录了 Y 轴 recoil 额外叠加 -0.20 的短脉冲，controller 采样覆盖约 LT 后 115–137 ms。它会在纵向最终输出里制造额外台阶，但发生在第一次水平反向之后，不能解释整段水平抖动。用户关闭的是游戏辅瞄，不等于本项目的 AutoFire/recoil 停用；这里按实际日志分别处理。

## 事件二：纠正需求到达了，最终输出被旧手势挡住

**实测事实 F4：** 第二次接纳目标时，源目标在准星右侧约 116 px，随后越过中心。该窗口并非“接纳瞬间误差为零”，不能为了套入旧假设而改写现场条件。

| 相对 LT 时间 | 水平误差 | 请求 | 平滑后 | 最终输出 | 状态 |
|---:|---:|---:|---:|---:|---|
| 126.0 ms | +20.50 px | +0.373 | +0.489 | +0.489 | ADS |
| 133.0 ms | -17.33 px | -0.420 | +0.325 | +0.325 | center-cross，切入 BodyLock |
| 140.0 ms | -17.33 px | -0.420 | 0 | +0.0588 | 旧输出卸到零，物理右摇杆透传 |
| 145.0 ms | -33.50 px | -0.551 | -0.319 | +0.0588 | 纠正请求已向左，最终仍向右 |
| 149.0 ms | -50.83 px | -0.583 | -0.576 | +0.0588 | 相同矛盾持续 |
| 194.1 ms | -55.67 px | -0.464 | — | +0.0510 | 仍未取得反向控制 |

149 ms 这一条 controller 与 delivered 样本以相同 `output_sent_ns` 精确对齐，原始 tick 为 38023，原日志 controller 行号 15806；同时有新 Vision trace。日志中 visual authority 为 1、目标仍被观测、没有主动退出或 D 点修正、手柄连接和输出成功。整个前 200 ms 没有开火/recoil 输出。因此该段不能归因为低置信度、减速或 recoil。

**负责模块与机制 F5：** [NativeGamepadController](D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:1040) 在进入 BodyLock 后，把仍然属于 `AcquireTarget` 的手势标成 `carried_acquisition_gesture`。[最终仲裁](D:/work/AI/yolo-study-001/native/controller_native/assist_control_state_machine.h:378) 遇到该标记、极小的非零反向意图和相反 AI 方向时，直接返回物理手柄值；它在后面的轴耦合、释放判断和新鲜位置纠正逻辑之前提前返回。

独立探针使用现场的误差 -50.833 px、平滑后请求 -0.576019、物理 X +0.0588092、visual authority 1，只对该标记及未记录的 bias-centered X 做有限范围控制：标记打开、centered X 为 +0.001 至 +0.06 时，最终输出均为 +0.0588092；标记关闭时，输出均为 -0.576019。这复现了现场“左向 AI 请求被变成小幅右向输出”的特征。

这里已经确认负责分支及其机制；现场的 `carried_acquisition_gesture` 与 neutral bias 未直接记录，触发标记由调用条件、模式、现有诊断和其他分支排除推断，不能假装是一个已记录字段。不能用统一加大 BodyLock strength 修复这个分支，因为它直接返回了手柄值。

该保护原本用于保留用户真实的开镜前手势。问题在于二维手势目的被带入逐轴仲裁后，可能把很小的某轴余量也升级为反向控制的否决权。后续修复应在手势语义与模式交接的负责层处理，同时保留明确手动退出和开火下拉的行为。

## 第二段 BodyLock 视频的日志位置

已提取 [14:31:20–14:31:35 定位窗口](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/bodylock-located-window.json)。这是覆盖视频的宽窗口，不是逐帧同步结果。

该窗口里已出现另一种需要单独追查的现象：14:31:25–14:31:26 中，有 54 条 target 162 的 `body_lock` 采样，平均绝对水平误差约 226 px，水平 AI 请求本身为零，最终仅保留约 0.012 的物理输入。这与第一段事件二的“请求很大但末端挡掉”不同，必须分别核对 continuation 范围、目标选择和生命周期。此次按最新要求优先完成两次开镜前 200 ms 的细节分析，不将第一段的机制直接外推成第二段的全部根因。

## 日志完整性、源码对应与复现

原始日志为 [native_runtime_telemetry_00005a9bf7d82f14fecc626ff6e6d1d5_0.jsonl](D:/work/AI/yolo-study-001/runs/native_perf/sessions/20260905T062921Z_51244_1/native_runtime_telemetry_00005a9bf7d82f14fecc626ff6e6d1d5_0.jsonl)。它有 64,685 行，最后一行在停止时截断，位置约为 14:31:41.835，晚于两段视频事件。原文件未修改，全会话审计仍标为 BLOCKED。

本次保留原始 1–64,684 行形成逐字节相同的独立前缀，并记录源哈希、字节范围和明确排除的末行，重新完成 [范围内审计](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/scoped-audit-intake.json)。范围内没有解析损坏，身份相符；硬件/刷新率未由会话冻结、混合日志及视频时钟覆盖仍使完整验收证据不足。没有放宽解析阈值，也没有补造半行内容。

另外发现了一个遥测归属缺陷：[采集器 drain 环节](D:/work/AI/yolo-study-001/native/runtime_app/telemetry_collectors.cpp:337) 把补写的历史 controller 样本标成当前 drain tick。完整前缀有 15,863 条 controller 记录，其中 809 条是内容相同的重发；15,054 个唯一采样中，14,201 个能与 delivered 记录的输出时间精确对齐，833 个保留样本的头部 tick 与对应实际 tick 不同，853 个没有同时间的 delivered 采样可核验。不能按行顺序或单独的 tick_id 做连接。

本次先以 `sample_seq` 去重，验证重复样本除 drain tick 外内容完全相同；按原始 `sample_ns` 排序；跨 controller/delivered 使用相同 steady clock 的 `output_sent_ns` **完全相等**及最终向量一致性验证；ADS 与观察只使用完整 frame/observation 标识。没有使用最近行或最近时间补配。[连接审计](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/analysis-integrity.json) 保留计数和示例。这是分析可靠性缺陷，与游戏实际出力机制分开记录。

运行时身份：

- 分支 `dev`，基准 commit `30014470af9ea09a673ffa29d8972a9da149ae93`，包含本任务此前已编译的 native 修复。
- 实际 runtime SHA-256：`272d7434a1955c23f161525ffb30578d3480431c1f8ebe0932168d40e649ae7f`，与当前启动器使用的二进制相同。
- Runtime config hash：`fcb027ef1fdf5b0bd245b79889546a89b70e860de4ad77ddc8aa6f9c4fc080be`。已验证原配置字节加原生 provenance context `profile=;auto_fire=RB;capture_fps=200` 后完全匹配，普通文件 SHA 与 contextual hash 不同属于预期。已保存配置副本。
- Engine hash：`45fc56274ff3bbc659e534c3b7833065b0483ef8022ac5d7657cd6da7dbdeb21`；遥测 schema 18，performance schema 2。
- 原始 telemetry SHA-256：`659a53ea060f6eeeb8c0941a4a23f56771cc76734e3acb4d917190286584c3b7`。
- 分析前缀 SHA-256：`840fb7661011929eeaf67f32a27221fae004a4aa9ed63775958c7634b7f9c973`。

[源码与二进制对应记录](D:/work/AI/yolo-study-001/artifacts/telemetry-audits/20260905-ads-bodylock-video/runtime-source-proof.json) 包含负责模块哈希及独立探针哈希。探针编译成功、运行返回 0；这是机制实验完成，不是 RED/GREEN 产品验收。生产源码、生产配置和供主观测试的 exe 均未因本次分析改动。

复现分析与绘图：

```powershell
D:/env/python/python.exe artifacts/telemetry-audits/20260905-ads-bodylock-video/analyze_incidents.py
D:/env/python/python.exe artifacts/telemetry-audits/20260905-ads-bodylock-video/plot_ads_windows.py --anchor physical
```

复现机制探针：

```powershell
cmake -S artifacts/telemetry-audits/20260905-ads-bodylock-video -B artifacts/telemetry-audits/20260905-ads-bodylock-video/build -G "Visual Studio 17 2022" -A x64
cmake --build artifacts/telemetry-audits/20260905-ads-bodylock-video/build --config Release --target ads_mechanism_probe
& artifacts/telemetry-audits/20260905-ads-bodylock-video/build/Release/ads_mechanism_probe.exe
```

后续应先把 F1/F2 的过零突变与 F4/F5 的模式交接否决分别变成有现场触发条件、反事实和量化输出 oracle 的 RED，再修改负责模块。缺失项包括完整 ADS 内部响应/速度分解、逐轴手势语义、相机/FOV 与目标运动分解，以及逐 tick 原始输入/多候选历史。当前无需靠增加平滑延时、统一降低强度或扩大死区掩盖波形。建议在后续同步 `.agent-context/` 时记录这两个 incident 与遥测 drain tick 缺陷，防止再次按错误的 tick 连接分析。
