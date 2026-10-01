# AI 变向清零与启动渐增：OPEN / 未解决实战抖动

后续：新会话 `20261001T051300Z_24256_1` 发现并复现 semantic 瞄准点变化污染物理运动测量的路径，定点修正已通过离线门禁，实战验收待完成。最新状态见 [物理位移测量修正](APEX_PIXEL_MOTION_MEASUREMENT_FIX_20261001.md)；本文保留前一实验阶段的历史记录。

用户明确选择：撤销旧方向 AI 输出，新方向从零渐增；真实物理输入保持即时响应。这个边界不包含模拟物理摇杆归中、不包含截断玩家输入，也不修改 15%–30% 手动意图权重或游戏灵敏度。

本轮证实了软件旧方向拖尾，实现并验证了一个输入策略候选，但它没有通过闭环性能检查。候选已从生产源码撤回，未替换 GUI 使用的运行程序。完整抖动原因与修复验收仍然 OPEN。此前调查见 [Apex 启动响应调查](APEX_ADS_STARTUP_INVESTIGATION_20261001.md)。

## 最早的信号所有权问题

`AimDynamicsShaper` 是 AI 时间整形的唯一所有者。它持有 AI 输出，不持有最终融合后的物理手动输入。旧实现遇到反向请求时，先以 decay slew 把旧方向逐步减至零，再启动新方向。因此 producer 已撤销方向，但消费者仍获得旧方向 AI 命令。之后 sole arbiter 与 composer 才融合物理手动输入及独立 recoil。

这是相对于用户新确认策略的所有权违例；此前的“先缓释再反向”是旧设计行为。它证明了一条软件拖尾路径，不能单独证明整个实战抖动都由该路径造成。

两段旧日志按 controller tick 去重后，同 tick 的 request/shaped sign 检查发现：

| Session | 轴 | 非零请求采样 | 请求/整形输出方向相反 | 其中控制误差 ≤10 px |
|---|---|---:|---:|---:|
| 20261001T034319Z_26276_1 | X | 384 | 13 | 12 |
| 20261001T034319Z_26276_1 | Y | 378 | 17 | 15 |
| 20261001T035108Z_28632_1 | X | 442 | 13 | 10 |
| 20261001T035108Z_28632_1 | Y | 441 | 2 | 2 |

例如第二会话 tick 6162：physical X=0.0431227、requested AI X=+0.0570596、shaped AI X=−0.0344772，控制误差 −1.86746 px。该判断只需同 tick 字段；原日志稀疏采样，不能从这些数量反推持续时间或宣称逐 tick 精确回放。证据为 `runs/signal-strategy-20261001/log-signal-ownership.json`，包含原日志哈希与全部相反方向采样。

## 冻结的信号实验

先执行原生产 shaper/intent 的开放信号探针，无相机或合成目标。80 组固定幅度 × 两轴 × ADS/BodyLock × 四种 dt。1 ms 周期下：

| 请求幅度 | 旧版首次启动比例 | 旧版反向后旧方向持续 | 旧版新方向达到 90% | 候选首次启动比例 | 候选旧方向持续 | 候选新方向达到 90% |
|---:|---:|---:|---:|---:|---:|---:|
| 0.01 | 100% | 0 ms | 2 ms | 6.4% | 0 ms | 15 ms |
| 0.10 | 64% | 2 ms | 5 ms | 6.4% | 0 ms | 15 ms |
| 0.30 | 21.3% | 6 ms | 12 ms | 6.4% | 0 ms | 15 ms |
| 0.80 | 8% | 16 ms | 29 ms | 6.4% | 0 ms | 15 ms |

候选在同 tick 清除旧方向；只重启反向轴的 onset progress，另一个轴继续。进度按经过时间增长，请求幅度每帧变化不重启它。目标身份改变重启进度；同身份 ADS→BodyLock 不重启继续轴，并保留原模式交接幅度约束。只有 continuation 证据时可以撤销旧工作，不能授权新方向或继续放大启动。鼠标适配器保留其校准时间策略。

**15.625 ms 全进度长度是实验假设**：复用旧默认 rise rate 64/s，把它解释为归一化进度；用户没有指定这个长度，也没有独立游戏测量证明它正确。它延长了原本当帧达到请求的小信号启动，表中小幅反向的 90% 响应明显变慢。

原绝对每 tick 上限 0.08 仍保护增量。最初完成时间 oracle 只考虑 1/64 s，在 2 ms、高幅度情况下漏算这个上限，误报 3/512。修正为同时等待两个既有约束允许的完成时间；未更改方向清零、peer 连续性、启动比例、seed、请求或闭环比较规则。修正后重新建立原实现 RED；初始与修正报告均保留。

512 个随机短/长信号案例固定 seeds 20261001、8675309；轴与控制模式独立取值，dt 0.25–2 ms。候选 v3 通过全部案例：2560 次反向、18497 个完成后检查，旧方向最大残留 0，peer 最大变化 0。另有 native 小幅 ADS 启动及零死区手动反向检查，以及交接、身份、continuation、authority 撤销检查。

## 闭环拒绝证据

遵循既有 AimLab contract，不修改保护指标、不靠总分推荐。固定每目标 1575 ms slot + 50 ms gap；四种输入 profile × standard/slow/delayed/long，共 16 对，holdout seeds 20260926、8675309，短 10 s / 长 60 s，两种 cohort。所有配对使用相同输入与配置，调用现有 `compare_sustained_aimlab.ps1`。

| 实验 | 信号/产品检查 | 16 对受保护比较 | 处理 |
|---|---|---|---|
| v1：清零 + 归一化 onset | native 小幅 ADS 通过；3 个完成 oracle 误报 | 16/16 有退步 | 拒绝 |
| v2：补回交接约束、continuation 持有 | 修正后 512/512；10 组 product CTest 通过 | 16/16 有退步 | 拒绝 |
| v3：交接只限制旧幅度，不能提前改写方向 | 512/512；包含新检查的 base 352/352 | 16/16 有退步 | 拒绝 |
| 清零单因素：保留旧 rise、不乘 onset | 用于隔离影响，不满足弱信号渐增要求 | 16/16 有退步 | 拒绝 |
| v3 + 当前 Apex 生效配置 | 原有场景/规则不变，独立冻结 Apex 配置 | 16/16 有退步 | 拒绝 |

前四项使用之前冻结的 global/COD 配置，保护相邻 COD 路径，不是 Apex 精确回放。最后一项将当前 `config.toml` 的 Apex overlay 递归合入 global 后单独冻结，因为 benchmark loader 没有 game 参数：linear，四区 prior 都为 1000。Apex 配置 SHA-256 为 `43abe8211540ad7c15aa59fd2d98b57b988f890397a28968316ecb9dd6d41baa`，其生成源 SHA 为 `ed1ba602df92ff691b979ef2962408a666a861121cda23a2071c1070ebcc2df1`。原配置未改写。来源见 `apex-effective-provenance.json`。

退步包含捕获、跟随误差、反弹、额外纠偏事件；不是仅因为“允许清零”与旧缓释测试语义冲突。Apex pure/ADS seed 20260926，P95 error 70.8313→71.0722，max handoff rebound 10.1818→10.5500，residual kick 0→1；同 seed BodyLock acquire points 4543.7385→4459.4473。

另在既有静止响应/延迟失配 RED 中，v2 水平两方向穿越次数 111/110，清零单因素为 118/118；原基线为 92/18。新增进度与取消尾巴都未消除该闭环问题。该 plant 的 gain、delay 是固定假设，有匹配 gain/delay 的负对照；它证明敏感性，不能当作真实 Apex 标定。

这些结果不否定所有可能的启动策略，只拒绝本次候选及单因素取消方案。不得据此把 16 ms 当成游戏启动耗时，也不得宣称主抖动已修好。需要在当前版本的实战数据中区分请求层振荡、整形拖尾、视觉误差与真实游戏响应，再决定启动模型如何进入求解/响应预测。

## 当前源码与复现

生产 shaper 与 native wiring 已恢复实验前版本。新增 `cod_native_gamepad_signal_onset_incident` 是 `EXCLUDE_FROM_ALL` 的 OPEN RED 诊断，不注册到普通 product suites；它明确暴露未实现的新信号策略，不表示已验收。最终原实现 3/3 诊断失败、512/512 随机场景失败。

```powershell
cmake --build native/build --config Release --target cod_native_gamepad_signal_onset_incident
native/build/Release/cod_native_gamepad_signal_onset_incident.exe --artifacts <new-directory>
```

候选源码、含新检查的 runner、信号探针、每对输入/输出/比较及哈希保存在 `runs/signal-strategy-20261001/`。执行归档的全 base runner 需要同目录的 SDL2.dll；补齐依赖后 v3 352/352 通过。最终保留版本 10/10 product CTest 通过，`retained-final-matrix/comparison.json` 16/16 无保护退步，仍为 EXPLORATORY / MISSING COVERAGE。

原 GUI runtime SHA-256 仍为 `a17cf1e267578cd8fc324eebb7ee161571c0a5b563d658dc0b873e6e03848971`，没有部署新候选。详细新日志尚待补录：当前版本、相同灵敏度，准星接近目标时分别只按 ADS、不射击，及 ADS 射击，正常停止日志。需要 fresh native/live A/B 与用户实际抖动消失确认才能验收。

建议将用户确认的 AI/物理输入边界及失败的固定启动假设同步到 `.agent-context/`；本轮未自动改写项目记忆。
