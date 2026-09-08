# Documentation

这里是项目文档的中央入口。先读当前状态，再按任务进入运行、benchmark、训练或历史证据。

## 从这里开始

1. [根 README](../README.md) — 启动、构建、日志和最短使用路径。
2. [Current State](project/CURRENT_STATE.md) — 当前生产架构、已验证行为、开放问题和非回归边界。
3. [Project Docs](project/README.md) — 仍在维护的项目参考文档。
4. [Benchmarks](benchmarks/README.md) — 测试合约、比较条件和有效入口。
5. [Archive](archive/README.md) — 旧基线、阶段验收、研究记录和项目历史。

## 当前系统

- [Project Overview](project/PROJECT_OVERVIEW.md) — 项目组件和主要数据流。
- [Native C++ Runtime](project/NATIVE_CPP_RUNTIME.md) — 默认原生 runtime 的构建、启动和 fallback。
- [Controller Overview](project/CONTROLLER_OVERVIEW.md) — controller 边界和运行模式。
- [Gamepad Overview](project/GAMEPAD_OVERVIEW.md) — 手柄路径和控制组件。
- [Vision Overview](project/VISION_OVERVIEW.md) — Python/native Vision 合约。
- [Native Vision](project/NATIVE_VISION.md) — TensorRT 原生 Vision 的详细实现与验证。
- [Mouse Overview](project/MOUSE_OVERVIEW.md) — 鼠标当前入口、状态与历史实现。
- [Mouse 输入替代路线](project/MOUSE_ROUTE_INPUT_REPLACEMENT_20260908.md) — 物理接管、唯一虚拟输出与正式 controller 接入门禁。

## 运行、日志与调试

- [Native Log Sessions](project/NATIVE_LOG_SESSIONS.md) — session manifest、fresh log 和清理方式。
- [Native runtime telemetry](benchmarks/native-runtime-telemetry.md) — telemetry schema 与因果日志说明。
- [Mouse 日志与压枪](project/MOUSE_DIAGNOSTICS_RECOIL_20260908.md) — 当前原生 JSONL、持续下压与慢拖回归。
- [Mouse BodyLock 范围与曲线](project/MOUSE_BODYLOCK_RANGE_CURVE_20260908.md) — 辅助范围、加速与减速。
- [Mouse 目标点控制](project/MOUSE_TARGET_POINT_CONTROL_20260908.md) — 小距离容差、速度补偿与验证结果。
- [Mouse Telemetry Debugging](project/MOUSE_TELEMETRY_DEBUGGING.md) — 历史 Python CSV 诊断。
- [Recoil Record/Replay Validation](project/RECOIL_RECORD_REPLAY_VALIDATION.md) — recoil 记录与回放验收。

## Benchmark

- [Aim Control Product Contract V1](project/AIM_CONTROL_PRODUCT_CONTRACT_V1_20260811.md) — 产品目标、用户权利、控制责任与 COD 系列适配边界。
- [Closed-loop Gameplay Acceptance V1](benchmarks/CLOSED_LOOP_GAMEPLAY_ACCEPTANCE_V1_20260811.md) — 游戏闭环仿真、事故回归与上线门槛。
- [Benchmark index](benchmarks/README.md) — 当前测试合约和比较身份要求。
- [Sustained AimLab](benchmarks/sustained-aimlab.md) — 60 秒 ADS/BodyLock 跟踪测试。
- [Vision blind window](benchmarks/vision-blind-window.md) — capture、publication、controller 与 response 时钟分离。
- [Causal response shadow](benchmarks/causal-response-shadow.md) — 因果响应 shadow 评估。

## Vision、模型与训练

- [Person Detector Training](project/PERSON_DETECTOR_TRAINING.md)
- [Training Results](project/PERSON_DETECTOR_TRAINING_RESULTS.md)
- [Valid/Train Loop](project/PERSON_DETECTOR_VALID_TRAIN_LOOP.md)
- [Roboflow Visible-Body Data](project/ROBOFLOW_VISIBLE_BODY_DATA.md)

## 可复用方法

- [Evidence-Driven Real-Time Control Optimization](project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md)
- [跨项目使用入口](methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)
- [接入 Prompt](methods/prompts/ADOPT_EVIDENCE_DRIVEN_CONTROL_OPTIMIZATION.md)

## 历史

- [历史归档](archive/README.md) — 已完成阶段和暂停研究。
- `superpowers/specs/` — 历史设计规格。
- `superpowers/plans/` — 历史实施计划。

归档与历史计划用于解释“为什么这样做”，不能覆盖当前代码、配置和
[Current State](project/CURRENT_STATE.md)。
