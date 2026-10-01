# Apex 开火视角抖动：反馈缺陷修复与验证边界

后续：用户确认局部 recoil 修正没有消除主要抖动。新会话中的 semantic 瞄准点虚假运动路径已另行修正并部署，最新状态见 [物理位移测量修正](APEX_PIXEL_MOTION_MEASUREMENT_FIX_20261001.md)。本报告保留此前局部缺陷及其离线验证，不能表示实战抖动已解决。

两段录制和配套日志都存在辅助控制反复反向的证据。本次确认并修复了一个 **BodyLock 将独立压枪贡献再次计入目标持续运动** 的代码缺陷。离线 RED→GREEN 已完成；第二段录制中的横向剩余振荡尚不能全部归因于这个缺陷，必须保留实测验证边界。

## 两次实战证据

会话为 `20261001T034319Z_26276_1` 和 `20261001T035108Z_28632_1`。两次均正常收尾，使用同一程序 SHA-256：

`1976c5d833532195bfd19511ff2d9de62102b61be544667744ab0184415aac67`

引擎哈希也相同。配置哈希分别是 `64c16b9a6ef6c59a230b6e4de26df88f41a32e3a8b4ad7f98207f8d5cdf54e5f` 和 `11922ccc57142ce70438da01af5ace435dd1fb2ecdbe9677a3f845f6a7a1184a`，所以两次不能作为单变量修复 A/B。用户确认游戏灵敏度没有改变，第二次仍抖动但稍有改善。

用户截图：ALC 死区 0%、外部阈值 1%、响应曲线 2，腰射横纵 380、ADS 横纵 240。视频编码帧率为 240 fps；不能将编码帧率当作实际游戏刷新率或独立游戏响应标定。

| 字段 | 第一会话 | 第二会话 |
| --- | ---: | ---: |
| 可解析 JSON 记录 | 8,518 | 10,669 |
| malformed / 非对象记录 | 0 / 0 | 0 / 0 |
| 独立 controller sample | 1,451 | 1,809 |
| 相同 payload 重发 | 27 | 37 |
| controller 样本跨度 | 16.647 s | 21.373 s |
| ADS / BodyLock 样本 | 81 / 330 | 122 / 346 |
| 已记录的发送失败、非有限轴、越界 | 0 | 0 |
| 实际固定 ADS 压枪 contribution | -0.20 | -0.14 |

样本按本生产者的 `sample_seq` 校验 payload 后去重、排序；没有用包裹层复用的 `tick_id` 跨表关联，也没有把视频文件名时间直接当成精确控制器时间桥。全部分卷均完整；沿用的 September screening 工具输出中“排除截断分卷”的硬编码说明不适用于这两个新会话，当前统计未排除任何分卷或修补原始数据。

第一会话 target 6、sample 1237–1359 的横向物理输入很小，辅助输出多次达到约 ±0.3；同身份片段里的反转不能归因于目标切换。第二会话 target 24、26、33 仍有同目标横向误差/输出反向筛查命中。筛查是诊断线索，不是玩法验收 oracle。

第二次使用 linear 工具曲线仍抖动，因此不能把 COD 曲线认定为唯一原因。日志里的压枪量是实际生效贡献；例如输入 0.10 时，既有 `feedback_min_amount=0.14` 会钳成 0.14。本次代码修改没有改用户当前配置。

## 根因与所有权

`observe_delivered_output()` 原先只保存实际最终输出经 transfer / forward curve 后的命令。这个命令同时供响应估计器和 BodyLock 运动 observer 使用。

响应标定需要知道实际发送的完整命令，这是正确的。但 BodyLock 的持续跟随需求属于 pre-recoil owner；在固定压枪已抵消武器上跳的情况下，把最终输出里的压枪量再次作为持续运动证据，会生成额外的 pre-recoil 拉动，后续 composition 又叠加一次压枪。非线性向量曲线还会使纵向压枪改变横向命令的归一化比例。

修复在同一发送回执、同一时间区间内保存两种不同语义的命令：

- 完整最终命令继续供响应学习使用，保持原来的拒绝条件和证据边界。
- 目标运动 observer 使用去掉当前已知、实际 composition 压枪贡献后的跟随命令。先在 stick 坐标扣除，再通过非线性曲线；不能在曲线之后只扣一个 Y 标量。

这个变化没有暂停开火跟随、添加延时或改变目标选择/权限。失败发送和重连仍会打断旧命令区间，原始手动输入仍走同一最终输出 owner。

## 冻结回归和结果

新增 `native/controller_native/bodylock_recoil_feedback_incident_tests.cpp`，直接运行生产 `NativeGamepadController` 和 composition 路径。

主 fixture：1 kHz controller、5 ms 新鲜观测及确定性短缺口、同一 target/generation、右摇杆和左摇杆均为零、先 ADS→BodyLock、200 ms 开始 RT 开火、固定压枪 0.20。plant 是明确标注的**假设线性、已抵消后坐力 plant**，不是 Apex 精确回放。对照同时移除压枪贡献和 plant 武器上跳。

提前冻结 oracle：静止目标额外 pre-recoil Y ≤0.02，误差 ≤1 px；trigger 要求开火期间真实进入 BodyLock、运动 observer 持续有效，并保持目标身份及 LT 释放后的手动透传。

| 主 fixture | 旧实现 RED | 修复 GREEN |
| --- | ---: | ---: |
| 开火 BodyLock tick | 1,000 | 1,000 |
| 有效运动估计 tick | 1,000 | 1,000 |
| 最大额外 pre-recoil Y | 0.157530 | 0 |
| 最大静止目标误差 | 3.21757 px | 0 px |
| 无压枪对照额外输出/误差 | 0 / 0 | 0 / 0 |
| 目标身份 / 释放透传 | 保持 | 保持 |

两组固定独立 seed（20261001、8675309），每组 256 种压枪量、横纵目标速度、2–10 ms 观测 cadence，分别执行 600 ms 和 3,000 ms，共 1,024 个场景。逐案例冻结允许的最大误差增加为 0.1 px：**0 个新增退化**；最大跟踪误差从 5.70015 px 降至 1.59771 px。另测实际 DS4 字节解码回执，以及 linear / COD 两种曲线：最大静止误差 0.0511817 px、额外 pre-recoil Y 0.0175301，移动目标误差 1.05036 px，均在冻结预算内。

六组 Base 和四组 Feature 产品门禁通过，覆盖 ADS、BodyLock、身份、生命周期、手动权限、recoil、AutoFire 和输出诊断。16 包独立 seed 的短/长 AimLab 配对通过既有保护指标比较，`compare_sustained_aimlab.ps1` 经矩阵比较器逐包运行，新增受保护退化为零。AimLab 状态仍是 **EXPLORATORY / MISSING COVERAGE**；假设 plant 无法证明此次 Apex 全部横向振荡已消失，也不代替 matched native/live A/B。

## 本地复现和程序

证据、程序备份、RED/GREEN 原生报告、hash manifest 和比较结果在 `runs/apex-jitter-20261001/`，不进入普通源代码提交。会话审计状态为 `INSUFFICIENT_EVIDENCE`：源文件和程序身份一致，但没有独立游戏响应、实际刷新率和精确视频时间桥。

```powershell
native/build/Release/cod_native_base_tests.exe --case incident_recoil_not_target_motion --artifacts runs/apex-jitter-20261001/recheck
native/build/Release/cod_native_base_tests.exe --case randomized_firing_tracking --artifacts runs/apex-jitter-20261001/recheck-random
native/build/Release/cod_native_base_tests.exe --case delivered_recoil_coordinates --artifacts runs/apex-jitter-20261001/recheck-delivery
python python/tools/verify/compare_oscillation_matrix.py --baseline runs/apex-jitter-20261001/matrix-baseline --candidate runs/apex-jitter-20261001/matrix-candidate
python .agents/skills/incident-to-regression/scripts/regression_contract.py check --manifest runs/apex-jitter-20261001/regression-manifest.json --stage complete --output runs/apex-jitter-20261001/recheck-contract.json
```

候选正式 runtime 编译在 `runs/apex-jitter-20261001/runtime-build/Release/cod_native_runtime.exe`，SHA-256：`a17cf1e267578cd8fc324eebb7ee161571c0a5b563d658dc0b873e6e03848971`。用户确认从原 GUI 停止后，已核对进程退出和旧程序备份，再替换 `native/build/Release/cod_native_runtime.exe`。安装后的 hash 和 Apex 有效配置读取均通过；旧程序保留在证据目录，可用于回退。

后续实测使用同一武器、ALC 和配置，先比较“静止目标持续开火”和“移动目标持续开火”。若新日志仍有同目标横向振荡，应将该剩余现象单独冻结为 RED，继续检查响应/延迟失配与独立横向枪械扰动，不能用本次纵向回归通过替代横向实战结论。
