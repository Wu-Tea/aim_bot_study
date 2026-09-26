# Vision：不依赖游戏的资源竞争 A/B

日期：2026-09-07。状态：**工具已实现、Release 编译和功能验证通过；尚无有效的性能候选结论。**

## 可以测什么

用冻结的训练图片驱动真实 TensorRT 推理，另起一个固定工作量的 D3D11 图形进程，替代手动进入游戏制造 GPU 竞争。可以比较资源交接、预处理、推理、结果同步等代码策略，测量串行处理频率、单帧完成时间、结果交付间隔及其 P99。

两个进程分开计量。**15% 限制只作用于 Vision 进程的 Windows PDH 最忙引擎**，不是把模拟图形负载也限制为 15%。采样周期约 500 ms，稳态任一样本超过 15% 即中止；该口径不保证瞬时占用上限。

图形负载使用固定 1920×1080 离屏渲染、固定全屏着色器、固定 passes 和目标频率。一次最多一帧在 GPU 上处理，没有无限提交队列。不会根据候选快慢动态调负载，也不提高进程或线程优先级。Vision 沿用生产配置的高优先级 CUDA stream，A/B 均保持一致。

## 代码及调用

- `native/vision_native/src/vision_contention_benchmark.cpp`：真实 TensorRTEngine、CudaGraphicsMapping、D3D11 纹理复制；不启动 Controller 或输出设备。
- `native/vision_native/src/vision_graphics_load.cpp`：独立图形进程、GPU timestamp query、完成时间和丢失调度槽计数；计时结束后读回图像，检查着色器确实产生非恒定图像。
- `native/vision_native/src/bench_support.h`：设备匹配、QPC 时钟和检查。
- `scripts/benchmarks/run_vision_contention.py`：A/A 或 ABBA 编排、证据冻结、逐帧结果核对、GPU 限制及结论。
- `scripts/benchmarks/vision_gpu_sampler.py`：PDH 分进程计量、NVML 时钟/设备身份。
- CMake 中两个目标均为 `EXCLUDE_FROM_ALL`，不会加入普通产品构建和 CTest。TensorRT DLL 会复制到推理测试程序旁。

当前工作区已有编译产物。GPU 空闲时运行相同代码的 A/A：

```powershell
& D:/env/python/python.exe scripts/benchmarks/run_vision_contention.py `
  --output artifacts/vision-offline-contention-20260907/aa-clean-60-p4 `
  --hz 60 --load-passes 4
```

测试省略显式 Flush 的候选，顺序固定为 A→B→B→A：

```powershell
& D:/env/python/python.exe scripts/benchmarks/run_vision_contention.py `
  --output artifacts/vision-offline-contention-20260907/abba-no-flush-60-p4 `
  --candidate no-flush --hz 60 --load-passes 4
```

每次需要新的输出目录，已有目录会被拒绝，避免覆盖证据。默认每段 24 秒，剔除前 6 秒和最后 1 秒；GPU 样本额外避开稳态起点的混合采样窗口。图形进程每段提前 2 秒开始；Vision 初始化完成后才开始分段计时。上述命令中的 60 Hz / 4 passes 是起始测试设置，不是已验收的最大吞吐或游戏负载等价点。

`--candidate no-graph` 是诊断控制，关闭真实引擎已有的 CUDA Graph 开关。默认基线保留生产捕获的 `CopySubresourceRegion + Flush`；`no-flush` 仅在测试程序中省略 Flush。这两个选项均不修改生产配置或可执行程序。

若需重新编译，在已配置的 native 构建目录执行以下命令；环境变量转大写用于绕开该机器同时存在 PATH/Path 时的 MSBuild 问题：

```powershell
& D:/env/python/python.exe -c "import os,subprocess; raise SystemExit(subprocess.call(['C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe','--build','native/vision_native/build','--config','Release','--target','vision_graphics_load','vision_contention_benchmark','--parallel','4'],env={k.upper():v for k,v in os.environ.items()}))"
```

## 冻结的输入和判定规则

模型：`models/best_480x384.engine`，SHA-256：

`45fc56274ff3bbc659e534c3b7833065b0483ef8022ac5d7657cd6da7dbdeb21`

图片：`artifacts/vision-gpu-budget-20260907/training-fixture.bgra`，160 张 640×512 BGRA；SHA-256：

`b6993c7afa47386086b2eb3997bd3fc364e2f291e2a9ff6e7bee52c5daca7cb5`

来源与裁剪清单：同目录 `fixture-manifest.json`。复用两套训练验证集，各原始标注数量分组固定取样，中心裁剪；没有重新挑选素材。图片预先驻留 D3D11 纹理，运行时继续采用生产的 640×512→480×384 软件预处理，阈值 0.20。磁盘读取、图片解码和初次上传不计入稳态。

每次启动先写 `contract.json`，记录二进制、模型、素材和编排脚本 SHA-256、固定参数与判定规则。结束时再次核对输入身份；另存硬件身份、每段原始 CSV、逐帧检测签名、GPU 样本、摘要和 verdict。

1. **完整性**：A/A 或四段 ABBA 必须完整；缺失测量和非有限数值均无效。
2. **结果一致性**：每张来源图片必须出现，每次推理必须有对应签名，帧号和来源序列连续。所有类别、置信度和框坐标以可往返 float32 的 9 位有效数字逐帧对齐；不能只比第一轮图片。首次 A 的输出形成 oracle，B 不能修改 oracle。
3. **GPU**：稳态至少 20 个有效样本，Vision 每个样本 ≤15%。候选平均占用不得高于两次 A 平均值的 105%。
4. **环境**：当前仅支持单 NVIDIA GPU；两个 D3D11/CUDA 适配器身份必须一致。每段至少 95% SM 时钟样本处于该段中位数的 ±5%，各段中位数差异 ≤5%；未参与测试的进程合计最忙引擎 ≤3%。开始前也检查后台占用，忙碌时直接退出。
5. **重复性**：两个 A 的 Vision、图形负载平均完成时间相差 ≤5%，P99 相差 ≤15%。A/A 不稳时，B 再快也无效。
6. **候选收益**：两次 B 的平均值和 P99 都须比两个 A 至少改善 5%；Vision/图形交付频率不能低于 A 均值的 99%；图形完成时间及 Vision 结果交付间隔的 P99 不能劣化超过 5%。

退出码：0 表示完成有效评价（可能是 `CANDIDATE_REJECTED`），2 表示测量无效。是否接受必须读取 verdict 的 status；0 退出码本身不代表候选获胜。

## 已完成验证

| 验证 | 实测结果 | 结论范围 |
|---|---|---|
| 两个 Release 目标构建 | 成功 | 可运行 |
| 逐帧输出控制 | baseline 199 帧、no-flush 199 帧、no-graph 198 帧；每条路径均覆盖 160 个来源，所有签名一致 | 这批素材上的解码结果保持一致，不是召回率证明 |
| 图形负载正控制 | 1→4→1 passes 的 GPU query 平均 0.377→1.848→0.417 ms；GPU query 均有效 | 更多固定着色工作产生更多 GPU 时间；不推导游戏等价性 |
| 图像读回 | 三段 checksum 同为 7270567001682540787，图像非恒定 | 着色器实际输出了确定图像 |
| 12 项判定反例 | 全部通过 | 检测变化、漏帧、缺失遥测、超预算、外部干扰、频率变化、NaN、基线漂移和竞争负载损害不能被速度收益抵消 |
| 初版 A/A 调通 | 约 59 Hz，GPU 最大样本 10.62%；判定 INVALID | 后台《英雄联盟》占用变化，且图形负载均值漂移；不能用作收益依据 |
| 后台占用前置拒绝 | 约 3.5 秒退出，未启动两项负载 | 避免重复跑无效长测试 |

初版 A/A 还使用省略 Flush 的早期探针，因此也不作为最终基线。随后已把默认复制/提交路径对齐主线，并重新编译、完成上述三种模式的逐帧输出验证。**最终版本的有效 A/A、ABBA 和 15% 预算下吞吐上界仍待 GPU 空闲时测量。** 本轮没有声称性能提升，也没有把候选迁入生产。

主要原始证据目录：`artifacts/vision-offline-contention-20260907/` 下的 `aa-60-p4`、`load-control`、`output-control`、`preflight-rejection`。所有测试创建的进程已经结束。

验证入口：

```powershell
& D:/env/python/python.exe scripts/benchmarks/test_vision_contention.py
& D:/env/python/python.exe scripts/benchmarks/check_vision_graphics_load.py --output artifacts/load-control-new
& D:/env/python/python.exe scripts/benchmarks/check_vision_contention_outputs.py --output artifacts/output-control-new
```

## 覆盖边界与后续使用

这套测试能摆脱“开游戏、找位置、重复动作”的人工依赖，适合先筛选代码方案。**GPU 必须空闲给测试使用**；运行其他游戏、原生主程序或压力测试会污染结果，进程占用检查会拒绝它们。

它没有 DXGI AcquireNextFrame / ReleaseFrame、桌面合成、真实游戏 CPU/DX12 提交、颜色回读、完整 Vision 服务和 Controller，也没有真实 present 时间。因此报告中的 wall 是合成输入纹理复制开始到推理/解码及解映射提交完成的 CPU 时间，结果间隔是该合成串行服务的交付间隔，不能改名为游戏帧龄。

之前真实日志中的 12 ms 捕获尾延迟**尚未在这里复现**。通过该离线筛选不等于消除了该故障，也不等于游戏最终验收。图形负载是固定 GPU 工作的竞争代理，不能给出“等价某游戏多少 FPS”的换算。

先在空闲机器上完成 A/A，必要时在不看候选结果的前提下校准固定 passes/频率，然后冻结为新的 contract 再跑 ABBA。适用的后续候选是资源交接方式、提交时机、必要的流水重叠；不要为了漂亮分数改变分辨率、素材或在候选阶段动态调整背景负载。

GPU timestamp query 仅在 `Disjoint == FALSE` 且 Frequency 有效时使用，见 [Microsoft D3D11 timestamp disjoint 定义](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)。本轮保留生产 CUDA graphics mapping；未来若评估 external-memory/fence 方案，应单独验证同步所有权，参见 [NVIDIA Graphics Interoperability](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)。

建议后续把这个测试入口、冻结的 GPU 口径及已知覆盖边界同步进 `.agent-context/`，防止再次把游戏录制或 ETW 当作离线筛选的前置条件。
