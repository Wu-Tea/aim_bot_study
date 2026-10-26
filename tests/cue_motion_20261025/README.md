# Cue 遮挡恢复验证

报告：`docs/project/CUE_OCCLUSION_RECOVERY_20261025.md`。所有工具只做离线分析，不打开输出设备。

- `run.py --tag before-hdr --hdr` / `--tag release-hdr --hdr`：两片原生 GPU 推理，读取真实 PTS。PQ/BT.2020 → 线性 → BT.709 → Mobius → SDR；原始录像不改写。`before*` 使用修改前保存的 scan，其余用当前工具。已有 complete.json 时跳过，代码变化请指定新目录。
- `render.py q1:800 --tag release-hdr`：查看案例前后帧，采用同样 HDR 映射。
- `validate_replay.py`：两新片三轮、八旧片一轮，前后二进制交错消费相同像素与固定模型检测。保存的修改前二进制必须复现各自基线行为。结果在 `validation-release/`。
- `check_outputs.py`：核对最终 GPU 与三轮 CPU 结果、原始模型输出不变、两处遮挡恢复及旧素材完整身体立即接回，写入 `behavior-release.json`。
- `simulate.py`：当前 controller + adapter，6 组全片开放回放、8 窗口×2/20 秒×6 输入×4 种子近似闭环；结果在 `controller-release/`。原生 controller_study 由 `tests/vision_controller_study_20261020` 构建。

生产库改变后必须显式重建 `runs/new-video-study-20261025/build` 的 scan 和 `runs/ironsight-cue-drift-20261025/build` 的 replay；普通 runtime 构建不会自动重链接这些独立研究工具。工具依赖本机 ffmpeg/zscale、CUDA/TensorRT 及已有模型，不下载替代模型。

恢复用的是新鲜 cue 与有限期身体标定；无观测期间不产生输出。不能把重新出现的目标帧数直接当作准确率。详细假设、人工复核与剩余限制见报告。
