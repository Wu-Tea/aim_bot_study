# 机瞄遮挡与 cue 身体参考回放

生产修复与限制见 `docs/project/IRONSIGHT_CUE_EXTENT_20261025.md`。

`replay.cpp` 使用原生 `VisionTargetSelector`，输入为时间戳、原始 person 检测框和中心 640×512 BGRA 像素。每次选择单独计时；解码、换色、IPC、日志写入均在计时之外。固定 E 核亲和性，测试不连接手柄，不向游戏输出。

构建示例（本机 CMake 位于 Visual Studio 安装目录）：

```powershell
cmake -S tests/ironsight_cue_20261025 -B runs/ironsight-cue-drift-20261025/build -A x64
cmake --build runs/ironsight-cue-drift-20261025/build --config Release -- /m:1 /nr:false
```

先在修复前构建并保存 `before.exe`，再用修改后的生产库重新链接。原始检测由 `vision_cue_video_review` 从原录像和 `models/best_480x384.engine` 导出，不能拿 selector 修正后的身体框替代模型输出。该工具是 `EXCLUDE_FROM_ALL`，每次生产库改变后必须显式构建 `--target vision_cue_video_review`，全量默认构建不会刷新它。回放默认使用 `ffprobe` 的实际 PTS；需要复现旧 GPU review 的固定时钟时可指定 `--uniform-fps 120`。参数与当前 Profile 相同：640×512、pickup 190、目标高度 0.2、宽低框 0.65、默认洋红/青色。

```powershell
python tests/ironsight_cue_20261025/run.py VIDEO REVIEW_JSONL OUTPUT_DIR BEFORE_EXE AFTER_EXE
```

输出 `0.csv` / `1.csv` 对应两个程序，字段包括阶段耗时、目标存在、位置、身份、来源、开火权限、身体上下边界和 cue 诊断。对照至少三轮完整输入，跳过最初十帧计算均值与 P95。

旧黄/绿录像可显式加 `--legacy-colors`，它用 OpenCV HSV 掩码将黄/绿像素合成为洋红/青色（保留模型原始框、改变背景同色像素）。此模式仅检查算法边界，不能作为新配色实拍，也不是像素级等价的旧颜色实现。

固定回归在 `native/vision_native/src/cue_identity_tests.cpp`：录制框尺寸、逐像素收缩、四独立种子的短长缩放/运动、独立人体恢复及不完整框边界、静止目标的原生 Controller 输出。新的素材不要求复制进仓库；本机原始产物在 `runs/ironsight-cue-drift-20261025/`。
