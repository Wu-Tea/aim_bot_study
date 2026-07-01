# 交给 Codex 的第一条任务

你现在位于 SoundDirectionAssist 仓库根目录。

先完整阅读并遵守：

- `AGENTS.md`
- `README.md`
- `PLANS.md`
- `docs/01_product_requirements.md`
- `docs/02_architecture.md`
- `docs/05_security_compliance.md`
- `docs/06_testing_acceptance.md`
- `docs/07_delivery_backlog.md`
- `docs/adr/ADR-001-windows11-cpp20-mvp.md`
- `docs/adr/ADR-002-native-wasapi-process-loopback.md`

本次只完成 **M0 仓库基线** 和 **M1 音频管线骨架**，不要实现真实机器学习、透明 overlay、OBS 插件、安装包或多声源定位。

## 需要交付

1. 创建执行计划 `docs/plans/PLAN-<date>-m0-m1-bootstrap.md`。
2. 建立 C++20/CMake 项目、MSVC Debug/Release presets、CTest。
3. 建立文档规定的最小目录与 target，但不要创建无内容的大量空类。
4. 定义稳定的领域类型：`AudioFormat`、`AudioBlockView`、`CaptureStatus`、`DetectionEvent`、`DirectionEstimate`。
5. 实现预分配 SPSC 音频环形缓冲区，单元测试覆盖 wrap、overflow、underflow、时间戳连续性。
6. 实现 `IAudioSource` 抽象与两个非设备源：
   - 合成立体声 source；
   - WAV replay source，支持 48 kHz stereo PCM 测试资产。
7. 实现基础 PCM 转换到内部 `48 kHz / stereo / float32 interleaved` 的接口；首版可以仅支持测试中需要的输入格式，并对不支持格式返回明确错误。
8. 为 Windows process-loopback 模块建立可编译的边界和生命周期接口；若真实捕获无法在当前环境验证，可先完成最小实现或受编译开关保护的实现，但不得伪造运行成功。
9. 提供一个 CLI smoke tool：从 synthetic 或 WAV source 读取，打印帧计数、RMS、左右声道能量和 overflow 统计。
10. 添加 `THIRD_PARTY_NOTICES.md`、依赖清单、格式化/静态检查入口和 CI 基线。
11. 更新相关文档，运行全部可用构建与测试。

## 约束

- 不得注入、hook、读写其他进程内存或实现任何反作弊相关能力。
- 音频回调/生产者路径不得分配、加锁、写文件或格式化日志。
- ONNX、Dear ImGui、OBS、Essentia 本次不加入生产依赖。
- 测试音频必须程序生成或由仓库自行创建，不得包含游戏素材。
- 不要把未运行的测试描述为通过。

## 完成报告格式

最后报告：

1. 变更摘要；
2. 关键设计决定；
3. 实际执行的构建/测试命令及结果；
4. 未能验证的 Windows 设备相关部分；
5. 下一步只建议 M2，不要直接开始 M2。
