> 下文为本轮实施前的扫描快照。删除已执行，当前结果以 [CLEANUP_RESULT.md](CLEANUP_RESULT.md) 和 [cleanup-execution.json](cleanup-execution.json) 为准。

# SHA256 与基准验收清理扫描

扫描日期：2026-10-04。状态：仅扫描和整理，未执行删除，未修改生产代码、测试或现有规则。

用户方向：删除项目 SHA256 校验和现有基准验收体系，后续按需重写；数字模拟测试可以保留。本轮交付删除范围分析及依赖清单，不启动重写。

## 扫描结果与文件

| 项目 | 数量 | 含义 |
| --- | ---: | --- |
| 已扫描文本文件 | 1,125 | 当前源码、脚本、文档及项目上下文 |
| SHA256 引用文件 | 171 | 共 711 个匹配行，包含生成、校验、字段、注释及历史记录 |
| 相关文件总清单 | 498 | 多种标签可重叠，不能直接用作删除列表 |
| 测试或事故文件候选 | 231 | 按名称筛选；包含普通单元测试、模拟及回归，不代表全部删除 |
| 模拟保留/拆分候选 | 50 | 初步识别，尚未逐个运行验证 |
| 执行或测量脚本候选 | 38 | 包含硬件测量，不能全当成数字模拟 |
| 人工整理的关键路径 | 67 | 按文件给出拟处理动作与原因 |

- [inventory.json](inventory.json)：逐文件路径、Git 跟踪状态、已有修改、SHA256 行号及分类标签；包含扫描排除范围。
- [proposed-actions.json](proposed-actions.json)：67 个关键路径的拟删除、修改、拆分或保留动作。这是审阅清单，不是已批准或已执行的删除 manifest。

匹配只是静态证据。例如 SHA 字段名不能证明文件在执行校验；历史文档中的 hash 也不意味着整个文档失效。

## 1. 可以独立拆除的旧验收层

这些入口的主要职责是判定旧基准或证据是否通过，数字模拟生成器不依赖其通过结论：

| 文件 | 当前职责 | 拟处理 |
| --- | --- | --- |
| `scripts/verify/compare_sustained_aimlab.ps1` | 加载 optimization policy，执行绝对门槛、同条件匹配和保护指标验收 | 删除验收器 |
| `scripts/verify/compare_oscillation_matrix.ps1` | 批量调用上述比较 | 同步删除 |
| `python/tools/verify/compare_oscillation_matrix.py` | 矩阵身份、报告 SHA、执行完整性与验收 | 同步删除 |
| `python/tools/verify/compare_state_machine_replay.py` | 冻结逐场景 trace 一致性与覆盖验收 | 删除比较，保留 trace 生成 |
| `scripts/verify/native_tracker_bodylock_refactor_acceptance.ps1` | 旧 tracker/BodyLock 指标门槛 | 删除 |
| `scripts/verify/native_runtime_performance_acceptance.ps1` | 运行 Base/Functional 与 pipeline 验收 | 删除 |
| `python/tools/native_runtime_acceptance.py` | 证据分段评分、release decision 与 SHA 身份 | 删除及其专用测试 |
| `scripts/verify/native_pipeline_contract.ps1`、同名 `.bat` | 混合静态架构检查、测试构建、BaseEndToEnd 和模拟 smoke | 拆清构建依赖后移除验收入口 |

`compare_sustained_pulse_smoothing.ps1`、`compare_axis_stress_ab.ps1` 和 Vision candidate 比较工具还承担指标整理，不宜仅凭 compare/benchmark 名称删除。它们应区分测量输出与 PASS/FAIL 判定。

## 2. SHA256 生产者、消费者和构建校验

运行时链路已确认：

```text
runtime_targets.cmake 的合同 SHA + runtime_provenance.cpp 的文件 SHA
    -> main.cpp / mouse_runtime_main.cpp
    -> RuntimeConfig 的 provenance 字段
    -> runtime_loop / session manifest / performance / telemetry
    -> 审计脚本 -> runtime profile -> AimLab CLI -> 比较验收
```

拟拆范围包括：

- `native/runtime_app/runtime_provenance.{cpp,h}` 的 SHA 文件/上下文实现和测试。
- `native/runtime_app/runtime_targets.cmake` 的合同 hash 与编译宏；运行时调用点、配置结构和日志序列化字段同步修改。
- `scripts/release/package_native_runtime.ps1` 的 manifest SHA 生成及 `verify_native_release.ps1` 的文件 SHA 比对。保留打包、文件复制与需要的启动 smoke。
- `build_mouse_link.ps1` 的下载包/解压内容校验，以及 `native/mouse_link/CMakeLists.txt`、`native/mouse_native/mouse_targets.cmake` 的 Interception DLL 固定 hash 门槛。保留依赖构建和 DLL 复制。
- profile 提取/运行、视频分析、Vision benchmark、mouse probes、训练导出工具中的 SHA 身份生成与读取，逐个修改字段契约。
- `.agents/skills/native-telemetry-audit/` 和 `incident-to-regression/` 的 SHA 验证、旧验收判定及对应测试。日志解析和数字场景生成应单独评估。

这里只是移除现有校验体系的范围分析，没有建议通过跳过异常、伪造 hash 或保留永远成功的验收入口来实现。

## 3. 功能性 SHA 不能直接连功能删掉

| 位置 | 实际用途 | 若要求所有 SHA256 实现归零 |
| --- | --- | --- |
| `python/desktop_app/settings.py:154` | 读取内容身份，保存前后检测外部修改，避免覆盖 | 用内容比较等方式替代，保留并发修改检测 |
| `python/desktop_app/runtime.py:40` | 项目级启动互斥锁名 | 换稳定锁键，保留互斥语义 |
| `python/desktop_app/gui.py:927` | UI 单实例锁名 | 换稳定锁键，保证多进程一致 |
| `python/tests/gamepad/{benchmark_scenarios,ads_benchmark_scenarios,manual_mix_inputs,ads_manual_inputs}.py` | 确定性 seed 推导 | 这是模拟输入生成；替换会改变现有场景和随机序列，必须明确接受该变化 |
| `python/tools/training/game_specialists.py` | 数据划分、样本 ID、augmentation seed；另有文件校验 | 分开处理，不能只删 import 或统一改成随机 ID |
| `python/tools/training/bf6_specialist.py` | 重复数据分组、基线身份及训练/导出记录 | 保留数据分组意图；先拆文件身份校验 |

用户说的是 SHA256“校验”，本轮未把上述 seed/锁名用途擅自解释为必须删掉的功能。若后续按“所有 SHA256 使用归零”执行，需要替代方案；直接删除配置冲突检查会使用户修改可能被覆盖。相关桌面和 BF6 文件已有未提交修改。

`state_machine_replay_tests.cpp` 的 trace digest 是 FNV 风格，`python/recoil_app/runtime.py` 使用 SHA1；它们不属于 SHA256 命中，不应为满足“SHA256 校验清理”顺带改动。

## 4. 数字模拟保留范围

优先保留或拆出：

- 原生 Sustained AimLab 的 `sustained_aimlab_{simulator,scenario,score,trace}.{cpp,h}` 与相应测试；生产控制器 adapter 和 physical input 辅助模块。
- `cod_native_aimlab_benchmark.cpp`、`cod_native_sustained_aimlab_benchmark.cpp` 的模拟运行能力；后者的 SHA CLI、身份输出与验收含义需要拆开。
- `oscillation_scan.cpp`、`state_machine_replay_tests.cpp` 的确定性随机短长场景。
- `zero_target_hold_incident.cpp`、`bodylock_motion_anchor_incident.cpp` 等数字事故场景；其中场景、断言、注册和 standalone 入口混合，需要逐文件判断。
- Python `run_gamepad_benchmark.py`、`run_mouse_benchmark.py` 及 `tests/gamepad/`、`tests/mouse/` 下共享的场景、输入、plant 和指标模块。
- `test_gamepad_adaptive_delta_gain_simulation.py`，但它执行 legacy 控制路径，保留不等于证明当前原生运行时正确。

数字模拟自身的合法参数、有限输出和确定性断言，与候选必须超过旧 baseline/通过 release gate 的验收规则用途不同。当前整理按保留模拟有效性、删除旧候选验收层划界；若要连事故 symptom oracle 一并删除，需要在逐文件清单明确，不能靠文件名代替判断。

普通配置、UI、输出编码、Vision、recoil 等单元测试属于另一个范围。本轮将其标为待审阅，没有将“基准验收测试”扩展成“项目所有测试”。CUDA/D3D11、GPU contention、鼠标传输探测是硬件测试或测量，也不是纯数字模拟。

## 5. 删除时必须联动的入口

- `native/CMakeLists.txt:151` 引入 `ProductTests.cmake`；它定义两批运行器及 10 个产品 suite，混有单元测试、事故回归和随机 replay。
- `native/CMakeLists.txt:153` 引入 `OfflineBenchmarks.cmake`；其中有八组离线辅助测试、AimLab 执行器和 smoke/CLI 测试。不能整文件删除而保留模拟能力。
- `base_tests_main.cpp`、`functional_tests_main.cpp` 的 `register_*` 声明和调用必须与保留源码一致，否则链接失败。
- `ProductTests.cmake` 中还列有 `vision_service.cpp`、`runtime_timing.cpp` 等生产实现，不能将目标源列表全部当测试文件删除。
- Python benchmark 直接 import `tests.gamepad`/`tests.mouse` 的共享模块；删 `python/tests/` 会破坏模拟入口。
- `python/tests/test_repository_layout.py` 直接 import 矩阵验收器；skill 合同测试和 profile 测试也依赖旧 SHA/schema。
- Fusion、mouse 和独立 mouse_link 的 CMake 测试注册分散在其他清单，不应遗漏，也不能自动判定全删。
- `pytest.ini` 定义测试发现目录；保留模拟测试时不应盲删 `testpaths`。

规则与文档同样需要处理：`AGENTS.md` 的 AimLab 契约、`docs/benchmarks/AIMLAB_OPTIMIZATION_CONTRACT_V1_20260827.md`、optimization policy、两项本地 skill 以及 README 的入口链接。目前仍描述旧验收流程，后续应明确退休并更新入口。用户此次方向优先于旧清理目标，不表示已授权重开低频控制器或改变手动权威。

历史报告和 `.agent-context/` 中的 SHA 是过去结果的记录，不能将新方向写成“过去验收未发生”。建议标注旧体系退休，历史是否整批删除另列范围。本轮没有修改上下文。

## 6. 本地产物统计

仅枚举文件与体积，未读取这些目录的文件内容，未提出整目录删除。

| 目录 | 文件数 | 约 GiB | 处理边界 |
| --- | ---: | ---: | --- |
| `artifacts/` | 48,870 | 11.62 | 混有模型、训练、依赖包、实验和验收结果 |
| `runs/` | 11,629 | 5.07 | 混有运行日志、配置备份和实验输出 |
| `native-test-artifacts/` | 43 | <0.001 | 小型测试结果，仍应按具体路径清理 |
| `native/build/` | 1,310 | 0.47 | 混有生产 exe、DLL、对象及测试二进制 |
| `native/vision_native/build/` | 4,653 | 0.60 | 原生 Vision 构建产物 |

`output/`、`debug_captures/` 当前为空。上述统计不包含 `.worktrees/`、`.venv/`、模型与训练数据根目录、第三方 Ultralytics；不会为删除验收而删除可运行的生产文件或用户数据。

## 7. 后续处理顺序与验证

1. 固定逐文件删除集合；以此清单区分纯验收、混合源码和保留模拟。
2. 先解除验收调用、CMake/registry 注册与 Python import 依赖，再删纯验收实现和专用 fixtures。
3. 拆掉运行时 SHA 的 producer/consumer/schema 与构建、打包字段；功能性哈希按已确定的边界保留或替换。
4. 保留并验证模拟入口，清理旧判定输出和与其专属的基线/manifest。
5. 更新现行入口、规则与退休说明；最后按具体文件处理本地产物。

执行后应验证：原生运行时及留下的模拟 targets 能构建；模拟命令仍产生数值结果；剩余测试能被发现；已删除脚本无悬空调用；源码中 SHA 残留都有明确用途。若替换 seed 派生，验证新的确定性并说明旧场景不再相同。桌面保存冲突与单实例语义若改动，必须分别验证。

本轮验证仅为文件扫描、静态依赖阅读及清单路径检查；没有运行旧验收、pytest、构建、GPU/输入探测或实机游戏。无法据此断言任一删除集合已可编译。

初始工作区的未提交改动已完整记录在 `inventory.json`；它们未被修改。新增交付仅在当前扫描目录中。

扫描期间另观察到 `native/controller_native/state_machine_contract_tests.cpp` 和 `python/tests/test_desktop_profiles_curves.py` 出现在变更状态中；本次未编辑这两个文件。清单是扫描快照，后续执行前需刷新工作区状态与受影响文件，避免覆盖其他正在进行的工作。
