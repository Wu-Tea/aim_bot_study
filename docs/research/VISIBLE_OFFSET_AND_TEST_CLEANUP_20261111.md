# 可见目标残余偏差调查与测试精简（2026-11-11）

用户现象：person 与 Q 可见，准星偏着却几乎不再纠正；小幅手动修正被助手的“手动意图起点／满权重”设置抑制。已确认这里不是游戏摇杆死区。

## 调查边界与结果

`IntentFilter` 以每轴输入幅度生成连续手动权限；低于起点权重为零。`AssistControlStateMachine` 在 AI 持有轴时使用这一权限，无目标或退出辅助的原始直通是另一条路径。这个已确认的规则解释了为什么小幅手动修正不能作为恢复判据，但不能解释 AI 为何没有纠到位。本次不改意图阈值、手动退出规则或生产控制算法。

新增 `BaseBodyLock.visible_offset_recovers_under_low_manual_intent`：完整原生控制链先居中，200 ms 时产生 5 px 偏差，250～600 ms 施加 2%～14% 的手动输入；手动意图配置为 15%～30%。四个确定性种子共 96 场，覆盖 Linear/Dynamic、X/Y、1.2/4 秒、4/8/16 ms 观测间隔、0/9/15 ms 输出延迟、1500/3400/5000 px/s 标定和 40～140 px 目标高度。逐场检查有限输出、后段控制权限、700 ms 后偏差小于 1 px，以及退出 ADS 后原始输入直通。

结果：96 场通过，后段最大残差 0.0739939 px。模型为静止目标、精确曲线与标定、无图像噪声、无后坐力；输入从 Vision 快照开始，不重放实际检测和选择器，也没有游戏内辅助瞄准。**没有复现用户实战中的持续停住，尚未确立根因，不能称为修复。**

临时诊断还检查了游戏输出死区和标定偏差。其结果仅用于区分假设，没有把临时无断言探针留进日常测试。可见目标超出跟随范围后权限归零是另一种已知现象，不能据此认定本次根因。

当次运行的 `runtime.stdout.log` 明确为 `telemetry=off`，只有启动信息；没有对应事件的逐帧目标、控制权限和实际输出，当前也没有运行中的原生进程可供检查。后续复现应对齐同一事件的画面、已选目标／瞄点、观测年龄、控制模式／权限、手动输入、位置需求、运动需求、制动预留和最终输出，以定位最早出现不一致的环节。不要先用放大力度、取消死区或增加延时掩盖现象。

## 精简对应关系

| 删除／简化 | 保留的检查 |
| --- | --- |
| `feedback_distance_delay_noise_sweep`：旧参数扫描只断言有限输出与联合限幅，收敛统计仅写报表 | `force_envelope_remains_bounded`、`independent_limits_and_response_randomized`；后者保留随机输入和独立参数不变量 |
| `point_boundary_closed_loop_diagnostic` 及专用头文件：旧求解路径的探索报表，没有收敛判定 | `delayed_stationary_target_converges_without_limit_cycle`、`range_response_delayed_plant_sweep`、完整控制链短／长场景 |
| 高频抖动回归的独立命令行包装与大型 JSON 序列化 | 七种原场景、捕获与控制频率、8 px 精度、250 ms 收敛、振荡特征、身份稳定和固定标定检查全部保留；改用普通测试注册及七行 CSV |
| 只供该包装使用的注册适配器、输出路径解析，以及未使用的 standalone 入口 | 共用原生测试入口；缺失的直接头文件依赖补到实际使用方 |

没有降低生产频率、减少检测、放宽剩余测试阈值或删除用户数据。修改只影响测试／文档，不改变运行程序的逐帧开销。

复现命令（仓库根目录）：

```powershell
native/build/Release/cod_native_base_tests.exe --case visible_offset_recovers_under_low_manual_intent --artifacts output/visible-offset-recovery
native/build/Release/cod_native_base_tests.exe --case incident_bodylock_high_frequency --artifacts output/interaction-test-cleanup/high-frequency
```

## 最终验证

Release 构建通过；原生基础测试 528/528 通过，功能测试 101/101 通过。基础用例从 529 项变为 528 项（删除两项重复诊断，新增一项可见偏差回归，内部含 96 场）。测试源代码净减少 383 行；未修改生产代码。高频回归七行均通过原有判定，固定错误标定的对照仍只要求标定不被运行过程改写，不把该对照误报为收敛通过。

结果在 `output/interaction-test-cleanup-base.log`、`output/interaction-test-cleanup-functional.log`，逐场结果在 `output/interaction-test-cleanup/base/visible-offset-recovery.csv` 和 `bodylock-high-frequency.csv`。这些文件属于本地生成产物。
