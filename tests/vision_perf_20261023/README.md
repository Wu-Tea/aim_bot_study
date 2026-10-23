# Vision 阶段耗时分析

只读离线录像，无设备捕获/输出。报告见 `docs/project/VISION_FPS_DIAGNOSIS_20261023.md`。先构建当前 dev Release 库；在仓库根目录运行：

```powershell
python tests/vision_perf_20261023/prepare.py
ffmpeg -v error -n -i 'E:/videoCut/2026-10-22 10-40-12-cut-merged-1792637433471.mp4' -vf 'crop=640:512' -fps_mode passthrough -pix_fmt bgra -f rawvideo runs/vision-perf-20261023/clip.bgra
cmake -S tests/vision_perf_20261023 -B runs/vision-perf-20261023/build -DOLD=D:/work/AI/yolo-study-001/runs/vision-perf-20261023/old
cmake --build runs/vision-perf-20261023/build --config Release -- /m:2 /nr:false
$env:PATH='D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;'+$env:PATH
foreach($variant in @('current','previous')) {
  & "runs/vision-perf-20261023/build/Release/profile_$variant.exe" D:/work/AI/yolo-study-001-clean/models/best_480x384.engine runs/vision-perf-20261023/clip.bgra "runs/vision-perf-20261023/$variant-ecores.csv" 1 3
}
python tests/vision_perf_20261023/summarize.py
```

CMake需可找到MSVC和CUDA。命令末尾`1 3`表示使用生产6个E核亲和性、每个CPU模式3轮。程序会读取约872MB原始帧到内存，以避免磁盘/解码进入计时。`previous`为71f9104的selector/cue实现和对应头文件，其他未修改依赖使用当前库。

selector统计不包含单独cue诊断调用；cue诊断另外计时，不可重复相加。推理/全框回读每帧测一次，其值在CPU轮次中复用。关闭cue的对照仍提供全框CPU图像，`roi_ratio`记录该路径实际请求区域；因此readback列是统一的全框回读成本，并非对照模式真实ROI回读成本。旧模式会改变识别行为，仅用于区分成本来源。新实验应使用新产物目录，保留已引用的原始结果。
