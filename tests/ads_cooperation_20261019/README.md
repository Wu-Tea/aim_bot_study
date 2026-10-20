# ADS 协作与高速跟随回归（2026-10-19）

本夹具调用生产控制器，不另写简化控制算法。`pre=6496f84` 是重建 clean 前的 dev，`before=998275a` 是本轮修改前，`after` 链接 dev 工作区库。历史版本分别使用自己的头文件和生成参数，避免 ABI 混用。

从仓库根目录执行（Windows VS2022，CMake 3.26+）：

```powershell
python tests/ads_cooperation_20261019/prepare.py
cmake -S tests/ads_cooperation_20261019 -B runs/ads-cooperation-20261019/build -G "Visual Studio 17 2022" -A x64
cmake --build runs/ads-cooperation-20261019/build --config Release -- /m:1 /nr:false
```

先构建生产 `controller_native_core`。若 Windows 环境变量大小写冲突，使用 `python tests/clean_incident_20261016/run_env.py <cmake绝对路径> ...` 包装。历史代码只从 Git 归档读出，不切换分支，不修改 clean。

运行矩阵：

```powershell
foreach ($version in @('pre','before','after')) {
  foreach ($seed in @(49980167,104396113)) {
    & "runs/ads-cooperation-20261019/build/Release/closed_loop_$version.exe" tests/ads_cooperation_20261019/fixture.toml "runs/ads-cooperation-20261019/final-validation/$version/$seed" $seed
  }
  & "runs/ads-cooperation-20261019/build/Release/live_$version.exe" tests/ads_cooperation_20261019/live.csv "runs/ads-cooperation-20261019/live-$version.csv"
}
python tests/ads_cooperation_20261019/summarize.py runs/ads-cooperation-20261019/final-validation
```

- 最终独立种子为 49980167、104396113；开发/反例种子为 2026101903、49979923、49980013、104395751。不能将已经用于调整的种子再称为独立验证。
- 每种子 48 场景 × 2/20 秒，1000 Hz；固定 1575 ms 目标槽＋50 ms 间隙。每版本最终 192 场景、2,112,000 tick。
- 六种运动：静止、60/360/600 px/s 横移、静止世界目标下左右走位造成的正弦屏幕漂移、二维变向。走位是显式外部屏幕运动模型，未模拟游戏透视或具体步速。
- 八种手动：中立、持续同向、固定 .30 偏置、后发协作、逐轴反向/释放、开火、强退出、.30/-.18 加 cue/缺帧。固定偏置和反向输入是权限压力场景，不能作为理想跟枪输入解释。
- 真实相机响应随机 900–2500，FIFO 延迟 6–30 ms；检测周期 5–12 ms、交付延迟 2–10 ms，位置噪声 ±.25 px。参数随种子固定，三版本配对。
- 显式固定 `max_observation_age_ms=40`，缺帧预测 32 ms，检查胸部参考、权限失效、手动退出/透传、cue 无开火权限、阶段单一所有者。个人 profile 未含 tracker 字段时，独立加载器的旧默认是 50 ms；实机的 `live_control_config` 在启动和热更时封顶 40 ms。本夹具显式对齐该实机边界，不是修改个人 profile。
- `fixture.toml` 冻结本次控制参数，排除设备标识和用户路径。开火植物使用恒定外扰 `.2 * response`，压枪仍通过真实非线性生产合成；它不等于真实枪械后坐力。
- `comparison.cpp` 保留上轮同参旧 dev 对照（理想 5 ms 新帧、2/20秒），命令也是 `profile output-dir seed`。

`live.csv` 来源为 clean 会话 `20261019T124949Z_27452_1` 的约 492 s / 981 s 两窗，共 32 个 nominal ADS 样本；`source_line` 可回到原遥测。`extract_live.py` 从上轮留存的 `runs/manual-sticky-20261019/live-windows.jsonl` 再提取。旧两版必须重现横向 pre-recoil 输出；新版本检查额外工作未超过手动本身。此处固定原 shaped AI，未记录的 ADS 总目标速度不伪造，**是手动仲裁局部重放，不是完整实录反事实或视频重放**。

数值结果与未解决问题见 `docs/project/ADS_COOPERATION_20261019.md`。所有场景执行成功仅表示数值/权限断言通过，不表示每项效果无退步，更不表示实机验收。
