# 2026-10-25 新视频研究工具

这些工具不打开手柄/鼠标设备，不改变生产配置。报告：`docs/project/NEW_VIDEO_ALGORITHM_STUDY_20261025.md`。

- `scan.cpp`：当前原生 GPU 推理和 selector，原始中心 640×512、120 fps、实际 PTS。产出 JSONL 与含 motion cue 的原生 records；额外全图 cue 检测仅用于诊断。
- `run.py`：读取 `runs/new-video-study-20261025/selected.json`，逐片解码运行 scan，已完整导出的片跳过。依赖本机 CUDA/TensorRT/ffmpeg；工具不自动寻找其他模型或下载依赖。
- `analyze.py`：候选筛选，输出的异常数量不是误检率或已确认缺陷数。
- `render_cases.py v07:21803`：导出指定帧前后各一帧。橙色为选中的 raw box，绿色为其余非友军 raw box，青色为友军，白色为当前使用的身体框，红十字为瞄点。
- `simulate.py`：完整视频开放回放与 12 个窗口的 4 种子短/长数值拟真，依赖已经构建的 `controller_study.exe`。
- `package_evidence.py`：提取 12 个 2 秒窗口，重排 frame ID、将首帧 PTS 平移到 1 秒，其他观测字段保留。原始片段起点记录在 `fixtures/index.json`。
- `geometry_probe.cpp`：真实身体和 cue 固定，仅输入检测框顶缓慢上扩，然后恢复；比较参考累计漂移。退出码 0 只表示探针执行成功，需检查输出；修复后漂移有界，原框返回立即恢复。
- `plot_evidence.py`：绘制实拍与探针证据，依赖 matplotlib。

在仓库根目录配置构建本目录（VS x64 Release），链接 `native/build` 当前生产库。`geometry_probe` 不需要 GPU；`scan` 需要 GPU。模型使用本机 `models/best_480x384.engine`。生产库变更后须显式重建本研究目标。

不依赖原视频的轨迹拟真，可使用 `tests/vision_controller_study_20261020` 构建出的 controller_study：

```powershell
& runs/new-video-study-20261025/controller-build/Release/controller_study.exe profiles/library/p_cd8d772a4f4f4335905ab9b86e3e5741.toml tests/new_video_study_20261025/fixtures/manifest.txt runs/new-video-study-20261025/portable-replay 407771 closed
```

需从仓库根目录运行，Profile 路径可替换为本机已有配置。每条轨迹是屏幕观测，不是世界坐标或标注真值；输入为合成假设，闭环没有重渲染。数值不变量通过不代表目标选择正确或实机验收。不得把本工具输出作为自动发布门槛。

## 本轮 dev 修复验证

结果见 `docs/project/NEW_VIDEO_DEV_FIX_20261025.md`。`fixtures/` 保留修改前观察轨迹，最终修改后完整记录在 `runs/new-video-fix-20261025/final/`，不混用中间输出。生产库修改后，要显式重建 `scan`、`geometry_probe` 和 `tests/ironsight_cue_20261025` 的 `replay`；这些独立工具不会随正常 runtime 自动重链接。

```powershell
python tests/new_video_study_20261025/run.py --output runs/new-video-fix-20261025/final --reuse-timestamps
python tests/new_video_study_20261025/summarize_fix.py
python tests/new_video_study_20261025/compare_fix_performance.py
python tests/new_video_study_20261025/check_fix_replay.py
python tests/new_video_study_20261025/simulate.py --vision-root runs/new-video-fix-20261025/final --output runs/new-video-fix-20261025/controller-final
```

`run.py` 跳过已有 complete.json 的导出；新版本请用新输出目录，避免把旧产物当新结果。性能脚本交错运行保存的修改前二进制和当前 replay；仅计 CPU selector，不能作为实机帧率。
