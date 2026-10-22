# 录像屏幕轨迹素材

17 条、2,645 帧，供本项目原生 Controller 拟真重复使用。每条覆盖一个 1575 ms 目标槽；仿真沿用 50 ms 间隔、2/20 秒生命周期，并重复执行同一片段。无需原视频、FFmpeg 或 TensorRT 推理即可重跑这些已提取轨迹。

从项目根目录运行（先按上级 README 编译原生诊断程序）：

```powershell
node tests/vision_controller_study_20261020/run_trajectories.mjs
```

默认运行 4 个种子，每条轨迹 6 种输入 × 2 种时长，输出逐案例 CSV。可以指定产物目录和种子：

```powershell
node tests/vision_controller_study_20261020/run_trajectories.mjs runs/my-trajectory-study 71020503 71020543
```

轨迹概览见 `overview.svg`，完整来源、原视频帧号、时间段、覆盖情况及标签见 `manifest.json`。

| 类型 | 数量 | 用途 |
|---|---:|---|
| recorded_motion | 6 | 两段主录像分别抽取横向位移、折返、纵向变化 |
| recorded_evidence | 5 | 长 cue、混合来源、漏检、短目标、重新取得目标 |
| negative_control | 1 | 无初始人体参考的纯 cue 冷启动，不应凭空新建目标 |
| diagnostic_fault | 5 | 已知范围切换、HUD 误关联、几何跳变、速度限幅反例 |

每条有两份同源数据：

- `.csv`：相对时间、原视频时间/帧号、selector generation、tracklet、胸部 X/Y、距中心偏移、身体大小、置信度、像素锚点和相邻有效测量速度。无目标坐标留空。速度不跨漏检或换代计算，没有插值补点。
- `.records`：拟真直接读取的完整原生字段，包含所有 detection、选中对象、瞄准区域、来源和身份信息。唯一改动是将 capture 起点平移到 1 秒，原帧号及所有空间/证据字段保留。

tracklet 只是按 selector 身份、换代和超过 40 ms 空档拆分的测量段，不是人工确认的真实人物 ID。若同代身份本身出错，本文件不会把它重新标成正确轨迹。

坐标为中心 640×512 截取框，中心 `(320,256)`。这是**屏幕测量轨迹**，包含原玩家转视角、人物移动和检测误差，不是世界运动真值。闭环将它作为外部扰动，并统一减去模拟相机位移。`diagnostic_fault` 不可当作真实人物理想运动用于调参；大幅主动手动退出的误差也不可直接计为追踪失败。

重新从已有全片 Vision 导出生成：

```powershell
node tests/vision_controller_study_20261020/extract_trajectories.mjs
```

生成器逐字段核对 `.records`，只允许时间原点改变；会检查帧数以及字段一致性。原始数据在 `runs/vision-controller-study-20261020/`。修改 Vision 或模型后应重新做像素导出，现有素材明确冻结在 `8a7f36a`，不能冒充新算法的检测输出。
