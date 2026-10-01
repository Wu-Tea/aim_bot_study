# Apex ADS 近中心抖动：物理位移测量修正

部署更新（2026-10-01 13:40，Asia/Hong_Kong）：用户明确要求编译替换；确认旧 runtime 已停止后，增量编译成功，备份旧 exe 并替换了原 GUI 路径，安装后 SHA-256 与候选 `e72033a3598d8e590841557c9661a4a53e75f25726877d8bd8c07b99e31f89f1` 一致。配置原始哈希保持 `ed1ba602df92ff691b979ef2962408a666a861121cda23a2071c1070ebcc2df1`，未自动启动手柄控制。部署记录及可回退备份位于 `runs/signal-strategy-20261001/deployment-20261001T054040878Z/`。**当前状态 DEPLOYED / LIVE ACCEPTANCE PENDING**；下文“尚未替换”是部署前取证时点的历史状态。

版本核对：部署产物来自本次提交前已验证的工作树，嵌入的 Git identity 可能仍为 `79a053b`。应以以上完整 executable SHA-256 识别实际运行版本；后续源码提交没有重新编译或替换该部署产物。

状态：**OFFLINE CAUSAL FIX VERIFIED / LIVE ACCEPTANCE PENDING**。新程序已编译，但记录本报告时原 GUI 的旧 runtime 仍在运行，尚未替换。离线回归已证明并修复一条产生虚假运动输入的路径；实际 Apex 抖动是否全部消失，仍须新版本日志与用户确认。

## 新日志与因果路径

会话 `20261001T051300Z_24256_1` 正常收尾，runtime SHA-256 为 `a17cf1e267578cd8fc324eebb7ee161571c0a5b563d658dc0b873e6e03848971`，schema 18。这是此前局部 recoil ownership 修正后的程序，没有包含本次 pixel 测量修正。

同一目标 135、ADS epoch 18、不射击的观测：

| source frame | 框内解剖瞄准点的水平误差 | 独立 pixel anchor 的水平位置 |
| --- | ---: | ---: |
| 5516 | 10.5 px | 328.917 px |
| 5517 | 0.0833435 px | 329.917 px |
| 5518 | -1.08331 px | 329.917 px |

5516→5517 相隔 5.5036 ms，框内瞄准点移动 -10.4166565 px，而追踪到的人体像素只移动 +1 px。人体框宽度从 49.6667 变为 66.5 px；身份没有切换。

controller tick 53302 中，水平误差仅 -1.08331 px，位置修正 -0.0229551，运动修正却达到 -0.600755，最终 AI 请求 -0.499456、整形输出 -0.383603。物理手柄 x 为 +0.0274361，存在低幅漂移，不能称为纯 AI / 手动完全归零。fresh trace tick 53296 的同一请求先被旧 shaper 归零，再逐步恢复：输出层收到的请求本身已经异常。

旧 `NativeGamepadController` 对 `source_error_px` 求导。该瞄准点随检测框和姿态变化；把其变化全部视为物理位移，会把检测几何变化传给 response learning 与 BodyLock target motion observer。后者产生过大的运动前馈，造成近中心异常 AI 请求；输出整形是后续传播环节。

## 共同测量生产者处的修正

实现位于 `native/controller_native/native_gamepad_controller.cpp/.h`：

- 对同一目标、同一响应采样区间，两端均有可信人体 pixel correspondence 时，用其位移计算 observed error rate，同时供两个 response estimator 和 BodyLock motion observer 使用。
- 沿用已有 tracked-anchor 质量边界 score ≥ 0.45，并要求有限坐标、当前人体框内支持。score 0.35 的新模板只建立位置，不算上一特征的连续追踪。
- 任一端缺少可信 correspondence 时，用两端 semantic source point 的旧测量；禁止 pixel 与 semantic 两个坐标域交叉相减。两套位置随原有 response anchor 一同推进、重置，不新增时间调度。
- 解剖瞄准点、选择/身份、手动即时透传及 15%–30% 意图权重职责保留；此前 recoil tracking ledger 修正保留。

此前保留的 vision anchor 人体框支持检查也包含在待测试程序中，这是单独的测量硬化，不改变瞄准点发布。

用户确认的“旧方向 AI 清零、新方向从零渐增”仍为独立 OPEN 需求。固定 15.625 ms 的实验假设造成受保护退步，已经撤回；本次没有重新部署该启动曲线。见 [信号启动策略调查](GAMEPAD_AI_SIGNAL_ONSET_INVESTIGATION_20261001.md)。

## 冻结夹具与 RED→GREEN

夹具通过完整 `NativeGamepadController` 闭环运行。物理响应 1000、效应延迟 0 是明确的合成假设，不是 Apex 重放或游戏标定值。主 seeds 20261001、8675309，独立验证 seeds 10012026、3141592653；两轴、Linear/COD dynamic 两曲线、目标速度 0/±40/±100、手动 0/±0.03、600/2400 ms、5/8/16 ms 观测；固定缺帧区间 [500,510) ms。

| 门禁 | 修正前 | 修正后 |
| --- | --- | --- |
| 主矩阵 1024 场景 | 511/512 几何扰动失败；512/512 无扰动对照通过 | 1024/1024 通过 |
| 独立 seeds、学习开启 256 场景 | 63/64 有效 correspondence 扰动失败；192/192 新模板、框外、非有限及缺失对照通过 | 256/256 通过 |
| 主矩阵最大虚假运动误差，归一化摇杆等价单位 | 0.481248 | 0.00000603104；冻结上限 0.002 |
| 触发、稳定身份及有限单位输出 | 0 无效夹具 | 0 无效夹具 |
| 普通 product CTest suites | — | 10/10 通过，包含本次 1280 场景 |
| COD/default 与 Apex 配置受保护比较 | matched baseline | 共 32 对，0 受保护退步 |

1280 场景已注册普通 BaseBodyLock suite。独立 runner `cod_native_bodylock_motion_anchor_incident` 复用同一夹具。32 对矩阵仍为 **EXPLORATORY / MISSING COVERAGE**；固定 target slot 1575 ms + gap 50 ms，matched covariates，不以总分抵消保护项。

```powershell
cmake --build native/build --config Release --target cod_native_bodylock_motion_anchor_incident cod_native_base_tests cod_native_functional_tests
native/build/Release/cod_native_bodylock_motion_anchor_incident.exe --artifacts <new-directory>
ctest --test-dir native/build -C Release -L "^(base|functional)$" --output-on-failure
```

提交前复核：上述 10 个 product suites 全部通过，GUI unittest 22/22 通过，离线证据包检查 PASS / 0 issues。当前 `native/build` 缓存另启用了额外测试注册；无标签的全量 CTest 实际选择 50 项，42 项通过、8 个独立 benchmark test exe 未编译而 Not Run，因此不能报告 50/50。上述标签限定的是本次要求的产品门禁；32 对受保护矩阵是此前独立执行的记录，不能以未执行的额外单测冒充覆盖。

证据归档在 `runs/signal-strategy-20261001/motion-evidence.json`、`motion-regression-manifest.json`、`motion-regression-check.json`，包括旧 runner、源码、逐场景输出、冻结配置及比较哈希。regression contract complete 检查 PASS / 0 issues 仅表示离线证据包完整，不表示实战验收。

## 运行身份与剩余验收

候选 `runs/apex-jitter-20261001/runtime-build/Release/cod_native_runtime.exe` SHA-256 为 `e72033a3598d8e590841557c9661a4a53e75f25726877d8bd8c07b99e31f89f1`。原 GUI 使用 `native/build/Release/cod_native_runtime.exe`；记录此报告时 PID 4276 仍占用旧 exe，需要 GUI 正常停止并收尾后按已有授权备份、替换、校验哈希。部署记录另写，不改写此前未部署的历史记录。

新日志有 7214 条 controller sample，6398 唯一 ticks，与 ADS trace 精确同 tick 对齐只有 1288 条；没有本次对应的新录像。实际游戏刷新率与完整硬件配置未知，telemetry audit 仍为 **INSUFFICIENT_EVIDENCE**，上述局部信号证据不是完整 live A/B。

已核对 session config：备份 `runs/desktop/config-backups/20261001-131434-6d7f1452.toml` 原始 SHA `c02495236fedfd90bca607efa5390f1efe6ac9491dc3012c9c6f2bbc502950c6`；`SHA256(raw_bytes + NUL + "profile=;auto_fire=RB;capture_fps=200")` 与 session 的 `a3c2c3a360f4d257ebe13ffad15d15bf1fa30fca8f5f3664573cf0e5690f1f11` 一致。实际 fire 输出是 RB，分组使用 final fire 状态，不能用 RT 判断。

新程序需保持原灵敏度，开启控制日志，验证近中心 ADS 不射击、ADS 射击及正常手动/目标移动，提供新会话与实际感受。核对 motion estimate、AI request、shaped output 和实际视角后才能验收；若仍抖动，继续按新日志定位。score 与人体框支持不证明完美特征身份或分割；无可信 correspondence 时仍使用旧 semantic 测量。遮挡、跨人体重叠和 live acquisition/handover 仍需验证。

建议将本次测量职责与 OPEN 信号策略同步至 `.agent-context/`；本轮未自动改写项目记忆。
