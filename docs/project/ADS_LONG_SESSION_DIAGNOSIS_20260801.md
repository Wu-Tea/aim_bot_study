# ADS 长局轻微过冲 / 欠跟日志诊断

日期：2026-08-01（Asia/Hong_Kong）
性质：只读诊断；本次同步没有据此修改生产控制逻辑

## 结论

最新约 25.5 分钟 bot session 不支持“学习器随游玩时间持续漂移”是主要原因。
更符合日志的解释是：

1. ADS 的 `220 ms` acquisition ceiling 从物理 LT epoch 起算，而不是从当前目标
   真正进入 ADS acquisition 的时刻起算；
2. 目标晚出现或被重新捕获时，只能使用该 epoch 剩余的几十毫秒，随后被固定切到
   BodyLock，形成欠一点；
3. 玩家/目标移动让目标在接近 `220 ms` 时穿过中心，旧方向的速度前馈仍可短时保留，
   形成拉过头。

因此当前优先级应是“移动场景 + ADS 交接时限/前馈方向”，不是新增学习策略或把
`rollout_shadow` 接入生产。会话态 `AimResponseEstimator` 可能对幅度有次要贡献，
但当前日志没有直接记录它的生产 scale/confidence，暂时不能完全排除。

## 证据范围与身份

| 项目 | 值 |
|---|---|
| 最新 session | `runs/native_perf/sessions/20260801T131000Z_6544_1/` |
| session manifest SHA-256 | `3497A244E9689ABBFB6E35D970A9EFDFE28741E2097F24D10A252FE69CC0524A` |
| 时长 | `25.51 min` |
| JSONL 分片 | `9` |
| 有效记录 | `1,733,193`，另有 1 条尾部未完整记录被忽略 |
| ADS transition | `448` |
| 解析到的 ADS runs | `355` |
| 同 target 正常交给 BodyLock | `260` |
| 满足 `>=20 ms` 的正常交接样本 | `246` |
| manifest config hash | `db80e13a191df21905218dd53d590c299b91b8527b9ceabc2d9692dacdf79df8` |
| TensorRT engine SHA-256 | `45FC56274FF3BBC659E534C3B7833065B0483EF8022AC5D7657CD6DA7DBDEB21` |
| capture / tensor | `640x512 -> 480x384` |
| 当前安装 runtime SHA-256 | `DE31FF53B4C0CFBAB091F589CB194296A01DC9C0C8B5E74513F90AB94ED30590` |

manifest 仍写着 `state=active`，且 `git_commit=5d9f6d3...` 是配置侧保留的旧
provenance；它也不记录 executable hash。因此，本次日志与 `DE31...` runtime 的
关联依赖 21:04 构建、21:10 启动且中间没有再次覆盖的安装链，不把 manifest 内的
commit 字段当作充分的二进制身份证明。

## 时间趋势检查

把 246 个正常 ADS→BodyLock 交接按会话时间四等分。下表的“结束误差”是交接时
目标误差模长；“过冲/欠跟”是本次只读分析使用的统一诊断分类，不是新的产品
acceptance 指标。

| 时间段 | 结束误差中位数 | P90 | 过冲占比 | 欠跟占比 | 左摇杆 RMS 中位数 |
|---|---:|---:|---:|---:|---:|
| 1.30–6.76 min | 16.62 px | 39.55 px | 39.3% | 41.0% | 0.993 |
| 6.82–12.91 min | 10.67 px | 35.49 px | 56.5% | 17.7% | 1.001 |
| 12.92–18.35 min | 15.39 px | 61.40 px | 45.9% | 29.5% | 0.991 |
| 18.41–25.22 min | 16.91 px | 61.90 px | 59.7% | 27.4% | 1.007 |

如果是随时间积累的学习漂移，应该看到误差、过冲或响应幅度呈明显单调趋势；
实际 Spearman 相关性很弱：

- 结束误差 vs. 会话时间：`rho=0.070`；
- 过冲 vs. 会话时间：`rho=0.121`；
- 欠跟 vs. 会话时间：`rho=-0.096`；
- 响应幅度代理 vs. 会话时间：`rho=-0.054`。

Q4 的尾部确实比 Q2 差，但 Q1 本来就不低，Q3/Q4 也不是随时间持续单调恶化。
73 个有效 ADS 视觉 transition 的早/晚误差中位数约为
`29.08 px / 29.57 px`，同样没有出现后半局整体漂移。

## 三个能解释现场手感的晚局窗口

`TargetCoordinator` 当前用物理 LT epoch 的 elapsed time 判断
`ads_max_acquisition_ms=220`。下面三段都在该 ceiling 附近发生交接：

| ADS epoch | 当前目标进入 ADS 的时刻 | 当前目标实际 ADS 时间 | 交接时刻 | 现场含义 |
|---:|---:|---:|---:|---|
| 411 | LT 后 `181.95 ms` | `36.29 ms` | 约 `218 ms` | 大误差目标只获得 36 ms，明显欠跟 |
| 416 | LT 后 `25.02 ms` | `194.01 ms` | 约 `219 ms` | 已接近中心；交接后观测/几何突然移动，非学习器单独可解释 |
| 443 | LT 后 `52.09 ms` | `168.64 ms` | 约 `220.7 ms` | 目标穿过中心后旧方向前馈继续，形成过冲 |

### Epoch 411：目标晚出现，ADS 预算被截短

目标重新进入时误差约为 `(+110.5, +9.7) px`，但物理 ADS epoch 已过去约
`182 ms`。请求 X 可达 `+1.294`，shaper 仍需从约 `+0.08` 渐入，只有约
`36 ms` 就被强制交给 BodyLock。交接时误差反而约为 `(+129, +22) px`，
BodyLock 又因超出 activation/进入 coasting 而没有继续有效输出。这是典型的
“当前目标没有拿到完整 acquisition 时间”。

### Epoch 416：接近中心后发生观测/运动突变

当前 ADS 段从约 `(-106.7, -40.6) px` 开始，在 194 ms 内接近
`(-2, +2.5) px`，随后到 `(+17, +16) px`，并在物理 epoch 约 219 ms 时交接。
进入 BodyLock 后约 4 ms，同一 track 的观测跳到约
`(-134.6, -99.4) px`。一个正的标量 response scale 不能凭空造成目标位置跳变
或符号翻转；这里首先是目标/视角/geometry 运动，交接时机只是让它更显眼。

### Epoch 443：穿过中心后仍沿旧方向推

当前段从约 `(-51, -6) px` 开始，约 64.9 ms 后误差翻到
`(+31.5, -33.1) px`。此后约 100 ms，请求 X 仍保持负向，约在
`-0.4..-0.7`，说明速度/前馈状态还沿旧方向；最终在 ceiling 附近以约
`(+78, -7) px` 交接。同期左摇杆长期接近满幅对角移动，这与“移动导致中心穿越，
旧方向前馈来不及退出”一致。

## 为什么暂不判成学习问题

- 顶层 `rollout_shadow` 只观察和记账，不驱动生产输出。
- 会话态 `AimResponseEstimator` 确实把 response scale 反馈给
  `TargetCoordinator`，所以它不是完全无关。
- 但它提供的是幅度标量，不能解释 epoch 443 的方向符号错误，也不能解释
  epoch 416 的瞬时观测跳变。
- 时间相关性和早/晚 transition 分布没有显示稳定的累积漂移。
- 当前 telemetry 没有直接记录生产 estimator 的 scale/confidence，因此结论是
  “不是主要原因”，而不是“数学上完全排除”。

## 下一步验证与实现边界

本轮不直接改代码。若继续修复，应按以下顺序：

1. 增加只读 telemetry：物理 ADS epoch elapsed、当前 target segment elapsed、
   handoff reason、position/motion contribution、生产 response scale/confidence。
2. 建立三个确定性 fixture：LT 后 170–200 ms 才出现目标、180–220 ms 穿越中心、
   无移动静止目标对照。
3. 只有在 fixture 稳定 RED 后，评估“bounded continuation”或 motion-aware
   handoff guard；不能把新目标当作新的物理 LT，不能重置为第二次强 ADS snap。
4. 验收必须同时确认：欠跟下降、过冲/continued push 不增加、同一 LT 仍只有一次
   强 snap、full manual escape 和现有 dual-proposal 仲裁不回归。
