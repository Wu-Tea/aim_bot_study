# 手柄过冲与摆动：链路扫描结果（2026-09-26）

**状态：已建立两组可复现 RED；17 组候选/消融均为 FAILED CONSTRAINTS。没有合格的生产修复，本次优化目标尚未完成。** 对真实游戏 AA 的解释仍属 EXPLORATORY / MISSING COVERAGE。已撤回全部候选生产修改，仅留下诊断工具、失败用例、结果和用户边界。

## 用户边界

保持原生手柄 0 输入死区；25% 是 AI 意图参考，允许连续交接，不要求 25.1% 立刻夺回全部手动方向。优先修改算法关系，不堆叠补丁门；不得牺牲索敌/身份/生命周期/跟踪来换取少摆动。用户的这一澄清已记录到根 AGENTS.md。未修改现有非回归预算。

## 已证实的问题与边界

1. **最终仲裁有阈值突跳。** 今日日志同一目标 1988、sample_seq 373697→373698，4.0383 ms 内 raw X 仅变化 0.007843，AI 提案变化 0.007781，最终 X 却从 -0.215855 跳到 +0.616020，幅度 0.831875。输入穿过 25% 后 filtered input 清零、D correction 清除，最终仲裁从人工残余直接切到 AI。AI dynamics 在仲裁前，无法约束仲裁之后制造的跳变。局部测试调用真实 IntentFilter 和 AssistControlStateMachine，72 个双轴/方向/新鲜度/权威组合，最坏突跳 1.146438；288 个无目标/明确退出反事实仍精确透传。
2. **完整闭环在响应/延迟失配时能自行振荡。** 2,268 个独立随机案例中，seed 20260926/case 768 在静止目标、检测噪声 0、左摇杆 0、无 AA 作用时出现 108 次反复穿越中心。其右摇杆为 0→-0.0117798 的阈值内固定漂移，不能称为 M=0。固定 prior=650、真实模拟响应=918.611084、控制器假定延迟=9 ms、真实模拟延迟=20 ms、Vision 间隔=16 ms、结果年龄=8 ms。开火使响应学习沿现有歧义规则停止。推断传播链为：未准确抵消自身相机运动 → 总运动估计出现残差 → BodyLock 持续补偿 → 延迟反馈继续纠偏。反事实能证明这组参数失配足以失稳，不能把它当成真实 Warzone 的标定值。
3. **observer 历史量还有坐标一致性缺陷。** 历史归一化 motion 在 response 更新后按新 response 转回像素，会凭空改变物理运动。独立 unit RED 中真实 +100/-60 px/s 不变，估计误差达到 60 px/s；重标定后约 0.000023 px/s。但该修正扩大到完整闭环后未通过受保护指标，已撤回，证据和 patch 保留。

第一处可以解释人手交接尖峰，第二处说明只修 25% 交接不能覆盖所有过冲；不能据此断言所有游戏摆动都来自某一个环节。游戏 camera/敌人运动、实际 AA、硬件报告噪声缺少独立观测。DS4 8 位量化被明确放在模拟 plant 输入端；未引入抑制原生细小输入的新死区。

## 固定静止目标反事实

以下为模拟结果；每组持续 10 秒，四方向各自初始误差约 ±39.48 px。穿越指标只计 acquisition 后且 200 ms 之后、越过 ±3 px 的方向改变。

| 方向 | 原始失配穿越次数 | 原始失配过冲 px | 只匹配延迟：穿越 | 只匹配响应先验：穿越 |
|---|---:|---:|---:|---:|
| X 负向 | 108 | 31.905 | 0 | 0 |
| X 正向 | 110 | 32.044 | 0 | 0 |
| Y 负向 | 118 | 16.651 | 0 | 0 |
| Y 正向 | 83 | 16.694 | 0 | 0 |

每组都实际进入超过 8,000 tick 的 BodyLock 和有效 observer，8 个单因素反事实都通过同一个 oracle：初次到位 ≤250 ms、穿越 ≤4、过冲 ≤8 px。12 个释放 LT 控制都原样透传。此 oracle 是新发现后的回归要求，不是游戏测得的容忍阈值。原始 4 组全部 RED；不是靠无目标/未进入 observer 才通过。单独取消开火只把 X 负向幅度减到约 6.657 px，仍有 94 次穿越，不能算修复。

![静止目标：原始失配与单因素反事实](../../runs/oscillation_optimization_20260926/stationary-counterfactuals.png)

## 大量短长案例与候选筛选

- 随机 plant 共 2,268 个不同案例（324 + 972 + 972），600/1,800/10,000 ms；轴向、目标运动/停止/换向、手动输入、开火、左摇杆、噪声、减速、延迟独立抽样。随机案例是范围扫描，移动目标/人工主动摇杆的中心穿越不能全部算“振荡故障”。
- 每个候选的 Sustained 矩阵为 16 个参数包、64 条序列、864 个固定目标机会，10 s 和 60 s，pure/mixed/scripted/wrong-then-correct，ADS 与 BodyLock 两 cohort；使用相同配置、种子、Vision/plant、1575 ms 靶位与 50 ms gap。
- 每个包都执行原 `compare_sustained_aimlab.ps1`。下表合计只展示取舍，**不据合计排名，也不抵消逐场景退化**。真实左摇杆/recoil/cue/identity 等还由场景产品测试保护，Sustained 合计不代替这些门禁。

| 修改/实验 | 获取目标 | 跟踪积分（诊断用） | 振荡 episodes（合计） | 失败参数包 |
|---|---:|---:|---:|---:|
| 原始基线 | 748/864 | 303216 | 260 | — |
| 去掉 D 对连续交接权重的硬覆盖 | 749/864 | 304544 | 265 | 11/16 |
| 增加相对误差速度阻尼 | 744/864 | 307243 | 330 | 16/16 |
| 取消 observer slew | 745/864 | 308877 | 447 | 16/16 |
| 停用总运动 observer（诊断消融） | 749/864 | 175278 | 33 | 16/16 |
| 每 tick 用实际最终输出同步 shaper | 742/864 | 284545 | 318 | 14/16 |
| 以 25% 为中心的连续权重 | 745/864 | 300053 | 252 | 12/16 |
| observer 响应坐标重标定 | 744/864 | 304236 | 266 | 16/16 |
| 仅在修正释放时同步 shaper | 747/864 | 303032 | 322 | 12/16 |
| 释放权重二次曲线 | 749/864 | 305126 | 261 | 12/16 |
| 释放权重四次曲线 | 747/864 | 303969 | 256 | 11/16 |
| 释放权重八次曲线 | 748/864 | 303981 | 258 | 11/16 |
| 释放权重十六次曲线 | 748/864 | 304311 | 259 | 11/16 |
| observer 10 ms EMA | 742/864 | 299275 | 227 | 16/16 |
| observer 20 ms EMA | 747/864 | 293941 | 150 | 16/16 |
| observer 40 ms EMA | 745/864 | 285196 | 105 | 16/16 |
| shaper 制动 48→80/s | 747/864 | 303978 | 253 | 16/16 |
| 允许反向运动补偿参与位置制动 | 744/864 | 301350 | 243 | 16/16 |

减少振荡最多的消融也严重损失跟踪，不能发布。连续交接的较好合计同样存在具体退化，例如十六次释放曲线在 mixed_standard、seed 424242、BodyLock 中把 stale_output_after_stop_events 从 1 增至 2，其他保护指标也退化。候选 6 的居中交接把获取从 748 降到 745。因而没有挑一个“看起来更稳”的版本留在生产。

## 覆盖与复现

- 全链路主路径已检查：物理/DS4映射 → intent/D → selector/ADS/lifecycle → response estimator/command ledger → target-motion observer → position/motion solver → dynamics → 最终仲裁 → AutoFire/recoil/输出。
- 现有 BaseContracts、BaseVisionSelection、BaseRuntimeFreshness、BaseAds、BaseBodyLock、BaseEndToEnd 及 4 个 Feature suite 均通过（10/10）。这些旧测试通过不代表新 RED 被修复。
- 恢复后 324 个闭环案例和 72 个边界数值与冻结 baseline 逐项相同；恢复后 16 包比较无退化。配置及游戏启动文件未改；没有更新运行时 exe 或发布候选。
- 边界 RED：[regression-manifest.json](oscillation-20260926/regression-manifest.json)。静止失配 RED：[stationary-regression-manifest.json](oscillation-20260926/stationary-regression-manifest.json)。两份 manifest 都通过 skill 的 `--stage red` 检查；`green_proof` 均为 null。
- 已看过的 seed 20260926/8675309 已转为开发回归，不能继续称为未见验证集。后续独立种子在 [next-validation.json](oscillation-20260926/next-validation.json) 冻结，尚未运行候选。

```powershell
# 先构建 native/vision_native/build 下的 cod_native_oscillation_scan (Release)。
# 使用未占用的输出文件；RED 预期退出码 1，fixture/执行错误为 2。
& native/vision_native/build/Release/cod_native_oscillation_scan.exe `
  --config runs/oscillation_optimization_20260926/config.toml `
  --output runs/oscillation_optimization_20260926/repro-new.json `
  --stationary-incident 1

& native/vision_native/build/Release/cod_native_oscillation_scan.exe `
  --config runs/oscillation_optimization_20260926/config.toml `
  --output runs/oscillation_optimization_20260926/sweep-new.json `
  --seed 1337 --cases 324
```

结果、已保存的 candidate patch/冻结 executable、逐包失败指标及 SHA-256 清单在 `runs/oscillation_optimization_20260926/`（本地忽略目录）。原始大日志未更改，也不进入提交。

下一项需要解决的是**响应与时间对齐的不确定性如何进入同一个跟随解算**，使自身相机运动不会以错误的总运动补偿反馈回来；连续人工交接也应在最终仲裁层处理。只改启动 prior、统一减小 gain、去掉 observer、加滤波或加强制动，本轮证据均不足以满足既定边界。本报告不是修复完成或实战验收声明。
