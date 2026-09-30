# Python 与原生 runtime 目录整理

日期：2026-09-30。范围：源码、构建入口、启动与验证路径；保留本机资产和历史证据。

原生 runtime 从 Vision 模块逐步扩展而来，原来的
`native/vision_native/CMakeLists.txt` 实际构建整个系统，产物也落在该模块下。
Python 应用、控制器、配置代码、训练支持和工具则分散在项目根目录。

这次整理将完整原生构建提升到 `native/`，将 Python 应用代码、工具和测试
集中到 `python/`。源码目录不再决定配置、模型和日志的资源根。

## 目录与入口

| 原位置 | 当前位置 |
| --- | --- |
| 根目录 Python 包、`main.py`、`controller.py` | `python/` 下的同名模块和包 |
| `tools/*.py` | `python/tools/*.py` |
| `scripts/{benchmarks,training,verify}/*.py` | `python/tools/{benchmarks,training,verify}/*.py` |
| `tests/`、`tools/tests/` | `python/tests/`、`python/tools/tests/` |
| `requirements.txt` | `python/requirements.txt` |
| 完整 runtime 的 `native/vision_native/CMakeLists.txt` | `native/CMakeLists.txt` 与按职责拆分的 target 清单 |
| Vision 核心库 | `native/vision_native/CMakeLists.txt`，作为顶层子模块 |
| 当前构建输出 `native/vision_native/build/Release/` | `native/build/Release/` |
| `tools/build_native_vision.ps1` | `tools/build_native_runtime.ps1` |

详细模块职责见 [Python 入口](../../python/README.md)、
[原生入口](../../native/README.md) 和 [根 README](../../README.md)。

根目录 `启动助手.vbs` 和 `scripts/launch/` 继续是 Windows 使用入口。
启动脚本配置当前进程的 `PYTHONPATH`，Python 工具直接执行时按自身位置定位
源码与项目资源。`python/project_paths.py` 为应用模块提供统一资源路径。

根目录的 `config.toml`、旧配置输入、模型、数据集、日志、实验资产和 worktree
保留原位。本机配置及原有两个未跟踪启动辅助脚本的 SHA-256 保持一致。
原有未跟踪输入测试随测试目录搬迁，只调整项目根定位；已恢复并核对其原始
内容哈希，保留在本地验证目录。

历史 `vision_native/build*` 保留。旧 CMake cache 绑定旧源码入口，不能直接搬到
新目录复用；当前程序从新入口重新构建。历史文档、事故证据包内的独立审计
脚本和 `.agents/skills/` 内的技能脚本保持各自的归属。

## 迁移时的验证结果

- 新目录完整 Release 构建成功，本机沿用 CUDA 架构 `75`、TensorRT 和 Python
  SDK。ViGEm 开启，离线 benchmark 注册按构建脚本显式选项控制。
- 原生 CTest：35/35 通过，包括 Base、Feature、鼠标和 Fusion 契约。
- 新旧工程中的 Vision、控制器、手柄 runtime、两个产品 test runner、Fusion
  Canvas 和鼠标 runtime 的 C++ 编译源集合一致。控制算法源码未修改。
- 新旧可执行文件读取同一 `config.toml`，有效配置输出仅构建 commit 和
  executable hash 不同，其余输出一致。
- 新位置 Python 原生扩展的 TensorRT 推理 smoke 通过，输入 `480x384`。
- 启动与目录相关验证：95 项通过，覆盖项目外目录调用、GUI 启动预览、Python
  fallback、训练 CLI、recoil 导入、Windows 启动脚本与工具测试。
- Python 全量验证：967 通过、1 跳过、2 失败。搬迁前基线为 956 通过、1 跳过、
  3 失败；新收集了 3 项目录验证与 7 项原先未默认收集的工具测试。
- `git diff --check` 与 Python 源码语法检查通过。

全量验证剩余两类问题均在搬迁前有对应失败证据：Tk 初始化时报告缺少 Tcl/Tk
库文件（失败的 widget 用例会变化）；原生桥接的尸体形状目标替换断言失败。
没有调整这些用例的 oracle，也没有在这次目录整理中修改目标选择行为。

## 提交前复核

复核发现并修复两处搬迁后的相对路径遗漏：

- `python/tools/verify/compare_oscillation_matrix.py` 原先假设 PowerShell
  比较器与 Python 文件同目录，现改为定位根目录 `scripts/verify/`。新增用例
  在修复前失败、修复后通过，并核对实际交给子进程的脚本路径。
- `python/tests/test_compare_axis_stress_ab.ps1` 的轴向比较脚本定位补上
  搬迁新增的目录层级；Pester 实际运行从失败变为 1/1 通过。

保留 native 启动检查对 `main.py` 的通用禁止条件，避免路径替换将其意外
收窄到某一种写法。没有修改控制器、目标选择或 C++ 生产算法源码。

最终入口与工具相关 Python 验证为 96 项通过；全量 Python 验证为 964 项
通过、1 项跳过、1 项失败，未收集原有未跟踪的退役输入实验。剩余失败为
`test_corpse_shape_does_not_authorize_challenger_replacement`。分别指定旧、新
位置的原生扩展后，该断言均失败，确认它不是本次目录迁移新增的回归。
此前 Tcl/Tk 初始化失败在最后一次全量运行中未复现，原因尚未确认。

暂存区的 `git diff --cached --check` 通过。提交不包含本机配置、构建产物、
模型、日志、原有三个未跟踪实验，以及复核期间新增的独立研究文档。

本机 HidHide 原先仅允许旧 runtime 路径。用户授权单独登记新路径后，登记
尝试因 CLI 读状态被驱动拒绝访问而停止；当时配置窗口仍在运行，管理员
启动被取消。没有执行登记或修改设备设置，允许列表的最终状态未验证。
随后用户反馈自行运行测试“貌似没问题”，并明确允许提交本次目录整理。
该反馈未附测试入口、日志或 HidHide 状态证据，按用户试运行反馈记录。

本次未做实战 A/B。新的可执行文件是当前工作树的重新构建，构建身份已变化；
目录与构建验证不扩展此前的 gameplay 接受范围。

本机验证记录位于 `runs/layout-reorganization-20260930/`，包括迁移清单、构建
与测试日志、配置对比及路径改动索引。原始日志、模型和回退构建未清理。
