# AGENTS.md — SoundDirectionAssist 仓库约束

Codex 在开始任何工作前必须阅读本文件，并按以下顺序补充上下文：`README.md`、当前任务涉及的 `docs/` 文件、相关 ADR、`PLANS.md`。

## 1. 任务目标

构建一个 Windows 本地 C++ 应用，在合法授权场景中：

1. 通过公开的 Windows 音频 API 捕获指定进程树或系统播放音频；
2. 在本地识别用户配置的目标声音事件；
3. 输出低置信度可抑制的粗粒度方向估计；
4. 通过独立顶层窗口或可选 OBS 组件显示；
5. 提供可复现的离线 WAV 回放、评估和测试工具。

## 2. 不可违反的安全边界

以下内容一律禁止实现、引入、建议或留下占位代码：

- DLL/代码注入、远程线程、进程内插件、渲染 API hook；
- 读写游戏进程内存、句柄提权、内核驱动、网络包嗅探；
- 反作弊探测、绕过、规避、签名伪装、进程隐藏；
- 防截图、防录屏、`SetWindowDisplayAffinity` 等规避记录功能；
- 自动按键、鼠标控制、瞄准、路径规划或任何游戏自动化；
- 从未授权软件提取、打包或再分发音频素材；
- 默认上传音频、遥测、云推理或隐式联网。

若任务与以上边界冲突，停止该部分实现，在结果中指出冲突，并继续完成不冲突的工作。

## 3. MVP 范围

- 平台：Windows 11 x64；C++20；MSVC；CMake。
- 捕获主路径：`ActivateAudioInterfaceAsync` + process loopback；目标是指定 PID 及其子进程。
- 回退路径：共享模式 WASAPI render-endpoint loopback。
- 输入规范：内部统一为 48 kHz、双声道、float32、interleaved PCM。
- 输出规范：`DetectionEvent` + `DirectionEstimate`，由 UI 读取不可变快照。
- 首个可交付方向：`left / center / right / unknown`。
- 首个可交付检测：离线 WAV 回放中的确定性基线；ONNX 集成是后续里程碑。
- 外部显示：独立 Win32 顶层窗口；不得嵌入或注入目标进程。

## 4. 架构与实时音频规则

- 音频回调只能做格式读取、时间戳和复制到预分配 SPSC 环形缓冲区。
- 音频回调中禁止：堆分配、锁、文件 I/O、日志格式化、模型推理、等待和 COM 激活。
- 环形缓冲区溢出必须计数并丢弃最旧或最新数据，策略需记录在 ADR/代码注释中；不得阻塞回调。
- DSP、重采样、检测和方向估计在工作线程执行。
- UI 线程不得访问可变音频缓冲；通过原子序号或短锁读取不可变快照。
- 所有跨线程对象必须有明确所有权；优先 RAII、`std::jthread`、`std::stop_token`。
- 音频时间使用单调时钟或帧计数；墙钟仅用于日志。
- 任何声道归一化必须保持左右相对增益。禁止在方向估计前分别归一化 L/R。

## 5. C++ 规范

- 使用 C++20；禁止裸 `new/delete`；禁止拥有型裸指针。
- Windows COM 使用 `Microsoft::WRL::ComPtr` 或等价 RAII 包装。
- `HRESULT` 必须在边界处检查并附带上下文；不得静默忽略。
- 异常可用于非实时控制流，但不得穿过音频回调、线程入口、Win32 回调或 C ABI 边界。
- 公共 API 使用明确的强类型、`std::span`、`std::chrono` 和不可变数据结构。
- 不在头文件暴露 Windows 细节，除非该模块本身就是 Windows 适配层。
- 默认启用高警告级别并将项目代码警告视为错误；第三方代码不纳入 Werror。
- 所有用户可见文本使用 UTF-8 源文件；Windows API 边界优先宽字符。
- 代码注释解释“为什么”和约束，不复述语句。

## 6. 依赖规则

- 新生产依赖必须先更新 `docs/08_dependency_and_license.md`，说明用途、许可证、版本锁定和替代方案。
- 默认只接受 MIT、BSD-2/3-Clause、Apache-2.0 或 Windows SDK。GPL/AGPL/LGPL 依赖不得进入发行二进制，除非有书面批准和明确合规方案。
- Essentia 仅可用于独立研究环境，不得默认链接进发行程序。
- ONNX Runtime 必须可选构建：`SDA_ENABLE_ONNX=OFF` 时核心与测试仍可完整构建。
- 第三方版本必须固定；生成 `THIRD_PARTY_NOTICES.md`。
- 禁止运行时自动下载模型或依赖。

## 7. 目录和模块边界

目标目录见 `docs/02_architecture.md`。核心原则：

- `audio_capture` 只负责获得 PCM，不做识别；
- `audio_core` 负责格式、环形缓冲、时间戳和重采样；
- `dsp` 只实现可测试的纯算法；
- `detector` 不依赖 Win32/UI；
- `direction` 不依赖 Win32/UI；
- `overlay` 不读取游戏进程，只消费领域事件；
- `tools/offline_eval` 必须能在无音频设备、无窗口环境运行。

禁止形成从核心库到 `apps/` 或 `overlay` 的反向依赖。

## 8. 构建、格式和测试

Codex 应建立并保持以下命令可用：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

还应提供：

```powershell
cmake --build --preset windows-msvc-debug --target format-check
cmake --build --preset windows-msvc-debug --target lint
```

若当前里程碑尚未具备某目标，可以先提供清晰的占位目标，但不得虚假报告已执行。

## 9. 测试要求

- 每个算法先有纯单元测试，再接实时路径。
- 捕获模块必须有可注入的 fake/replay source。
- 集成测试不得依赖第三方游戏；使用程序生成的合成 PCM 和仓库自有测试 WAV。
- 测试样本不得包含受版权限制的游戏音频。
- 浮点测试使用明确容差，不比较未定义的精确位模式。
- 修复缺陷必须先添加可复现测试，除非无法自动化并在 PR 中解释。

## 10. 文档同步规则

改动以下内容时必须同步文档：

- 公共接口或线程模型：更新 `docs/02_architecture.md`；
- DSP、窗口、阈值、模型输入：更新 `docs/03_audio_and_ml_spec.md`；
- UI 状态或交互：更新 `docs/04_overlay_ui_spec.md`；
- 权限、数据保存、联网或依赖：更新 `docs/05_security_compliance.md` 和依赖清单；
- 需求或验收变化：更新 `docs/01_product_requirements.md`、`docs/06_testing_acceptance.md`；
- 不可逆技术决定：新增 ADR，不覆盖旧 ADR。

## 11. Definition of Done

一个任务只有在以下条件全部满足时才算完成：

1. 实现只覆盖当前任务范围，没有未经要求的横向扩张；
2. Debug 和 Release 构建成功；
3. 相关单元/集成测试通过；
4. 未引入实时回调中的分配、锁或 I/O；
5. 错误路径有测试或可演示处理；
6. 文档、配置示例和依赖清单同步；
7. `git diff` 中无秘密、二进制模型、游戏音频或大体积临时文件；
8. 最终报告列出：改了什么、运行了什么、结果、未解决风险。

## 12. Codex 工作方式

- 涉及两个以上模块或预计超过一次提交的任务，先按 `PLANS.md` 写执行计划。
- 先检查现状和现有测试，再修改；不要假设空仓库。
- 每个里程碑优先完成可运行的纵向切片，不一次铺满所有模块。
- 不得以“以后补测试/文档”作为完成条件。
- 遇到不明确点时采用本文档和 ADR 中最保守、最小范围的解释，并在结果中记录假设。
