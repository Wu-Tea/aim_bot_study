# 测试与验收规范

## 1. 测试层级

### 单元测试

- SPSC ring buffer：wrap、overflow、underflow、并发压力；
- PCM convert：int16/float32、interleaved、clamp、NaN；
- resampler：采样数和频率保持；
- window/STFT/log-mel；
- ILD：已知左右增益；
- GCC-PHAT：已知样本延迟；
- smoothing/hysteresis/cooldown；
- JSON schema/config migrations；
- model metadata validation。

### 集成测试

- synthetic source -> pipeline -> direction；
- WAV replay -> deterministic event timeline；
- capture start/stop/cancel；
- source silence、process exit、device error；
- ONNX disabled build；
- optional ONNX smoke with tiny repository-owned test model。

### 系统测试

- Windows 11 指定进程 capture；
- endpoint fallback；
- 30 分钟稳定性；
- DPI、多显示器、全屏窗口旁 overlay；
- sleep/resume、默认设备切换；
-目标进程重启。

## 2. 合成测试信号

仓库测试资产应程序生成：

- 双声道 impulse，R 延迟 N samples；
- 左/右不同 gain 的 tone/noise；
- 中心相同信号；
- 静音、极低能量、clipping、NaN/Inf；
- 两个冲突方向的频带组合；
- 连续事件和短瞬态。

生成脚本和 seed 必须提交，二进制 WAV 可由 CI 生成。

## 3. 实时安全验证

- 在 capture producer 路径启用测试分配计数器；
- 音频回调不得发生 heap allocation；
- 不得持有 mutex；
- callback duration 记录 p50/p95/max；
- overflow 不是 silent failure；
- worker 变慢时队列保持有界。

## 4. 指标计算

### Detection

- event precision/recall/F1；
- onset tolerance 默认 100 ms，可配置；
- false positives per minute；
- 分 profile 和环境报告。

### Direction

- confusion matrix；
- macro F1；
- left/right sign accuracy；
- unknown coverage；
- accepted-only accuracy；
- 可选 circular MAE。

不得只报告被接受样本准确率而隐瞒大量 unknown；两者必须同时给出。

## 5. 性能指标

在发布报告中记录参考硬件，而不是宣称绝对跨机器指标：

- capture callback p95；
- audio worker p95 每 hop；
- inference p50/p95；
- onset-to-render p50/p95；
- CPU、内存；
- overflow count；
- dropped inference frames。

目标：受控数据上 onset-to-render p95 <= 250 ms，30 分钟 overflow = 0 或有明确已解释的压力测试例外。

## 6. 里程碑验收

### M0

- Debug/Release build；
- CTest；
- presets/CI/style；
- 文档与第三方通知。

### M1

- synthetic/WAV source；
- 统一 PCM；
- SPSC 测试；
- audio smoke CLI；
- process capture 接口至少可编译并对不支持环境给出诚实状态。

### M2

- ILD/GCC-PHAT 纯算法测试；
- left/center/right/unknown；
- WAV replay 结果可复现；
- 冲突信号输出 unknown。

### M3

- target detector baseline；
- cooldown/hysteresis；
- 离线评估工具；
- 不使用游戏音频测试资产。

### M4

- 独立 overlay；
- lock/click-through；
- DPI/多显示器；
- 不注入，不防截图。

### M5

- ONNX optional build；
- metadata validation；
- CPU inference；
- failure degradation；
- 数据集指标达到产品门槛，否则保持 baseline/低维方向。

### M6

- 稳定性、隐私、依赖、签名/安装、发布说明、SBOM；
- 竞技使用风险说明；
- clean-machine install/uninstall。

## 7. 发布阻断项

- 任意注入/hook/反作弊/防录屏能力；
- 默认联网或保存原始音频；
- 未审查的 GPL/AGPL 生产依赖；
- 没有 unknown 的强制方向输出；
- 只有实时测试、没有离线可复现测试；
- 模型/配置与评估特征不匹配；
- 未运行却声称通过的测试；
- 仓库含未经授权游戏素材。
