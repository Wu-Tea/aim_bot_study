# 目标架构

## 1. 系统上下文

```text
Target process / Windows render endpoint
                |
                v
        IAudioSource (Win32 adapter)
                |
                v
       preallocated SPSC ring buffer
                |
                v
 format normalize / resample / frame / timestamp
                |
                +------------------+
                |                  |
                v                  v
        event detector       direction estimator
                |                  |
                +--------+---------+
                         v
                event fusion + smoothing
                         |
                         v
              immutable presentation snapshot
                         |
              +----------+-----------+
              v                      v
       external overlay        offline evaluator
```

## 2. 线程模型

### T0：UI/Main

- Win32 message pump；
- 配置、source 生命周期；
- 读取最新 `PresentationSnapshot`；
- 不持有音频写入缓冲。

### T1：Capture callback/producer

- 从 WASAPI 获得 PCM；
- 记录 frame position/QPC；
- 将数据复制进预分配 SPSC buffer；
- 更新无锁统计；
- 禁止锁、分配、I/O、推理。

### T2：Audio worker

- 读取 SPSC；
- 转为内部格式；
- 重采样、分帧、窗函数和特征；
- 调用 detector/direction；
- 产生候选事件。

### T3：Inference worker（M5，可选）

- 若 ONNX 推理可能阻塞 audio worker，则使用有界队列；
- 队列满时丢旧帧而不是积累无限延迟；
- 模型异常转为 degraded，不使进程崩溃。

## 3. 领域类型

```cpp
struct AudioFormat {
  uint32_t sample_rate_hz;
  uint16_t channels;
  SampleType sample_type;
  ChannelLayout layout;
};

struct AudioBlockView {
  AudioFormat format;
  uint64_t first_frame_index;
  std::chrono::nanoseconds monotonic_time;
  std::span<const std::byte> bytes;
};

struct DetectionEvent {
  EventClassId class_id;
  float probability;
  uint64_t onset_frame;
  uint64_t offset_frame;
};

struct DirectionEstimate {
  DirectionBin bin;          // Unknown, Left, Center, Right, ...
  std::optional<float> azimuth_deg;
  float confidence;
  DirectionMethod method;
};

struct PresentationSnapshot {
  uint64_t sequence;
  CaptureStatus capture_status;
  std::optional<DetectionEvent> detection;
  std::optional<DirectionEstimate> direction;
  Diagnostics diagnostics;
};
```

实现可调整命名，但语义和模块边界不得弱化。

## 4. 接口

```cpp
class IAudioSource {
 public:
  virtual ~IAudioSource() = default;
  virtual Result<AudioFormat> Start(AudioSink& sink, std::stop_token stop) = 0;
  virtual void Stop() noexcept = 0;
  virtual CaptureStatus status() const noexcept = 0;
};

class IEventDetector {
 public:
  virtual ~IEventDetector() = default;
  virtual std::optional<DetectionEvent> Process(const FeatureFrame&) = 0;
};

class IDirectionEstimator {
 public:
  virtual ~IDirectionEstimator() = default;
  virtual DirectionEstimate Estimate(const StereoFrame&, const DetectionEvent&) = 0;
};
```

`AudioSink` 的热路径应是无锁/无分配写入，错误通过计数和状态面传出。

## 5. 目标仓库布局

```text
/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  AGENTS.md
  PLANS.md
  apps/
    sda_app/
  libs/
    domain/
    audio_capture_win/
    audio_core/
    dsp/
    detector/
    direction/
    fusion/
    overlay_win/
    config/
  tools/
    audio_smoke/
    offline_eval/
    dataset_check/
  tests/
    unit/
    integration/
    assets/generated/
  models/
    README.md
  samples/
    README.md
  docs/
```

`models/` 和 `samples/` 默认只含说明，不提交受限制模型或游戏音频。

## 6. 数据流默认参数

- 设备输入：接受 WASAPI mix format；
- 内部格式：48,000 Hz、stereo、float32、interleaved；
- 基础 DSP frame：1024 samples；
- hop：480 samples（10 ms）；
- 方向分析窗：20-100 ms，按算法配置；
- 检测窗：由 baseline/model 决定，目标不超过 250 ms onset 延迟；
- UI snapshot：最多 60 Hz，不要求每个 audio hop 重绘。

参数必须来自有 schema 的配置并记录到评估结果。

## 7. 生命周期和错误

### Source startup

1. 验证 OS/build；
2. 解析 PID；
3. 创建 capture object；
4. 激活接口；
5. 取得 mix format；
6. 分配缓冲；
7. 启动 capture；
8. 发布 `Capturing`。

### Shutdown

- 先停止新回调；
- 发送 stop；
- 等待 worker 有界退出；
- 释放 COM/device；
- 清理快照；
- 发布 `Idle`。

### 错误原则

- 每个 `HRESULT` 带操作名和十六进制码；
- 对用户显示可行动信息，不显示堆栈；
- 目标进程无 render stream 是状态，不是崩溃；
- 设备变更可重启，重试必须有退避和上限；
- ring overflow 可降级但必须计数。

## 8. 配置与持久化

- 配置 JSON，验证 `schemas/app_config.schema.json`；
- 默认写入 `%LOCALAPPDATA%/SoundDirectionAssist/`；
- 不自动保存原始音频；
- 评估输出包含 app/build/model/config hash；
- 迁移使用 `schema_version`，旧配置不静默误读。

## 9. 可替换性

所有检测/方向算法通过接口注入；同一离线回放应能运行：

- `baseline_detector + ild_direction`；
- `onnx_detector + ild_direction`；
- `onnx_multitask_detector_direction`。

这样可以把“捕获是否正确”和“模型是否有效”分开验证。
