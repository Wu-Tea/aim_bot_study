# 验收耗时与 fresh 目标下游拦截（2026-11-11）

本轮按用户澄清处理两个问题：验收用时过长；Vision 已有新鲜、明确目标，后续链路仍可能不输出。上一轮减少测试源码行数不能代表验收加速。本报告中“明确目标”指已选 person、有效几何、正置信度及敌人证据；代码没有名为 `cleared` 的统一放行字段。

## 验收实测

同一工作区、Release 测试二进制、相同 11 个 CTest 套件、相同 GUI 检查；正常宿主权限下，GUI 始终在独立 Windows 桌面。此次单次串行／并行对照，不含构建时间，不是长期性能保证。

| 阶段 | 串行 `--jobs 1` | 并行 `--jobs 3` |
| --- | ---: | ---: |
| 原生测试 | 45.126 s | 23.771 s |
| 桌面测试 | 17.966 s | 21.179 s |
| WebView2 | 5.707 s | 5.964 s |
| 总计 | 68.800 s | 50.915 s |

原生墙钟时间降低约 47%，总验收降低约 26%。桌面测试仍串行，观测到的波动如实保留；不是减少测试数量获得的收益。11 套件包含 529 项基础、101 项功能测试；桌面 99 项（新增进程超时回归），真实 WebView2 页面与宿主流程通过。

原先 45.9 s 基础测试日志中，用例耗时之和为 45.899 s，说明主要时间在用例内部。最慢项包括小目标垂直运动 3.506 s、随机 cue 外观序列 3.177 s、手动转发短长流 2.587 s、随机开火跟踪 2.180 s、输出撤销回放 2.110 s。这些场景保留：直接缩短模拟时长或减少种子会削弱检查。现在使用现有 CTest 套件和各自的产物目录并行，默认并发 3；不改变生产捕获、识别、控制频率。

实测还遇到另一条无限等待路径：沙箱内 WebView2 初始化报告 `0x8000FFFF E_UNEXPECTED`，未触发 loaded 回调，原本放在回调里的检查和退出均未执行。该轮主动中止，不能拿来比较完整验收时间。正常宿主权限下 WebView2 通过，故不把该初始化错误归咎于产品。

隔离测试父进程现在可设置超时，超时只终止自己创建的测试子进程并释放桌面／句柄。统一检查入口给桌面用例 120 s、WebView2 90 s；超时明确返回失败，不静默跳过、不标为通过。0.2 s 阻塞子进程回归验证超时和随后再次创建隔离桌面的正常退出。

```powershell
D:/env/python/python.exe python/tools/check_app.py --scope all
D:/env/python/python.exe python/tools/check_app.py --scope all --jobs 1
# 可选 --build；构建耗时会单独输出。
```

另外验证了 `--scope config --build` 入口：已构建状态下增量构建为 7.288 s，配置检查通过。这不是干净全量编译的测量，构建不计入上表。

日志：`output/acceptance-serial.log`、`output/acceptance-after.log`。每阶段输出 `[Timing]`，CTest 输出各套件墙钟时间；逐用例时间保留在 `native/build/Testing/Temporary/LastTest.log`，该文件会被下一次 CTest 更新。

## 已证实并修复：有界候选传输丢掉已选目标

`NativeGamepadController::observation_batch_from` 原先只取候选列表前 32 项。选择器可以选中后面的 person；此时 preferred ID 仍正确，但相应几何没有进入控制列表。`TargetCoordinator::choose_candidate` 找不到该 ID，因而不会采用这一新鲜目标。这是传输层违反“选择器拥有目标身份”的不变量，不是视觉不清楚。

新增 `selected_person_survives_candidate_capacity`：32 个组合覆盖首次取得／已持有目标、位置 1／32／33／53、正常／友军／无效／缺少瞄点。修前正常目标在索引 32（第 33 项）明确失败；修后全部通过（约 0.4 ms）。实际输出、当前几何和权限都检查，负例不能借容量处理绕过拒绝。

修复仅在已选目标位于截断部分时，为其保留最后一个槽位。仍最多转译 32 项，无新分配；普通不溢出快照没有额外遍历，溢出时扫描剩余 ID。没有增加重试、等待或改变选择器排序。已重新构建运行程序；未自动启动手柄输出。尚无用户事发帧的框数量和已选索引，因此不能断言这就是那次实战问题。

## fresh／已选之后仍可能停在哪

| 环节 | 拦截条件及结果 | 本轮核对依据 |
| --- | --- | --- |
| 发布到控制器 | 配置版本或瞄准 epoch 不一致；例如旧请求的推理刚完成，当前请求已经变化。fresh 发布也不能跨 epoch 投递 | `RuntimeLoop::run_once` 的 policy / request / aim transition 检查；已有 `hot_policy_inflight_frame_revision`、`detection_to_assist_epoch_fence` |
| 画面时效 | 从源画面的 present 时间计龄，包含校准不确定度；达到 live 上限 40 ms 或更小配置值时拒绝。重复帧号／捕获或源时间不前进、时钟无效也拒绝 | `VisionDeliveryGate::accept`；已有 `delivery_uses_source_image_age_and_identity`、`vision_delivery_has_strict_40ms_ceiling` |
| 候选传输 | 已选目标超过前 32 项，被截断 | 本轮修复，前述回归修前失败、修后通过 |
| 初次 ADS 准入 | fresh 目标在当前拾取范围外，保持 Manual／零辅助；进入范围的后续 fresh 帧可以取得权限 | `size_scaled_pickup_revalidated_per_epoch` 等已有回归 |
| BodyLock | 当前目标在跟随范围外，仍可保留目标身份，但 `aim_authority=0` | `TargetCoordinator` 的 `bodylock_outside_activation_range` |
| 当前辅助与手动权限 | 尚未满足辅助启用条件；或显式手动退出进入 HandoverSeek。相同旧目标即使再次 fresh，也不能在同一 ADS 持有期自动夺回手动控制 | `AssistControlStateMachine::update`；身份更换／松开辅助另行处理 |
| 求解到实际发送 | 求解后画面已过期，撤销视觉输出；或 `ManualRelay` 发现输入序号已经更新／样本年龄达到两个采样周期，拒绝该次 AI 输出并发送原始手动状态 | `expire_visual_output`、`ManualRelay::run`；现有转发回归中新增 fresh deadline 但旧输入序号的断言 |

最后一条是必须与 Vision 区分的证据：转发器每个采样周期都会递增序号，即使物理值完全未变。在 1000 Hz 下，计算跨过下一次采样就可能因序号不符被拒绝；另有严格的小于 2 ms 样本年龄要求。新增断言实测 `visual_fresh=true`、`input_current=false`、`request_accepted=false`、`delivered=false`，且物理摇杆、ADS 和 scope revision 未变。此路径能够解释“画面仍明确，但指令没有送出”，但还没有证明用户事件就是这个原因。没有擅自放宽输入新鲜度或撤销规则。

此外，`AdsAlreadyConsumed` 表示这一轮 ADS Snap 已用过，**不等于所有 AI 被禁用**：后续仍可由 BodyLock 输出。不能只看某个 `Rejected` 字段就判定整个控制链被拦。位置刹车、运动项抵消、配置力度上限也可能产生小输出，它们是求解结果，不是 Vision 丢帧。

定位用户事件应优先核对同一帧的输入／输出阶段：Vision 的选中 ID 与索引、source age、epoch、plan mode／authority／decision reason，再看既有 trace 字段 `relay_input_current`、`relay_visual_fresh`、`relay_accepted` 和 `output_delivered`。这些字段已有，未另加每帧日志或同步开销。

## 验证范围

完整串行与并行验收均通过；随后只补强手动转发测试的诊断断言，重编并针对该项复验通过（约 76 ms）。运行程序 Release 构建成功。本轮没有现场同步录像与指令记录，不能把已修复的容量缺陷或已证明的转发拒绝条件当作实战根因结论。
