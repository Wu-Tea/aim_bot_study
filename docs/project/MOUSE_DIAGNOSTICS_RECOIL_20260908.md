# Mouse 运行诊断、BodyLock 慢拖与固定下压

2026-09-08。按用户反馈落实日志、近距离卡住检查、可配置持续压枪。默认 AI 速度保持 2，FOV/DPI/游戏灵敏度继续读 `[mouse]`。本轮只做离线验证，没有启用实体鼠标捕获或完成新版本游戏手感验收。

## 使用

启动 `scripts/launch/mouse_start.bat`；改配置后重启。`--check-config` 会打印生效值，不打开设备。

```toml
[mouse]
speed = 2.0
breakaway = 4.0
bodylock_deadzone = 0.5
recoil_enabled = true
recoil_counts_per_second = 30.0
recoil_require_ads = true
log_enabled = true
log_directory = "runs/mouse"
log_max_mb = 2048
```

`recoil_counts_per_second` 是虚拟鼠标原始 counts/秒，正 Y 向下，范围 0..10000。它不是屏幕像素，也不会被 `speed` 再乘一次。30 是可调起点，不是已标定的某把枪参数；不同游戏灵敏度对应不同压枪角速度。`recoil_enabled=false` 或速度 0 关闭。默认要求右键和开火同时成立；设 `recoil_require_ads=false` 可在腰射开火时下压。

开火依据所选实体鼠标左键或当 tick 获准输出的 AutoFire 状态。无目标时实体开火仍可压枪。松开、AutoFire 间隔、校准、停止时撤销输出并清掉不足一个 count 的余量；下一次开火重新计时。持续开火按实际 elapsed time 积分，亚整数余量连续累计；时钟倒退或超过 50 ms 的停顿不会补发压枪。它是固定匀速下压，不识别枪械，也不保证抵消不同武器的后坐力曲线。

## 日志

后续[目标点控制修正](MOUSE_TARGET_POINT_CONTROL_20260908.md)已补充原始/最终目标点、每轴容差命中、位置与运动补偿字段，并记录正式运行的诊断结果。最新默认配置与全部验证结果统一见 [Mouse Overview](MOUSE_OVERVIEW.md)。

启动控制台会打印详细日志目录。每次运行创建新的 `runs/mouse/<UTC epoch microseconds>-<PID>/`：

- `session.json`：生效鼠标参数、识别/控制配置摘要、程序/配置/模型 SHA-256、输出语义。
- `ticks-0000.jsonl` 等：逐控制 tick 记录，约 64 MB 分段。
- `summary.json`：正常结束标记、入队/写入/丢弃条数、写盘失败、容量耗尽。强制结束可能没有此文件。

控制线程只拷贝固定大小记录到 8192 项 SPSC 队列；后台低优先级线程序列化写盘，每秒 flush。无磁盘等待放进控制 tick。队列满丢日志并计数，不阻塞鼠标。单次 tick 日志达到 `log_max_mb` 后停止接收并计数，控制台持续显示状态；不会自动删除其他运行的日志。需自行按需要保存或清理旧记录。

主要记录包括：

- 控制开始/结束时间、控制器 dt；最新识别帧的 capture/result 时间、是否获准交付、控制器实际使用的 frame/target/generation 和帧龄。
- BodyLock/ADS 阶段、生命周期、限幅原因、目标框与瞄点/误差、视觉权重。
- 实体 M、归一化 M、过滤后意图、AI 请求/整形量、手动保留比例/冲突、D 修正与 handover。
- 瞄准输出 counts、量化余数、压枪请求/实际 counts、最终 T；实体左右键和 AutoFire。
- 消费窗口 epoch/sequence、source begin/end、时间戳、提交/取消 counts、HID report 数、按钮滚轮与错误。

`aim` 是压枪前的完整瞄准输出 T，已包含保留的手动输入，不是额外 AI 增量；`final = aim + recoil`。不得把 `source + final` 当期望。`delivered`/`submitted` 只表示传输 API 接受，不是游戏独立接收证据。日志不完整、队列丢数据或容量耗尽时不得用总数证明守恒。

快速分析最近一次运行：

```powershell
python scripts/verify/analyze_mouse_diagnostics.py
python scripts/verify/analyze_mouse_diagnostics.py runs/mouse/<session> --output runs/mouse-review.json
```

分析器流式读取分段，报告控制/识别间隔分位数、帧龄、输入/输出总量、BodyLock 手动被挡且 AI 输出为零的签名、方向反转、窗口对账、日志完整性。方向反转本身不等于异常抖动。可将问题发生的大致时间和该次日志一起提供，继续定位。

## 已证明的问题与修正

旧速度死区把低于阈值的 M 同时从意图分类和最终输出里清零。持续慢拖永远不会成为 D 修正；目标已居中、AI 自身无请求时，最终输出就一直是零。放大目标框到 300×700，已建立 BodyLock 后输入 80 counts：原版本输出 0，关闭死区的对照输出 80。X/Y、1/2 ms 控制周期、1/16/33 ms 识别间隔均覆盖。

修正在意图过滤拥有者引入有界空间容差，其宽度为 `bodylock_deadzone × 0.004 seconds` 的归一化位移。小幅往返留在容差内，不产生索敌/D 修正；持续移动超过边界后把该轴识别为手动修正，最终裁决不能再按速度死区把它清掉。物理 M 保留原值，没有补发历史被挡位移。默认响应下容差约 9.3 counts；同样输入 80 counts，候选输出 71/72，首个非零输出在 10 ms。此处时间只是 1 count/ms 固定输入下的测量，不是强制等待时长。

压枪在瞄准裁决之后合入单个最终报告，并将实际下压反馈给共享相机运动观察器。否则观察器可能把程序自己的压枪误认成目标移动。F11 校准输出仍独占；设备生命周期与单窗口提交契约不变。

这些用例证明了本地控制不变量，不能冒充未记录的游戏事件精确重放。近距离目标的其他几何/视觉限制、真实低 FPS 下抖动、枪械/手感接受仍需要新运行日志与游戏验证。证据包：`runs/mouse_incidents_20260908/`。
