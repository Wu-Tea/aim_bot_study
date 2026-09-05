# ADS 过零抖动与 BodyLock 释放后出力修复

本轮在 `dev` 的 `d892476` 基础上，修正了两个由现场分析引出的生产代码缺陷，并完成 Release 编译与离线回归。**代码级回归已 GREEN；录像症状的完整消除与真实手感仍待实测。** 没有把合成输入描述为视频精确回放。

## 症状、根因与修复所有者

### ADS：屏幕误差过零时放出整段运动补偿

[原分析](D:/work/AI/yolo-study-001/docs/project/ADS_ONSET_200MS_INCIDENT_ANALYSIS_20260905.md) 中，第一段录像的首个事件在目标身份稳定时出现多次 AI 反向。生产 ADS 将屏幕位置变化率传入共享响应求解器：该变化率包含相机、FOV 和目标的共同影响，不是独立确认的目标世界运动。

旧求解器只限制反向运动项，令它在接近零误差时趋零，却在误差等于零或刚与运动同向时返回完整运动项，形成输出不连续。实际录像中的 FOV 贡献和逐 tick 内部响应值未完整记录；可以证明这个代码缺陷，不能从现有日志宣称它是全部抖动的唯一原因。

[ADS 请求](D:/work/AI/yolo-study-001/native/controller_native/ads_acquisition_controller.cpp:64) 现在显式标明运动项是屏幕误差前瞻；[共享求解器](D:/work/AI/yolo-study-001/native/controller_native/response_model_aim_solver.cpp:46) 使用已有的 `0.12` 位置需求邻域，让同向前瞻项也连续收敛到零。接近目标时的反向制动保留，远离中心的原需求保留。

这修正了运动项在零误差处的权限语义，没有增加等待、滤波状态或降低全局 ADS 强度。BodyLock 的持续目标运动请求保持原语义，因此目标居中时仍可用运动补偿持续跟随。

### BodyLock：旧手势把噪声余量升级成反向否决权

原日志出现了“新鲜目标、充分权限、请求与平滑后输出均足够，但最终只输出小幅反向物理摇杆值”。例如 target 172 的请求约 `+0.485`，最终为 `-0.0275`，过滤后的双轴输入已经归零。现场没有直接记录 neutral bias 与 carried 标记，因此现场具体分支是证据支持的推断。

完整 `NativeGamepadController` 回归通过开镜前按住摇杆、ADS 完成、目标移动、释放摇杆的序列，复现了相同类别的阻断。旧仲裁只看二维 `AcquireTarget` 目的和几乎任意非零的反向 centered 输入，提前返回物理值。

[IntentFilter](D:/work/AI/yolo-study-001/native/controller_native/intent_filter.cpp:35) 现在为每轴提供连续活动证据：在该轴的自适应噪声门限内为零，从门限到两倍门限连续增加，明显超出噪声时为一。[调用者](D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:1036) 传给[末端仲裁](D:/work/AI/yolo-study-001/native/controller_native/assist_control_state_machine.h:382)，由它决定旧手势保护的权重。真实按住的反向手势仍受保护；已经释放或落入噪声范围的轴不再否决目标请求。没有仅用 `filtered == 0` 添加一个新的硬切换。

### 释放边界的补充修复

新增慢速释放检查还发现：原始 bias/noise 学习在 raw 输入进入 `neutral_learning_limit` 时突然开启，新活动权重会把这个校准突变传播到最终输出，最坏单步变化为 `0.233891`。

因此在[输入校准所有者](D:/work/AI/yolo-study-001/native/controller_native/intent_filter.cpp:14) 将学习权重按距离学习边界的余量连续增加。没有添加输出延迟或另一个平滑器。候选曾使用更缓慢的曲线，导致旧静态漂移回归无法在原冻结时长内校准；最终采用线性进入权重，同时满足原校准回归与新释放曲线门限。旧测试和新测试的门限均未放宽。

## RED → GREEN 证据

| 检查 | 修复前 / 中间候选 | 最终候选 | 固定接受条件 |
|---|---:|---:|---|
| ADS 48 组过零矩阵，最大单步输出变化 | 0.622222 | 0.00156746 | ≤ 0.01 |
| 同一矩阵，零位置误差处最大输出 | 0.622222 | 0 | ≤ 0.00001 |
| BodyLock 164 个已释放样本，最终与平滑请求的最大差值 | 0.670000 | 0 | ≤ 0.01 |
| BodyLock 164 个真实持续按住样本，最终与物理值最大差值 | 0 | 约 7.45e-9 | ≤ 0.00001 |
| 慢速释放 32 个组合，最大逐样本输出变化 | 中间候选 0.233891 | 0.0357245 | < 0.04 |

ADS 输入矩阵为 X/Y、正反向 1400 px/s、300/500/900 响应信念、近/远目标和 Linear/Dynamic 曲线；误差取 `-0.01/0/+0.01 px`。速度为机制验证用合成输入，不冒充现场每 tick 真值。反事实包括零速度位置需求和 BodyLock 居中持续运动；后者双轴输出仍为 `0.2`。

BodyLock 完整控制器回归使用 1 kHz tick、200 Hz 新观察，中间保留非新帧 tick；固定同一目标与 generation，当前敌人证据充分。对照开镜前的 ±0.12 持续输入，以及 80 ms 后释放到 ±0.01 的输入，检查 90–130 ms 的出力。覆盖双轴正反方向，并验证 LT 释放立即退出。输入与观察时间表均为确定性构造，没有相机 plant，因此不能推导真实游戏的误差收敛时间。

慢速释放另覆盖两档噪声门限、正负偏置、双轴正反向、开火/不开火；32 个组合都先验证明显按住时的物理输出，再检查释放连续性与所有 filtered-neutral 样本的 AI 接管。

![确定性回归曲线](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/regression-curves.png)

[事件测试源码](D:/work/AI/yolo-study-001/native/controller_native/ads_bodylock_september_incident_tests.cpp) 在生产修改前运行，两个命名用例均以 `exit 1, invalid 0` 形成 RED。固定输入、断言、阈值和反事实在候选上保持逐字节一致；唯一登记修正是将 suite 名 `BaseADS` 改成现有 CTest 的 `BaseAds`，让它进入完整回归，manifest 记录该登记差异与两个源码哈希。

- [ADS 完整契约检查](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/ads-complete-contract.json)、[BodyLock 完整契约检查](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/bodylock-complete-contract.json)。
- [完整构建](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/final-build.log)、[11/11 CTest](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/final-tests.log)、[360 个命名用例记录](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/final-LastTest.log)。覆盖 ADS、BodyLock、身份、手动控制、生命周期、AutoFire、Recoil、Vision freshness、输出和 Fusion 契约。
- [启动器程序编译日志](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/launcher-build.log)、[源码与产物哈希](D:/work/AI/yolo-study-001/artifacts/controller-september-fixes-20260905/artifact-manifest.json)。独立验证构建与实际启动器构建分别记录二进制身份，不假定不同构建目录的 exe 字节一致。

## 当前测试版本与实机检查

启动器使用的 [cod_native_runtime.exe](D:/work/AI/yolo-study-001/native/vision_native/build/Release/cod_native_runtime.exe) 已重新编译，SHA-256：`eab6a4a6e6c989de093e5f8e35b31de312ebab2f863472e0212c68ade19234ab`。旧录像对应二进制 SHA-256 为 `272d7434a1955c23f161525ffb30578d3480431c1f8ebe0932168d40e649ae7f`，保留了其副本用于身份核查。

继续使用原来的 [Fusion 启动脚本](D:/work/AI/yolo-study-001/scripts/launch/gamepad_fusion_background_start.ps1)，无需更改配置。没有启动游戏输出进行自动实测。本次沿用原 MSVC/CUDA/TensorRT 与构建选项，生产 config 未变。

建议保持同武器、同 FOV、同原生辅瞄关闭条件，先验证准星已贴近目标时开镜，再验证 ADS 结束后目标横移、右摇杆缓慢释放，最后对照真实持续反向操作和开火下拉。重点看最初 200 ms 的摆动，以及松杆后是否仍只剩一点反向小力。

第二段录像还有两类小力度：target 162 超出当前继续跟踪半径、target 166 缺少确认敌人证据。这两类均有现行准入规则依据，尚未证明其规则或感知输入错误；本轮保持范围、选敌与低置信度预算，不能宣称所有“识别到但小力”情况都已修复。现有 telemetry 历史样本 drain tick 归属问题也未改动，后续分析仍须按 sample/output 时间与身份严格连接。

本轮未做 Vision 性能重构、AimLab 优化或现场 A/B。应在实测后决定是否需要继续处理 FOV/相机运动分解、选择器交接或敌人提示漏检。建议把本报告和实机验收状态同步到 `.agent-context/`；本轮没有修改这些上下文文件。
