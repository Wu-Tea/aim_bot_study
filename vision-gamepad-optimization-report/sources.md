# 资料来源

## 外部参考

1. 美团技术团队，《Java线程池实现原理及其在美团业务中的实践》
   - 链接：https://tech.meituan.com/2020/04/02/java-pooling-pratice-in-meituan.html
   - 用途：作为文章结构参考。本文借鉴的是“背景 -> 问题 -> 方案演进 -> 风险治理”的写法，不复用其具体内容。

2. NVIDIA TensorRT Best Practices
   - 链接：https://docs.nvidia.com/deeplearning/tensorrt/10.16.1/performance/best-practices.html
   - 用途：支撑报告中关于推理性能、benchmark、profiling 和硬件/软件环境的背景判断。

3. Microsoft Desktop Duplication API
   - 链接：https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api
   - 用途：支撑 DXGI/D3D11 桌面帧获取、GPU surface 和逐帧更新处理相关背景。

4. A Case Study of First Person Aiming at Low Latency for Esports
   - 链接：https://arxiv.org/abs/2105.10498
   - 用途：支撑 FPS 瞄准对本地输入到输出延迟敏感的背景。

5. Simple Online and Realtime Tracking
   - 链接：https://arxiv.org/abs/1602.00763
   - 用途：作为多目标跟踪方案的代表资料，用来解释为什么 full MOT 是可选方向但不是当前优先级。

6. Simple Online and Realtime Tracking with a Deep Association Metric
   - 链接：https://arxiv.org/abs/1703.07402
   - 用途：作为 ReID / deep association 路线的代表资料，用来说明身份重识别会引入额外特征和实时成本。

7. OpenCV Optical Flow
   - 链接：https://docs.opencv.org/4.x/d4/dee/tutorial_optical_flow.html
   - 用途：作为光流运动估计的背景资料，用来说明它与当前 controller-side projection 是不同层级的方案。

8. The Effectiveness (or Lack Thereof) of Aim-Assist Techniques in First-Person Shooter Games
   - 链接：https://rodrigov.ca/wp-content/uploads/2016/03/aim-assist-cameraReadychi2014-v8-final.pdf
   - 用途：支撑 aim assist 在真实 FPS 场景里会受到地图元素、玩家感知和场景复杂度影响，不能只按静态目标场景判断。

9. Supporting Aim Assistance Algorithms through a Rapidly Trainable, Personalized Model of Players' Spatial and Temporal Aiming Ability
   - 链接：https://equis.cs.queensu.ca/~equis/pubs/2023/schneider-chi-2023.pdf
   - 用途：支撑 aim assist 需要考虑玩家空间和时间瞄准能力，而不是只看目标点距离。

## 项目内证据

1. `.agent-context/handoff.md`
   - 用途：确认当前版本目标、已实现能力、验证记录和后续风险。

2. `.agent-context/decisions/`
   - 用途：确认黄色 cue、native hotpath、external cue、weak association、authority gating 等设计取舍。

3. Git 历史中的最新相关版本
   - `c4c9982 Improve native target authority and gamepad hold`
   - `9473ca2 Guard stale target escape with yellow cue`
   - 用途：确认 weak association / authority gating 已经成为已提交事实，并确认宽低框黄点 double check 是最新实现的一部分。

4. `docs/project/NATIVE_VISION.md`
   - 用途：确认 native vision 的边界、阶段和当前运行方式。

5. `docs/project/GAMEPAD_OVERVIEW.md`
   - 用途：确认 gamepad host、插件链、ADS snap、body lock、auto-fire、recoil 的角色。

6. `docs/project/GAMEPAD_CONTROLLER_GOAL.md`
   - 用途：引用 gamepad benchmark 的手感控制结果。

7. 当前源码
   - `native/vision_native/src/target_selector.cpp`
   - `native/vision_native/include/vision_native/target_selector.h`
   - `native/vision_native/src/vision_engine.cpp`
   - `native/vision_native/src/aim_enhancement.cpp`
   - `vision/targeting.py`
   - `vision/native_runner.py`
   - `vision/perf.py`
   - `controllers/base_controller.py`
   - `controllers/gamepad_controller.py`
   - `controllers/gamepad/ai_aim.py`
   - `controllers/gamepad/target_tracker.py`
   - `controllers/gamepad/auto_fire.py`
   - 用途：校准报告里的参数、权限、宽低框判断、开火 gate 和运行边界。

8. 当前测试
   - `tests/test_native_vision_targeting_bridge.py`
   - `tests/test_vision_targeting.py`
   - `tests/test_native_vision_runner.py`
   - `tests/gamepad/test_gamepad_ai_aim_plugin.py`
   - `tests/gamepad/test_gamepad_auto_fire_plugin.py`
   - `tests/gamepad/test_gamepad_controller_host.py`
   - `tests/gamepad/test_gamepad_target_tracker.py`
   - 用途：确认 selector、runner、gamepad authority、projection、auto-fire 等规则有回归覆盖。
