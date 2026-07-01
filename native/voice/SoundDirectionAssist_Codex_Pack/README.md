# SoundDirectionAssist (SDA) Codex 开工包

版本：0.1
日期：2026-06-24
状态：需求与架构基线，可开始 M0/M1

## 项目一句话

在**自有游戏、离线研究、无障碍辅助或明确授权**场景中，从 Windows 播放音频中识别指定声音事件，估计其粗粒度方位，并通过独立外部窗口或可选 OBS 组件进行可视化。

## 先读什么

1. `AGENTS.md`：Codex 必须遵守的硬约束。
2. `CODEX_START_PROMPT.md`：第一次交给 Codex 的任务文本。
3. `docs/01_product_requirements.md`：需求编号、范围和非目标。
4. `docs/02_architecture.md`：模块、线程模型和接口边界。
5. `docs/06_testing_acceptance.md`：何时算完成。
6. `docs/07_delivery_backlog.md`：里程碑和任务顺序。

## 包内文件

| 文件 | 用途 |
|---|---|
| `AGENTS.md` | 仓库级 Codex 约束、编码规范和 Definition of Done |
| `PLANS.md` | 中大型改动的执行计划规则 |
| `CODEX_START_PROMPT.md` | M0/M1 首次开工提示词 |
| `CONTRIBUTING.md` | 人类与代理共同开发规则 |
| `docs/00_research_report.md` | 技术可行性、方案比较与结论 |
| `docs/01_product_requirements.md` | PRD、功能/非功能需求、范围 |
| `docs/02_architecture.md` | 目标架构、线程、接口和目录 |
| `docs/03_audio_and_ml_spec.md` | 音频 DSP、检测、方位、数据与模型规范 |
| `docs/04_overlay_ui_spec.md` | 外部显示层和交互规范 |
| `docs/05_security_compliance.md` | 安全、隐私、公平性与禁止事项 |
| `docs/06_testing_acceptance.md` | 测试矩阵、指标与发布门禁 |
| `docs/07_delivery_backlog.md` | M0-M6 任务拆分 |
| `docs/08_dependency_and_license.md` | 依赖选择与许可证约束 |
| `docs/adr/*` | 已确认的架构决策 |
| `schemas/*` | 数据集清单与应用配置 JSON Schema |

## 当前基线决定

- MVP：Windows 11 x64、C++20、CMake、MSVC。
- 主捕获：Windows Application Loopback，按 PID 捕获进程树音频。
- 兼容回退：WASAPI render-endpoint loopback，仅捕获系统混音。
- 显示：独立顶层透明窗口；不注入游戏、不挂钩渲染 API。
- 算法顺序：先离线回放和确定性基线，再接 ONNX 模型。
- 方向能力：先左右/中间，再 4 方位，最后才评估 8 方位。
- MVP 不支持同时定位多个重叠声源，也不承诺前后/高度/距离精确度。

## 第一次启动 Codex

在仓库根目录放入本包内容，然后将 `CODEX_START_PROMPT.md` 的全文作为第一条任务。Codex 应只完成 M0 和 M1，不应一次性实现 UI、机器学习和安装包。

## 合法与公平性边界

本项目不允许实现或讨论：进程内注入、DLL 注入、DirectX/Vulkan hook、读写游戏内存、驱动、网络包解析、反作弊绕过、隐藏窗口或规避录屏、自动瞄准/自动输入。所有样本与目标软件必须由使用者拥有或获得明确授权。
