# Vision 提交与设备调度跟进：2026-09-07

**结果：INSUFFICIENT_EVIDENCE。两个原始性能问题尚未解决，本轮没有进入生产的候选。**

用户授权继续查明 CPU 提交后 GPU 启动间隙及捕获长尾，并确认进入游戏场景。本轮完成管理员系统跟踪尝试、原始 ETL 完整性检查、一个完整提交策略 ABBA，以及一个因预算超限而中止的计时事件 ABBA。生产 Vision、Controller/Fusion、配置、模型和优先级均未改；已有 Controller 工作区改动保留。

## 要回答的问题

此前 CUDA 轨迹把较慢样本的额外间隙定位在设备执行之前：CPU 进入同步调用后，GPU 平均再过约 0.967 ms 才开始输入复制；实际复制和预处理合计约 0.020 ms。它不能解释成 GPU 完成后 CPU 未及时返回，也不能直接归因于 WDDM、某个驱动或游戏抢占。原日志约 12.391 ms 的捕获复制段停顿尚未复现。

本轮继续保持 640×512 → 480×384、同一软件缩放公式、原模型，且不提高程序优先级。GPU 预算仍为进程 PDH 最忙引擎的稳态 500 ms 采样最大值不超过 15%；不等于整卡占用或瞬时硬上限。初始化不计入此判据。

## 系统跟踪：获取了文件，但没有有效队列证据

普通工具进程没有管理员 token。用户通过 Windows UAC 授权后，采集脚本确认管理员 token 为真，游戏进程为 `cod.exe` PID 111528。独立探针不向游戏注入代码、不运行 Controller、不发送手柄输入。

| 采集 | 结果 |
|---|---|
| `schedule-20260907-203316` | 24 秒探针完成 72 帧 CSV；Nsight 未在 100 秒启动/采集/导出时限内返回报告 |
| `schedule-20260907-203816` | 改用独立 35 秒 profiler 结束条件，24 秒探针仍完成；Nsight 再次导出超时 |
| `raw-dxg-20260907-204229` | 独立 logman DxgKrnl 会话启动和停止均成功，ETL 保存；完整性不合格 |

两次 Nsight 残留均通过 PID、父 PID、完整程序路径及创建时间核实后精确清理。最终查询没有本次 profiler、探针或 `CodexVision` ETW 会话残留。没有停止其他跟踪会话，也没有改系统 ETW 配额、HAGS、驱动或功耗设置。

原始 DxgKrnl 跟踪使用已在本机枚举的 Base、Profiler、GPUScheduler、HardwareSchedulingLog、Present 关键字，64 KB buffer，16–256 buffers，单独命名会话；仅写本地文件。

### ETL 完整性结论

- 文件大小 391,905,280 字节，5980 buffers。
- 文件头 `EventsLost = 3,822,742`，`BuffersLost = 0`。
- Microsoft xperf 报告丢失 3,822,742 个事件，只能读到两条 EventTrace 元数据。
- tracerpt 只输出两条元数据，其概要中的丢失数为零，与文件头及 xperf 不一致。因此没有使用 tracerpt 的“零丢失”作为通过依据。
- 用独立离线解析库 `dissect.etl 3.14` 遍历全部 buffer：**2 条 SystemHeader，3,822,742 条 ErrorHeader，0 条可用设备队列事件**。ErrorHeader 类型为 0x0D。
- 不改写错误标记，不将残留 provider GUID、时间字段或 payload 强行解释成有效事件。
- 原始 ETL SHA-256：`ac59b8d4aadd96616375deaa05604d4c380bdd635477a35d2ff974c43d78e3d0`。

这证明本次系统跟踪数据不可用于调度归因；没有证明其具体系统原因。xperf 的磁盘/缓冲区提示是通用建议，不能仅凭提示认定磁盘太慢。Nsight 两次超时也不能直接认定与 ErrorHeader 同源。

完整性复现：`D:/env/python/python.exe artifacts/vision-live-contention-20260907/check_etl_integrity.py artifacts/vision-live-contention-20260907/raw-dxg-20260907-204229.etl`。解析库安装于实验目录 `etl-deps`，没有修改系统 Python。

## 完整对照：整条 CUDA Graph 没有带来实质收益

针对“减少提交次数可能减少等待”的假设，复用已有隔离 CombinedEngine 原型：直接从 CUDA array 做相同软件插值，把预处理、TensorRT、D2H 放入同一 Graph。它同时改变输入读取方式与提交组织，因此即使有收益，也不能单独归因于其中某一个变化。

游戏在后台提供真实负载，模型输入使用用户目录中已冻结的 160 张训练裁剪，不使用录像。相同进程按 A→B→B→A 运行；每阶段请求 30 Hz、持续 24 秒，前 4 秒和最后 1 秒排除。四阶段各有 555 帧稳态样本，实际 29.21 Hz，GPU 各有 36 个合格窗口样本。该测试测量机制差异，不是 Vision 极限 FPS，也不是完整游戏回放。

预先冻结准入条件：每个候选阶段 mean、P99 均比两个基线阶段至少低 5%，GPU 均值不回退超过 5%，每个采样点不超过 15%，模型输出完全相同。GPU 时钟可比条件为至少 95% 样本在 2775 MHz 的 ±5% 内；实际所有合格样本均为 2775 MHz。具体见 `game30-contract.json`。

| 阶段 | 调用均值 ms | 调用 P99 ms | 进程 GPU 均值 | 进程 GPU 最大值 |
|---|---:|---:|---:|---:|
| 原实现 A1 | 4.107 | 4.974 | 2.305% | 2.375% |
| 整条 Graph B1 | 4.108 | 4.957 | 2.300% | 2.417% |
| 整条 Graph B2 | 4.197 | 5.100 | 2.315% | 2.511% |
| 原实现 A2 | 4.213 | 4.974 | 2.317% | 2.761% |

两组平均后调用均值仅改善 **0.185%**，候选 B2 的 P99 还高于两次基线，收益门槛失败。资源、时钟条件通过；四阶段全部 160 个输入、每阶段 260 条检测/空结果记录逐字段完全相同。

B1 的预处理 CUDA event 区间从 A1 的 1.938 ms 降到 1.195 ms，但实际调用均值从 4.107 ms 到 4.108 ms。**不能把这个局部计时下降算作 0.74 ms 的端到端优化。** 候选不会进入主线。

此时外部进程最忙 GPU 引擎均值约 88–89%，整卡 NVML 占用约 99%。这与共享 GPU 资源争用的解释一致，但没有对应的可用 WDDM 队列/调度事件，故仍是推断；也缺少匹配的游戏 Present/帧时间，不能声称保护了游戏吞吐。

复现：`D:/env/python/python.exe artifacts/vision-combined-20260907/summarize_game_abba.py game30`。产物为 `game30-identity.json`、各阶段 CSV/GPU JSONL、`game30-verdict.json`。

## 计时事件对照：未完成，不作为否定或接受的完整证明

编译了独立 `no_timing_paired.exe`：从当前 TensorRTEngine 源码复制，只移除逐帧 CUDA event record 和 elapsed-time 读取。RGB/BGRA 两条方法共移除 12 次 record、8 次 elapsed-time 读取；本次 BGRA 热路径对应每帧 6 次 record、4 次读取。保留输入复制、软件预处理、TRT Graph、D2H、原 stream 同步。没有改变生产代码。

预先冻结同样的 ABBA、时间窗口、资源、正确性和收益门槛，见 `no-timing-game30-contract.json`。候选 GPU timing 字段的零值代表未测量，不代表计算耗时为零；评估只使用 CPU 实际调用耗时、独立 GPU 占用和检测结果。

首个基线/候选阶段：调用均值 4.178 → 4.120 ms，P99 4.935 → 5.017 ms。前半段没有出现毫秒级改善，也没有通过本轮收益条件，但不足以构成完整 ABBA 的否定证明。

第四阶段外部 GPU 占用降至约 3.17%，GPU 时钟降至 930 MHz，探针进程 GPU 采样为 **19.334%**，预算保护立即停止本次进程。对照未完成，运行条件改变，不能把此前第三阶段的均值下降归因于取消计时事件。结果记为 `INSUFFICIENT_EVIDENCE / ABORTED_RESOURCE_GATE`，不重复补跑或拼接为完整对照。

## 捕获侧与生产状态

原始 DxgKrnl 采样同时保存了完整 Vision 探针 CSV；19 秒稳态 57 次均为新帧，捕获 CPU 复制段最大 0.290 ms，仍未复现原日志 12.391 ms 停顿。没有足够证据决定应修改 Copy、Flush、ReleaseFrame 或 interop 的哪一层。

本轮新编译通过的是独立诊断 EXE，不是新的生产优化版本。没有通过准入的实现，因此没有替换用户正在测试的程序，也没有运行无关 Controller 功能回归来冒充性能验收。

## 下一步边界

1. 先恢复一个能生成有效 WDDM 事件的跟踪环境，用数秒小采样验收 `ErrorHeader=0`、事件覆盖和丢失计数，再采正式场景。当前无法从这些错误记录定位具体系统组件；重启仅可作为恢复跟踪状态的尝试，不是已证明的 Vision 修复。
2. 现有整条 Graph 原型已在稳定高时钟、实际游戏负载下未通过收益条件；不再因为局部计时变短而推荐它。
3. 不默认提高优先级、限制游戏 FPS、关闭 HAGS 或改全局功耗来制造收益。这些涉及用户明确关注的游戏吞吐或环境取舍，不能替代当前代码优化的证据。

建议将“系统 ETL 全部 ErrorHeader、整条 Graph 稳定负载下无实质收益、计时事件对照中止”同步到 `.agent-context/`，避免后续重复同样实验；本轮仅写项目报告，未修改需单独确认的上下文文件。
