# FPS 2D Target Memory / Tracker Package

这个包是一个面向 **低延迟 FPS 视觉-控制系统** 的 C++20 参考工程。它把 YOLO/TensorRT 在屏幕中心 ROI 上输出的 2D detector boxes，与最终送出的 gamepad right-stick output 结合起来，做短期 target memory、预测、association、ego-motion compensation、recoil visual compensation，以及 observed-only fire authority。

核心约束：

- 只做 **2D screen-space tracking**，不做 3D 重建。
- tracker 可以给 controller 提供 ADS/bodylock/snap 目标状态。
- predicted-only / coasting target **不能给 fire authority**。
- controller loop 目标：100–160Hz。
- vision update 目标：90–120Hz，可处理 detector latency 和 occasional missing frames。
- ego-motion compensation 使用 **final right stick output**，也就是 clamp/mix/deadzone 之后真正送给设备/游戏的输出。

## 目录

```text
.
├── DESIGN.zh-CN.md                 # 完整方案说明
├── CMakeLists.txt
├── include/fps_tracker/
│   ├── math.hpp                    # Vec2/Box2/Mat2 等轻量数学类型
│   ├── types.hpp                   # Detection/VisionFrame/TrackSnapshot/Config
│   ├── projection_model.hpp        # screen px <-> track coordinate
│   ├── stick_projector.hpp         # final stick -> screen/track rate
│   ├── recoil_visual_model.hpp     # recoil visual kick model
│   ├── ego_motion_buffer.hpp       # cumulative E(t)
│   ├── kalman_cv2d.hpp             # constant-velocity Kalman filter
│   ├── association.hpp             # detector-to-track gated association
│   └── target_tracker.hpp          # TargetTracker public API
├── src/                            # 实现文件
├── examples/synthetic_replay.cpp   # synthetic benchmark / replay 示例
└── tests/tracker_invariants.cpp    # authority invariant 检查
```

## 构建

```bash
cmake -S . -B build
cmake --build build -j
```

运行示例：

```bash
./build/synthetic_replay
./build/tracker_invariants
```

## 快速使用

```cpp
#include <fps_tracker/target_tracker.hpp>

fps::TrackerConfig cfg;
fps::TargetTracker tracker(cfg);

// 1. 每个 control tick 推入最终 right-stick 输出。
fps::ControlSample sample;
sample.applyTime = now;
sample.finalRightStick = {0.12, -0.03};
sample.mode.ads = true;
tracker.pushFinalControlSample(sample);

// 2. TensorRT 结果 ready 时，用真实 captureTime ingest vision frame。
fps::VisionFrame frame;
frame.frameSeq = seq;
frame.captureTime = capture_time;
frame.readyTime = now;
frame.mode = sample.mode;
frame.detections = detections;
tracker.ingestVisionFrame(frame);

// 3. controller loop 查询 tracker snapshot。
auto out = tracker.query(now, {});
if (out.hasSelection) {
    const auto& s = out.selected;
    // s.assistAuthority 可用于 ADS/bodylock
    // s.fireAuthority 必须是 ObservedOnly 才能进入 fire decision
}
```

## 重要 invariant

代码里保留了这些设计约束：

```text
predictedOnly == true  => fireAuthority == None
missStreak > 0         => fireAuthority == None
no backing detection   => fireAuthority == None
fire authority         => latest usable vision frame 中有 direct observation token
final stick saturated  => ego compensation uses saturated final output
```

这个工程是参考实现，不包含任何游戏专用内存读写、外部进程注入或反作弊绕过逻辑。你需要用自己的 capture、detector、gamepad report、weapon/recoil calibration、offline benchmark pipeline 对接。
