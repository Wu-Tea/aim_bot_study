# 技术研究报告：通过游戏播放音频识别事件方位并可视化

版本：0.1
日期：2026-06-24

## 1. 结论摘要

该项目在 C++/Windows 上可实现，但准确性由输入信息决定：

1. **第一方事件位置模式**最可靠：若能接入自有游戏或音频中间件，直接读取 emitter/listener 位置，不需要从最终混音反推。
2. **多声道 PCM 模式**次之：若得到 5.1/7.1 各通道，可依据通道能量和事件检测估计粗方位。
3. **最终立体声混音模式**最困难：只能从 ILD、ITD/GCC-PHAT、频带差和游戏 HRTF 留下的统计特征中估计；前后、高度、距离和重叠声源通常不可可靠恢复。

本项目的建议 MVP 是第三种场景的**保守版本**：指定进程音频捕获、目标事件识别、`left/center/right/unknown`、独立透明窗口。达到数据指标后再升级 4/8 方位。

## 2. Windows 音频捕获能力

Microsoft 的 Application Loopback 样例可按 PID 捕获指定进程及其子进程的播放音频，并与具体物理 endpoint 解耦；官方样例注明需要 Windows build 20348 或更新版本。[R3] 因此 MVP 直接将官方支持面收窄为 Windows 11 x64。

传统 WASAPI loopback 可捕获 render endpoint 正在播放的系统混音，但只能用于 shared-mode stream；它适合作为兼容回退或诊断模式。[R4]

推荐实现：

- 主路径：`ActivateAudioInterfaceAsync` + process loopback；
- 回退：默认 render endpoint + `AUDCLNT_STREAMFLAGS_LOOPBACK`；
- 测试：WAV replay 和 synthetic source，保证无设备也能验证核心算法。

## 3. 方向信息能否从立体声恢复

双声道输出常包含：

- **ILD**：左右耳能量差，适合判断左右偏向；
- **ITD**：左右声道相对延迟；可用 GCC-PHAT 估计；
- **频带 ILD/IPD**：不同频带的幅度与相位差；
- **HRTF 频谱线索**：游戏的双耳渲染可能保留前后/高度统计特征，但需针对具体音频设置训练。

无法假设：

- 最终混音仍能分离每个声源；
- 音量能稳定代表距离；
- 一个通用公式可跨游戏、HRTF、耳机虚拟环绕和动态范围设置准确工作；
- 同时出现两个目标声音时能得到两个独立方向。

因此系统必须输出置信度并支持 `unknown`，而不是强制给方向。

## 4. 检测和方位的推荐路线

### 4.1 阶段 A：确定性基线

- 目标：验证捕获、时间戳、离线回放、DSP 和 UI 链路；
- 检测：模板相似度、能量/瞬态门控或用户标记时间窗；
- 方位：宽带和频带 ILD，辅以限制滞后范围的 GCC-PHAT；
- 输出：left / center / right / unknown。

### 4.2 阶段 B：监督学习

训练小型 CNN/CRNN 或等价轻量模型：

- 输入：共享增益处理后的 stereo log-mel、L-R、频带 ILD、可选相位特征；
- 输出头 1：事件类别/背景；
- 输出头 2：4 或 8 个方位桶；
- 输出头 3：可选置信度/unknown；
- 部署：导出 ONNX，在 C++ 中使用 ONNX Runtime CPU 执行。

ONNX Runtime 提供 C++ API，C++ 层是 C API 的薄包装，并提供 RAII 风格对象管理。[R5]

### 4.3 阶段 C：针对游戏/配置校准

- 每个游戏音频模式、HRTF 开关、动态范围配置单独记录数据；
- 训练/验证/测试按录制 session 或地图分组，禁止随机切 clip 造成泄漏；
- 若 8 方位未达到门槛，产品保持 4 方位或左右提示。

## 5. DSP 库和许可证选择

- **KissFFT**：适合 FFT/GCC-PHAT，BSD-3-Clause，可用于闭源或开源发行。[R13]
- **ONNX Runtime**：MIT，适合作为可选推理依赖。[R5][R12]
- **miniaudio**：低层 API 支持 WASAPI loopback，可用于兼容/工具，但不能代替 process-specific API；许可证为 public domain 或 MIT No Attribution。[R6][R11]
- **Essentia**：功能丰富的 C++ 音频分析库，但官方文档注明 AGPLv3 或商业许可。默认不进入发行程序，除非项目选择相容许可或购买商业许可。[R7]
- **Dear ImGui**：适合开发面板和调试工具；官方定位更偏工具 UI，且缺乏完整无障碍能力，因此最终用户 overlay 建议采用 Win32 + Direct2D/DirectWrite，ImGui 只作为可选调试面板。[R8]

## 6. 可视化选择

Windows layered window 支持 alpha-blended 顶层窗口；Direct2D 可用于硬件加速的 2D 绘制。[R9][R10] 推荐：

- 独立顶层、无边框、可调透明度；
- 锁定时 click-through，设置时可交互；
- 不注入游戏，不 hook DirectX；
- 不实现防截图/防录屏或隐藏行为；
- 可选 OBS 插件作为直播/录制输出，OBS 官方文档说明插件通常以 C++ 动态库实现。[R14]

## 7. 工程风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 前后混淆 | 8 方位误导 | unknown 门槛；先 3/4 方位；专用数据 |
| 多声源重叠 | 单一方位不可信 | MVP 明确单一主事件；冲突时 suppress |
| 游戏音频设置变化 | 模型失配 | 配置指纹、校准集、按配置评估 |
| 独立 L/R 归一化 | 破坏 ILD | 只做共享增益/共享标准化 |
| 进程无活跃 render stream | 捕获静音 | 状态机、超时和可解释提示 |
| 依赖许可证 | 无法分发 | 生产依赖许可门禁、第三方通知 |
| 竞技公平性 | 合规/账号风险 | 授权使用条款、功能硬边界、不绕反作弊 |

## 8. 推荐结论

应立即开始的不是“完整 AI 雷达”，而是可验证的纵向切片：

1. CMake/C++20 仓库；
2. process loopback + WAV replay；
3. 统一 PCM 和 SPSC 缓冲；
4. 离线 RMS/ILD/GCC-PHAT；
5. synthetic test；
6. left/center/right 事件；
7. 外部 overlay；
8. 数据集后再引入 ONNX。

该顺序可以在最早阶段暴露最关键问题：目标音频是否含有足够的方向信息。

## 9. 参考资料

见 `docs/REFERENCES.md`。
