# 非右杆运动即时贴合：ADS 持续落后 RED

2026-09-26。结论：**已在真实 controller 链的合成闭环中复现“有输出，但迟迟贴不上移动目标”**。它发生在 ADS 首次获取阶段。右杆全程为零，因此可以在人工修正之前独立出现。

本轮不修改生产算法。结果是所声明合成条件下的 RED；对用户那次真实滑铲事故仍是 `EXPLORATORY / MISSING COVERAGE`。没有把合成外部运动伪装成实战精确回放，也没有复现后续人工与 AI 冲突的全过程。

## 冻结条件与结果

基线为提交 `02ec6a40b1210c517a1821924865d8b9edb0ab8e` 的独立源码快照，排除同时存在的未提交实验。配置是本轮执行前保存的当前 config.toml；该文件未受 Git 跟踪。配置 SHA-256：`b2090d89fdc1219c15e2ee6d47b0d8796ea14576d720bdba366843102e8279f1`。

使用生产 `NativeGamepadController` → DS4 序列化 → 实际量化坐标交付回执 → 延迟相机模型。controller 1000 Hz；主矩阵 Vision 200/500 Hz；零 source age、零测量噪声、恒定身份。相机响应 650、效果延迟 9 ms、曲线均与 controller 的配置相匹配；无游戏原生 AA。只关闭独立 recoil，物理开火分别开/关。八方向、120/240 px/s、正向→反向→停止，另用两个预先指定 seed 扩展至随机角度和短/长运动段。

速度所需的理想维持输出均在配置 BodyLock 椭圆预算内，最高约 70.7%；不是故意制造执行器饱和。由于真实游戏 plant 未标定，这仍是隔离问题的假设。

| 条件 | 运动样例 | 失败 | 结果 |
|---|---:|---:|---|
| 先进入 BodyLock，再启动外部运动；固定矩阵 | 64 | 0 | 当前 tick 启动响应，100 ms 过渡后保持在 8 px 内 |
| 外部运动已经发生，带 60 px 偏差按下 ADS；固定矩阵 | 64 | 64 | 超过 355 ms 仍未获取，持续落后超过 8 px |
| 随机探索 seed 20260926：BodyLock / ADS | 34 / 30 | 0 / 30 | 相同分界 |
| 独立 seed 20260927：BodyLock / ADS | 39 / 25 | 0 / 25 | 相同分界 |
| 所有配对静止反事实 | 256 | 0 | 仅将外部速度设为零，仍保留相同左杆/开火脚本 |

合计 512 例：256 个运动样例、256 个静止对照；119 个运动样例失败，全部为 ADS 组。512 例触发断言全部通过，没有无效样例。各例 M=0、持续目标身份、fresh 数量、observer 有效、输出有限有界及 LT 释放透传均验证通过。

O1 启动响应、O2 持续贴合、O3 获取预算、O4 停止后回摆在[首次运行前合同](NON_RIGHT_STICK_MOTION_CONTRACT_20260926.md)中定义。失败集中于 O2/O3。8 px 使用当前到位半径，100 ms 过渡和 355 ms 获取预算是本轮预先声明的工程目标；355 ms 并非声称现有代码必须停止所有 AI 输出的生命周期截止时间。

## 最小可读例子

下表均为 200 Hz Vision、横向、不开火、相同 60 px 初始偏差：

| 外部速度 | 355 ms 的落后 | 699 ms 的落后 | 首次进入 8 px | 进入 BodyLock |
|---|---:|---:|---:|---:|
| 0，配对静止控制 case 129 | 已完成获取 | 接近零 | 255 ms | 265 ms |
| 120 px/s，case 128 | 16.82 px | 14.26 px | 729 ms | 740 ms |
| 240 px/s，case 144 | 30.19 px | 28.24 px | 753 ms | 765 ms |

运动脚本在 700 ms 反向。后两例直到外部运动反向帮助误差穿过到位区后才完成获取，不能把 740/765 ms 当成继续原方向移动时也能完成的时间。

500 Hz 并未消除此失败：120 px/s 对应例 case 192 在 355 ms 仍落后 16.92 px，734 ms 才进入 BodyLock。独立 seed 的长例 case 470 保持原方向运动 2.5 秒，2499 ms 时仍距 D 约 28.18 px、仍为 ADS；反向之后到 2559 ms 才进入 BodyLock。

![冻结 controller 的闭环误差曲线](../../runs/non_right_motion_20260926/response.png)

BodyLock 组通过的是合同给定的过渡预算，不代表任何瞬间都无偏差。例如 case 16 在突变附近的全程峰值为 10.61 px，100 ms 过渡后的峰值为 4.28 px。没有用“通过”隐藏这个瞬态。

## 因果链与归属

1. 外部运动发生，可信新帧持续到达，目标身份不变。ADS 在第一个 tick 就开始输出，不存在这里所怀疑的整段“等一等再动”。
2. 总运动 observer 正常工作。case 128 在 20 ms 已估到约 120 px/s；355 ms 时估计 120.003 px/s，response 650.004，与 plant 一致。
3. **ADS 控制器没有消费已经可用的总运动估计。** `ads_acquisition_controller.cpp` 用 `plan.error_px` 和 `plan.error_rate_px_per_sec` 构造请求，后者只是屏幕误差变化率，以短 lookahead 参与制动。`bodylock_target_motion_px_per_sec` 由另一条 BodyLock 路径消费。
4. 当相机追赶速度接近外部运动速度，屏幕误差变化率接近零，ADS 的速度项也接近零。继续维持相机速度就只能依赖不为零的位置误差。因此形成稳定落后，而不是最终贴到 D。
5. 当前目标大小给出的横向到达 horizon 为 117.543 ms。在未饱和、单位权威、匹配响应、误差变化率接近零时，解算约为 `camera_speed = error / horizon`。稳态要求 `camera_speed = external_speed`，所以 `steady_error ≈ external_speed × horizon`。120 px/s 预测 14.105 px，699 ms 实测 14.257 px；240 px/s 预测 28.210 px，实测 28.237 px。残差受瞬态和 DS4 量化影响。
6. 落后超过 8 px，真实到位条件不会成立。`target_coordinator.cpp` 正确保留未完成 ADS，超出扩展预算进入 manual-safe 状态，仍保留 ADS 解算；已经具备持续跟随语义的 BodyLock 得不到正常到位交接。

该合成缺陷的首要归属是 **ADS 运动解算语义**，而不是 fresh cadence、observer 饥饿或到位门槛本身。case 128 在 355/699 ms 的 requested、shaped、final 三者相等，说明那时没有后级 slew 或仲裁继续压低请求；错误的追赶平衡已在请求层形成。源码位置与快照哈希见 regression manifest / artifact manifest。

这解释了为何“继续提高 Vision 频率”不能消除该类落后，也说明不宜通过放宽到位条件、硬切 BodyLock 或增加超时开关来掩盖它。若后续授权修复，应优先审查 ADS 如何在保持制动和到位保护的同时，正确使用可信的持续运动需求。本轮没有验证任何修复方案。

## 复现与证据

fixture：[non_right_motion_probe.cpp](../../scripts/verify/non_right_motion_probe.cpp)。冻结工程、完整报告、逐 tick CSV、PNG/SVG 和哈希位于 `runs/non_right_motion_20260926/`。

```powershell
Push-Location D:\work\AI\yolo-study-001\runs\non_right_motion_20260926
cmd /c build.cmd
.\probe.exe source/config.toml reproduced.json -1
$LASTEXITCODE  # 预期 1：声明的响应目标 RED；2 表示触发/对照失败
Pop-Location

python .agents/skills/incident-to-regression/scripts/regression_contract.py check `
  --manifest runs/non_right_motion_20260926/regression-manifest.json `
  --stage red --output runs/non_right_motion_20260926/rechecked-contract.json
```

输出文件存在时程序拒绝覆盖，复跑应使用新文件名。隔离 build.cmd 使用 Visual Studio 2022 Professional 的 x64 C++ 工具链。它编译冻结的完整生产 controller core 源文件清单，不改运行中的游戏程序。

首次跑测的 fixture 身份断言曾错误要求“无新帧 tick 的 plan 也必须携带非零 selector generation”，因此首次输出全部标为无效。源码与逐 tick trace 证明生产路径仅在 fresh batch 携带该元数据，持有 tick 的 target ID 仍不变；随后修正为全程 target ID 不变、fresh tick generation=1。保留初始失败报告和修正说明，没有更改场景、oracle 或生产代码。最终报告又随三个指定 case 的 trace 采集完整执行三次，512 例 JSON 结果逐字节相同。

## 尚未覆盖

- 真实滑铲的加速度、相机/FOV 变化、游戏原生 slowdown/rotational AA、检测误差、帧龄及调度波动。
- 初始偏差与运动方向相反的迎面穿越、实际失去目标/替换和 target search；本轮由可信单目标输入直接隔离 controller。
- 用户随后手动修正的具体杆轨迹、D 修改、manual authority 交互以及冲过后反拉的完整实战因果链。本轮结果证明前半段“按住 ADS 仍贴不上”可以独立出现，不能把后半段当成已经复现。

完整测试 package 已建立，可在后续生产修复时原样作 RED→GREEN 对照；还需补人工修正和真实游戏协变量，才能判断实战是否改善。
