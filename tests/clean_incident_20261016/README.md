# 2026-10-16 clean 缺帧与身体框回归

这些 DEV 夹具验证 clean 的生产库，产品分支不携带测试。修复对象是身体框高宽比 0.65–0.85 的准入空洞，以及同一目标缺帧超过 32 ms 时提前清除身份、结束尚未完成 ADS 的状态错误。身份可以延续到已有的来源硬时限，预测输出仍受原来的 32 ms 限制。

## 场景来源与边界

来源会话为 clean 的 `runs/native_perf/sessions/20261016T123101Z_39880_1`；末段视频为 `E:\迅雷下载\Replay 2026-10-16 20-35-04.mp4`。视频约 +10.1–11.9 秒对应日志 20:35:01.48–03.23。DEV 的 `output/incident-20261016` 保留对齐图与提取数据。

`incident.cpp` 固定了 JSONL 第 117283、117347、117382、117494、117529 行的五组框尺寸，以及 20:35:01.724 缺帧、01.729 来源年龄超过 32 ms、01.735 新鲜同 generation 返回的时序。旧 clean（ba679ce）在几何、短缺帧和相邻边界三组回归上均失败。

回归重建的是视频定位出的日志几何和状态序列，没有重新对视频逐帧运行模型。几何准入与 selector 连通性分别检查；selector 连通性使用固定高置信度以隔离几何缺陷。真实模型完全漏检、较小目标未达准入尺寸，以及人工反向输入受到保护，都不能靠这两个修复消除。

## 构建和运行

先按产品构建说明构建目标 checkout 的 Release 库。下面命令在 DEV 根目录执行；`ProductRoot` 可替换为临时 clean worktree。依赖使用本机现有 VS 2022、CUDA 13.1、TensorRT 10.15 与 SDL2。

```powershell
./tests/clean_incident_20261016/build.ps1 -ProductRoot D:/work/AI/yolo-study-001-clean -BuildName candidate
./runs/clean-incident-fix-20261016/candidate/Release/incident_checks.exe
foreach ($seed in 20261016,730201,982451,416103) {
  ./runs/clean-incident-fix-20261016/candidate/Release/incident_checks.exe $seed runs/clean-incident-fix-20261016/randomized
  if ($LASTEXITCODE -ne 0) { throw "seed $seed failed" }
}
```

需要将产品使用的 SDL2.dll 放在测试 exe 旁。`run_env.py` 统一 Windows 环境变量大小写，避免宿主同时提供 Path/PATH 导致 MSBuild 失败；不更改系统环境。

`prepare_suites.py <ProductRoot>` 把 DEV 已跟踪的测试源与产品源组合到 `runs/.../suites`，生成 Base 与压枪/开火测试构建。2026-10-18 同步修复到 DEV 后，原生夹具直接分别检查身份硬时限与 32 ms 预测时限，不再在生成副本中替换断言。50 ms 测试配置的身份过期在原 capture 年龄 51 ms 检查。复制 SDL2 后运行：

```powershell
python tests/clean_incident_20261016/prepare_suites.py D:/work/AI/yolo-study-001-clean
./tests/clean_incident_20261016/build.ps1 -BuildName suite-build -SourceRoot D:/work/AI/yolo-study-001/runs/clean-incident-fix-20261016/suites
./runs/clean-incident-fix-20261016/suite-build/Release/base_checks.exe --artifacts runs/clean-incident-fix-20261016/artifacts
./runs/clean-incident-fix-20261016/suite-build/Release/fire_checks.exe
```

桌面回归用 `gui_checks.py <ProductRoot>` 在私有 Windows 桌面运行 81 项测试和 WebView2 交互检查。`live_gui.py <ProductRoot> <新建输出目录> <模型绝对路径>` 使用独立配置、隐藏窗口、关闭输出与震动，验证真实 runtime 初始化、GUI 保存与热更新回执、IPC、停止按钮和正常退出。它不会修改用户配置。

## 随机仿真的含义

开发种子为 20261016、730201，独立验证种子为 982451、416103。每个种子有 16 类场景，各跑 2 秒与 20 秒，1000 Hz；总计 128 案例、1,408,000 tick。链路是实际 VisionTargetSelector → adapter → NativeGamepadController → 交付回执 → 合成相机。固定目标节奏为 1575 ms + 50 ms 空隙，输入在第 1500 ms 松开扳机。

捕获间隔 5–12 ms，结果延迟 2–12 ms，缺帧覆盖 22/35/75 ms 和 200 ms；覆盖友军拒绝、逐轴轻推/反向/强推、开火和压枪。相机显式假设为线性 1400 px/(stick·s)、9 ms 输出延迟；并非游戏真实响应曲线。检查来源时限、预测时限、原始透传、显式退出、源瞄点不受手动改写、物理输出范围与 ADS/BodyLock 生命周期。

逐案例 CSV 记录误差、中心穿越、最大输出变化及各状态 tick 数，供观察行为，不是游戏表现分数或实机验收。真实显示延迟、遮挡和模型漏检只能在实机继续验证。

## 2026-10-18 DEV 同步与探索

相同夹具分别链接 DEV 与 clean，使用新种子 20261018、104729（开发）和 15485863、32452843（独立验证）。本轮结果位于 DEV `runs/dev-clean-sync-20261018/`。生产同步及探索结论见 [报告](../../docs/project/DEV_CLEAN_SYNC_20261018.md)。

`incident_checks.exe <seed> <目录> [额外延迟毫秒]` 支持测试侧的 0–20 ms 延迟偏移，不改生产时限或参数。本轮比较 0/6/12 ms，捕获、输入和目标节奏保持相同种子；result 时间戳与合成完成时间一致。合成结果按 FIFO 交付，后到期的早帧可能阻塞后帧；这是仿真假设，不代表生产 latest-only mailbox。新增 tracking 指标使用固定的每周期 100–1500 ms 窗口，包含失去目标时的误差，避免只统计辅助有效帧造成偏差。`expired_ticks` 历史列名实际表示无目标 tick，也包括松杆和周期空隙。

`incident_checks.exe --geometry-probe <目录>` 固定框的左上角和宽度，将高度每 100 ms 在 64.8/65.2 px 间切换，经生产 selector/adapter/controller 记录源瞄点和输出。它是隔离几何因果的开环探针：不让输出改变输入画面，不能把该输出振幅当作实际游戏振荡幅度。测试侧 CSV 可复查公式跨越 0.65 时的连续性。

## 0.65 边界修复回归

新增 `continuous_geometry` 检查瞄点和区域同时连续、两端配置参考不变、相等/反向比例、完整高宽比扫描以及缩放平移不变性。原生默认 BaseVisionSelection 另经真实 selector 连续输入 200 帧尺寸抖动，检查目标身份、源点和区域；GUI 检查预览协议 v4 与原生参考一致。原版的独立回归仅 `continuous_geometry` 失败，不能把这个预期失败误报为全部测试通过。

```powershell
incident_checks.exe 2026101801 <输出目录> 0 boundary trace
incident_checks.exe 179424673 <输出目录> 0 boundary-neutral
incident_checks.exe 86028121 <输出目录> 0 boundary-confirmed
incident_checks.exe 67867967 <输出目录> 0 boundary-neutral-confirmed
```

上例演示场景名；完整验证让四个种子各运行全部场景，前两个用于开发、后两个独立验证。每个种子/场景有 16 类 × 2/20 秒。`boundary` 保留手动/开火练习，`neutral` 去掉手动和开火，`confirmed` 给检测提供明确敌方颜色证据；不改变生产权限策略。场景胸部真值固定在框顶下方 27.625 px，检测框高独立抖动于 64.6–65.4 px，不用生产混合公式重新定义真值。原有 `mixed` 场景继续检查相邻路径与来源时限，但不用于边界精度结论。

CSV 分别记录初始、手动介入、释放阶段误差与 ADS/BodyLock tick 数。可选 `trace` 为前三个短案例记录状态轨迹。所有场景保留原 1575+50 ms 节奏、合成相机和交付延迟假设。初始没有敌方证据的案例须与 confirmed 分开看：修复后 ADS 正常完成会进入原有弱证据 BodyLock，不能为改善总体误差而提高其权限。详见 [修复报告](../../docs/project/GEOMETRY_CONTINUITY_20261018.md)。
