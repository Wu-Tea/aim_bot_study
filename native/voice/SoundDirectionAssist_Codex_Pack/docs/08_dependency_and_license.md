# 依赖与许可证策略

## 1. 原则

- 核心尽量依赖 Windows SDK 和标准库；
- 生产依赖必须固定版本并记录许可证；
- 新依赖需说明为何不能用现有代码/标准库实现；
- 不把研究依赖直接带入发行程序；
- 运行时不下载依赖或模型。

## 2. 推荐依赖

| 依赖 | 用途 | 许可证 | 状态 |
|---|---|---|---|
| Windows SDK / WASAPI | 音频捕获、Win32 UI、Direct2D | Microsoft SDK 条款 | 必需 |
| KissFFT | FFT、GCC-PHAT | BSD-3-Clause | 推荐 |
| Catch2 v3 | 单元测试 | BSL-1.0 | 推荐，仅开发 |
| nlohmann/json | 配置和指标 JSON | MIT | 推荐 |
| spdlog | 非实时日志 | MIT | 可选；不可在 callback 使用 |
| ONNX Runtime | 模型推理 | MIT | M5 可选，默认可关闭 |
| miniaudio | WAV/兼容 loopback 工具 | Public domain 或 MIT-0 | 可选 |
| Dear ImGui | 开发诊断面板 | MIT | 可选，仅工具/调试 |

精确版本在 `vcpkg.json` 和 baseline 中固定，不在本文件写“latest”。

## 3. 默认排除

### Essentia

官方文档说明为 AGPLv3，另有商业许可。除非项目许可与 AGPL 相容或取得商业许可，否则不得链接进发行二进制。可在独立研究环境评估，但研究输出必须能由发行栈复现。

### FFTW

常见发行许可为 GPL（另有商业许可），因此默认不选。使用 KissFFT 或另经批准的宽松许可实现。

### 未审查模型和代码片段

- 不从论坛/博客复制无许可证代码；
- 不接受来源不明的预训练模型；
- 不将游戏资产作为测试 fixture。

## 4. 依赖引入清单

新增依赖时 PR 必须包含：

- 名称、版本/commit、官方来源；
- 许可证和 NOTICE 要求；
- 静态/动态链接方式；
- 发行物中包含哪些文件；
- 安全维护/更新方式；
- 关闭或替代方案；
- 对 build size、startup、runtime 的影响。

## 5. 构建开关

```text
SDA_ENABLE_ONNX=OFF
SDA_ENABLE_IMGUI=OFF
SDA_ENABLE_OBS=OFF
SDA_BUILD_TOOLS=ON
SDA_BUILD_TESTS=ON
```

最小核心构建不得被可选依赖破坏。
