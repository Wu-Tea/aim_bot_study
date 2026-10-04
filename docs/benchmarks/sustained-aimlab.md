# Sustained AimLab 数字模拟

此入口驱动生产 NativeGamepadController，通过合成目标轨迹、响应曲线、slowdown、延迟及人工输入进行数字闭环模拟。2026-10-04 已移除旧 SHA256 admission、日志 profile 审计流水线和基线验收比较器；模拟计算、场景及指标保留。

## 构建与运行

在项目根目录用 CMake 构建 `cod_native_sustained_aimlab_benchmark`。构建工具路径沿用当前 Visual Studio 环境；源码入口在 `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`。

```powershell
& 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build native/build --config Release --target cod_native_sustained_aimlab_benchmark
& ./native/build/Release/cod_native_sustained_aimlab_benchmark.exe --config config.native.example.toml --duration-ms 6000 --seed 20261004 --profile pure --cohort both --target-slot-ms 1575 --output runs/simulation/aimlab.json
```

使用 `--help` 查看当前参数。`--profile` 可选择 pure、mixed、scripted、wrong-then-correct 等人工输入；`--cohort` 区分 ADS 获取和 BodyLock 跟随。`--vision-hz`、`--vision-result-delay-ms`、`--control-response-delay-ms` 和 sensitivity/slowdown 参数描述模拟条件。

默认固定靶位为 1575 ms，靶位后 gap 为 50 ms。失败消耗完整靶位；BodyLock 未进入时没有获取分。这是模拟场景与计分语义，当前没有将其转换成候选发布门槛。

## 批量场景

- `scripts/verify/run_target_hold_matrix.ps1`：四种人工输入、短长时长及 slowdown/delay 情况；保留 `-IndependentValidation`，不再接受 `-BaselineDir`。
- `scripts/verify/run_bodylock_pov_fire_matrix.ps1`：POV/firing/occlusion 数字场景，不再接受 `-BaselineDir`。
- `python/tools/verify/run_oscillation_matrix.py`：并行运行数值场景，保存执行状态和 JSON，不生成 SHA 身份或验收 verdict。
- `native/controller_native/oscillation_scan.cpp` 和 `state_machine_replay_tests.cpp`：数字随机扫描与状态机 replay。

`--profile runtime` 仍可直接输入显式 observation/manual/target patterns 和时间参数；旧 profile 提取/审计 runner 已删除。输入被当作调用者提供的模拟条件，不再有 SHA 身份证明。此路径不是精确日志回放。

## 结果含义

JSON 保留获取、跟踪、平滑、误差及逐目标数值指标。模拟程序正常退出说明执行成功；合法参数、有限输出和模拟器内部一致性断言仍可能使命令失败。总分和执行成功不再产生旧 BENCHMARK-ELIGIBLE、release PASS 或保护指标验收结论。

plant 属于合成假设；超过 1 kHz 的控制频率仅是数值反事实，不改变生产运行时的采样/输出权限。生产手动权威、目标所有权和整条 controller lockstep 逻辑未在本次清理中重写。
