# 框内目标点寻址改进：有界上方接近

日期：2026-09-07。状态：代码、原生回归及当前工作区运行程序已完成；实战手感未验收。

## 行为

保留当前检测模型和视觉默认瞄点。在物理 ADS 周期内首次通过新鲜目标准入时，由 DesiredPointReducer 选择最终期望点 D：

- 准星位于默认点上方且横向接近中线时，可选择略高、路径较短的位置。
- 水平坐标保持视觉默认值。横向距离必须不大于瞄准区域宽度的 25%。
- 上移量不超过瞄准区域高度的 10%。标准人框的区域为 22%–58%，因此最大约为框高的 3.6%，即 40% → 36.4%。这是工程限幅，不是解剖位置或轮廓安全证明。
- 预留两倍现有到位半径作为接近/制动距离。近中心捕获不改点，避免自动选点制造到位或抹掉中心穿越。
- 侧方、下方、上侧方超出中央走廊、非 VisionGeometry 来源保持默认点。
- 选中的点保存为区域内相对坐标；后续视觉刷新、提示点续接、ADS→BodyLock 不重新向默认点回中。
- 显式手动修正仍优先；新目标重置旧偏移。新物理 ADS 准入可以重新选择，已有手动修正不会被覆盖。

核心计算（屏幕 Y 向下）：

```text
预算 = min(0.10 × R.height, max(0, sourceY - crosshairY - 2 × arrivalRadius))
desiredY = max(R.top, sourceY - 预算)
desiredX = sourceX
```

上述预算只在符合上方中央进入条件时应用。普通跟踪帧不会重新计算进入方向。

本次优化选点与保持，不调整移动增益、到达时间、模型或图像推理频率。

## 原因与归属

视觉 `target_point` 按框比例产生默认点；旧 DesiredPointReducer 在每个非提示点、非手动修正的新帧中恢复默认点。仅修改初始位置会被下一帧覆盖。

最终 D 属于 DesiredPointReducer，因此选点和保持都在该状态所有者内完成。TargetCoordinator 只在新鲜 ADS 准入后调用一次。视觉的源点、关联、源速度估计保持原有语义，避免将自动瞄点选择当作人物运动。

新增诊断值 `desired_point_source="approach_selected"`。枚举追加在末尾，保留既有序号。

## 验证

- 冻结的 TargetCoordinator 场景先 RED 后 GREEN。使用固定 60×200 人框、72 像素高瞄准区域和同一接近轨迹；模型/视频不参与该控制回归。
- 旧版源点与最终点均为 Y=260，节省路径 0；固定接近轨迹结束后剩余向下误差 7.2 像素。
- 新版源点仍为 Y=260，最终点 Y=252.8，节省 7.2 像素；到位、提示点续接和恢复新鲜视觉后的误差均为 0。
- 覆盖侧向双轴、下方、上侧方、弱几何来源、手动修正、替换目标、无源帧和 ADS→BodyLock。新增原生控制器端到端测试验证输出方向、范围、诊断来源及松开 ADS 后输出归零。
- 首版候选影响旧中心穿越回归；已通过预留接近距离在生产选点层修正。旧回归和阈值保持原样。
- 10 个 Base/Feature 测试组、365 个用例全部通过，0 failed / 0 invalid。
- 遥测日志轮转用例在一次中间运行失败，未改其代码；最终同一套组测试通过。该间歇性失败未定位，不将本修改称作遥测修复。
- 当前工作区日常启动路径 `native/vision_native/build/Release/cod_native_runtime.exe` 已成功重新构建。没有自动启动游戏控制，也没有同步其他工作树。

命令：

```powershell
cmake --build artifacts/native-review-20260905/build --config Release --target cod_native_base_tests cod_native_functional_tests --parallel 4
ctest --test-dir artifacts/native-review-20260905/build -C Release -R "^(Base|Feature)" --output-on-failure
cmake --build native/vision_native/build --config Release --target cod_native_runtime --parallel 4
```

使用现有 CUDA/TensorRT DLL 搜索路径。回归包在 `artifacts/target-addressing-20260907/`：`regression-manifest.json`、`red-final/`、`green-final/`、`final-tests.log`、`artifact-manifest.json` 记录命令、哈希和测量值。初期夹具在生产编辑前移除了依赖候选输出的相机轨迹，并补齐 cue 准入条件，最终 RED 后夹具保持不变。

## 使用与限制

从本工作区按原来的启动方式运行即可，无新增设置。已运行的进程需要重新启动才能载入新二进制。

该方案不识别颈根、头部或身体轴，也不能保证避开四肢。侧身、截断、遮挡和框抖动仍是几何信息不足的边界。本次 7.2 像素结果是固定合成场景的路径节省，不是实战命中率、到达时间或 GPU 性能提升。实际手感及匹配 live A/B 仍待确认。
