# SHA256 与旧基准验收清理结果

完成日期：2026-10-04。用户授权：在避免依赖报错的前提下，最大程度删除扫描范围中的核心代码外内容。未提交 Git commit。

## 结果

已删除 **94 个文件、14,850 行原 Git 跟踪文件内容**，文件原体积共 717,741 字节。这个行数只统计整文件删除，不包含保留文件中删去的 SHA 和验收分支。

- 删除旧 AimLab、振荡矩阵、状态机 replay 比较验收器，runtime/pipeline acceptance 和专用测试。
- 删除两套项目本地审计/回归验收 skill 的源码、入口、描述及对应合同测试。
- 删除旧优化 policy、SHA manifest、RED/GREEN 验收材料、验收报告与过时实验包装脚本。
- 删除运行时文件/上下文 SHA 实现和专用测试，同步解除 CMake、运行器注册、配置结构、日志及遥测的依赖。
- 从保留的模拟矩阵入口移除基线比较、hash admission、产品验收分支和 BENCHMARK-ELIGIBLE 输出。
- 清理打包、下载依赖、训练导出和测量工具的 SHA 校验；保留必要的文件存在性、路径边界及执行错误检查。

完整逐文件执行记录见 [cleanup-execution.json](cleanup-execution.json)。实施前的 inventory/proposed-actions 为历史扫描快照，不能用其旧路径判定当前文件仍然存在。

## 保留的功能

生产 controller 的目标搜索、获取、身份、交接、手动权威、AutoFire、recoil 与输出算法未由本次清理改写。运行时改动仅涉及 SHA 身份生成及非控制用途的元数据。

保留数字场景、plant、生产控制器 adapter、轨迹输入、指标计算、状态机随机 replay、数字事故场景及普通功能单元测试。Python 模拟依赖的 `tests.gamepad`/`tests.mouse` 共享模块仍然保留。

配置保存的外部修改检测改用原始字节快照比较，保持保存前、校验后两次冲突检查。单实例锁名、确定性模拟 seed、训练数据分组和训练记录仍有 SHA256 使用；这些没有恢复旧文件验收判定。训练中的模型/manifest hash admission 已移除。

模型、训练数据、运行配置、原始日志、配置备份和完整构建目录未删除。已存在的其他 UI、controller、研究与训练工作保留；对共享的 settings 和训练文件仅修改此次 SHA 清理所需的部分。

## 依赖与格式变更

- 删除的 SHA API、CMake 源注册、功能测试 registry 声明/调用一起解除。
- 保留矩阵脚本不再接收旧 `-BaselineDir`。直接运行数值场景并保存结果。
- 旧日志 profile 提取/审计流水线删除；显式 runtime-pattern CLI 仍可使用，不再要求 SHA 参数。
- 新运行时 telemetry schema 为 **19**；session manifest 为 **3**，session metadata 为 **2**，performance summary 为 **3**。它们不再包含旧 SHA 字段；历史日志不改写。
- 发布打包 manifest 使用 schema **2**，不生成 SHA；文件检查入口兼容 schema 1/2，保留路径、存在性和大小检查。
- 当前 benchmark 文档和 `AGENTS.md` 已更新为模拟/测量用途。已删除材料的 Markdown 链接转为退休说明。

## 验证

| 检查 | 结果 |
| --- | --- |
| 原生 CMake 重新配置 | 通过，无丢失源码依赖 |
| Release 完整默认构建 | 通过，包含生产运行时、mouse、Fusion、Vision 模块和已注册目标 |
| 单独指定的 11 个原生关键目标构建 | 通过 |
| 原生 Base/Feature/Sustained CTest | 20/20 测试组通过 |
| 相关 Python 功能/模拟测试 | 两批共 144 项通过 |
| 完整 Python 测试收集 | 978 项成功收集，无删除引起的导入错误 |
| Python 全部源码 AST 与 11 个保留 CLI 的 `--help` | 通过 |
| 所有保留 PowerShell 脚本语法解析 | 通过 |
| target-hold / Python oscillation 矩阵实际执行 | 各 16 组通过 |
| POV/firing 矩阵实际执行 | 4 组通过，输出 SIMULATION_COMPLETED |
| 显式 runtime-pattern 模拟 | 不提供 SHA 参数即可运行；JSON 字段与语法核对通过 |
| 生产运行时 `--dump-effective-config` | 通过，不输出 SHA 字段 |
| `git diff --check` | 通过 |

验证日志位于当前目录；矩阵数值输出位于本地 `runs/cleanup-verification-20261004/`。数字执行通过只证明保留入口可用，不是新的优化或实机验收。

## 限制

自动审批拒绝了 PowerShell 文件删除操作，原因仅返回 `blocked by policy`。文本文件已通过明确逐文件路径的补丁删除，但以下两个生成缓存仍在：

- `.agents/skills/incident-to-regression/scripts/__pycache__/regression_contract.cpython-311.pyc`
- `.agents/skills/native-telemetry-audit/scripts/__pycache__/audit_evidence.cpython-311.pyc`

对应 skill 入口和源码均已删除，缓存不属于当前构建或正常导入链。本次没有绕过该拒绝。

未执行实机游戏、输入设备捕获、GPU workload、训练/模型导出或完整发布打包。完整 Python suite 未全部运行；已完成相关测试和全量收集。工作区有其他持续进行的修改，验证结果对应本次执行时的文件状态。

`.agent-context/` 保留历史记录，仍可能提到已删除材料。建议后续同步当前退休决策，避免将旧 handoff 当作当前验收入口。
