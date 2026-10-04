# 视觉能力概览

当前只有 C++ 原生视觉路径。Python 视觉、pybind 桥接、训练及分析脚本已于 2026-10-04 按用户授权退役；旧实现和说明可从 Git 历史查看。

视觉负责从桌面图像生成当前目标证据，控制器负责把证据与物理输入组合成输出。主要入口是 `cod_native_runtime.exe`；独立设备诊断入口是 `vision_native_debug.exe`。

链路为 DXGI 捕获 → CUDA 预处理 → TensorRT 推理 → 检测解码 → 目标选择 → VisionService 最新快照 → RuntimeLoop 的 VisionDeliveryGate → NativeGamepadController。Fusion 输入与本地引擎通过现有 Adapter 汇入同一控制边界。

VisionEngine 拥有捕获、推理和选择状态；VisionService 拥有工作线程及最新快照；DeliveryGate 判断来源、帧身份和年龄。控制器中的 TargetCoordinator 拥有目标身份及生命周期。无新帧不等于新鲜空目标：前者不刷新证据，后者释放通用目标权限。弱证据和可见 cue 延续只有瞄准权限，开火仍要求直接观测证据。

配置从原生 loader 进入 runtime。控制数值热更新保留学习；视觉策略更新在提交边界清除相应学习；引擎或设备结构变化要求重启。停止时先结束工作线程，再释放引擎、捕获及输出资源。

构建和入口见 [仓库说明](../../README.md)，具体字段与源码见 [Native Vision](NATIVE_VISION.md)，状态归属和验证限制见 [当前项目模型](CURRENT_STATE.md)。离线仿真和输出关闭的设备检查不能证明实际游戏体验。
