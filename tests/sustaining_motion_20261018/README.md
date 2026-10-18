# 有效维持速度的 dev 回归

链接生产控制器和求解器；不替换生产算法、不增加产品开关。生产修改只在
`response_model_aim_solver.cpp`：canonical BodyLock 的有效总目标速度不再乘身份权重；
原位置增益、运动空间约束、曲线反变换及最终二维预算保留。

原生反例 `BaseBodyLock.sustaining_motion_uses_existing_budget_once` 修改前失败、修改后通过。
零位置误差、R=1000、60px/s、authority=.0736：输出必须为 .06，而非 .004416。
同组覆盖二维方向、两种曲线、零权限、超预算、禁用轴、位置增益和 legacy/lookahead 隔离。

## 编译和复跑

在 DEV 根目录的 PowerShell：

```powershell
& tests/clean_incident_20261016/build.ps1 -ProductRoot D:/work/AI/yolo-study-001 -BuildName sustain-focused-after-v3 -SourceRoot D:/work/AI/yolo-study-001/tests/sustaining_motion_20261018
Copy-Item native/build/Release/SDL2.dll runs/clean-incident-fix-20261016/sustain-focused-after-v3/Release/SDL2.dll
& runs/clean-incident-fix-20261016/sustain-focused-after-v3/Release/braking_checks.exe 49979731 runs/sustaining-motion-20261018/after-v3/focused/49979731
& runs/clean-incident-fix-20261016/sustain-focused-after-v3/Release/sustaining_diagnostics.exe runs/sustaining-motion-20261018/diagnostics
python tests/sustaining_motion_20261018/analyze.py
```

对照版链接修复前的生产库。noise/chain 的 before 二进制是在本次修改前从 dev
静态链接；focused 的 before-v3 链接既存候选 `1cb9a12` 的库，其相关生产文件与
dev `f9f42d3` 一致。编译产物全部写入 dev 的 runs，没有编译或改动实际 clean。

## 场景与种子

- 开发：2026101853、32452909；最终独立：49979731、104395409。独立种子运行前生产修改已固定。
- 每种子原 evidence_noise 80 随机 +20 已知相机场景、settling_chain 64 随机场景；
  本夹具另加 48 理想二维场景 +20 弱证据全链场景。均为 2 秒/20 秒。
- 每版本共 928 场景、10,208,000 tick。比较表有逐例误差，不能用总平均代替退步项。
- 理想组使用生产 BodyLock、9ms 相机延迟，12种方向/速度×2曲线×2时长；
  case11 特意超预算，不能要求到点。其余需求在 .0736 预算内。
- 弱证据全链使用真实 NativeGamepadController、交付账本、相机/目标运动估计、
  3%观测降噪、两种曲线；R=2500、9ms作用延迟、10ms capture、最低追赶 .5、
  响应时间100ms。明确设置敌人cue已检查但未发现、置信度 .92，断言近区 BodyLock 权限 .0736。
- 五类：±.35px 居中噪声、小偏差、预算内二维移动、间歇开火、ADS→BodyLock。
  移动组先居中，300ms 开始运动，确保检验 BodyLock；从入场起移动仍由旧随机组覆盖。
  开火夹具施加 .2R 的独立枪口扰动，压枪杆量 .2；非线性曲线下二者不等价。
- 旧随机组保留1575ms目标槽+50ms间隔，随机响应/先验失配、6–30ms相机延迟、
  7–12ms capture、2–12ms投递延迟、遮挡、开火、手动退出和逐轴接管。

## 如何解释结果

全部原生与夹具约束断言通过，不等于整体行为无退步。现有 focused 上界仅为
噪声 RMS<.5px、小偏差均值<.5px、预算内跟随均值<1px及真实交接；它们不会检出
“0.04px变为0.29px”这种相对退步。必须同时读 `comparison.csv` 和报告。

本轮没有通过整体停稳要求：有效恒速缩小已修复，但噪声和部分开火/慢移场景退步，
保持 dev 候选，不合 clean、不接受这些退步。

探索夹具 v1 从入场即移动，线性场景未完成 ADS；v2 未标记已检查敌人 cue，
实际权限 .3325。两者保留用于诊断，不计入最终 .0736 回归结果。正式结果使用 v3。
早期另外两颗种子49979719/104395399仅为探索产物，不计入最终统计。

仿真用浮点1000Hz FIFO相机模型，未覆盖实机设备量化、真实视频检测误差分布、
实际 latest-only 输出调度、枪械/开镜模型；仿真成功不代表实机验收。
