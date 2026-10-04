# 测试代码精简调查：先划清使用范围，再决定删多少

调查日期：2026-09-23。源码基线：`dev`，`e68f76eb5386866ab1ef8188af8718d9aa16db43`。本轮只调查、运行现有测试并形成报告，没有修改或删除生产代码、测试代码和构建配置。

当前最有依据的处理顺序是：先清理已经退出生产链的旧功能测试，再减少原生测试的重复辅助代码。现有证据不足以支持“保持当前产品保护能力，同时删除至少 30% 测试代码”的承诺。按本报告的原生源码口径，测试及辅助代码共 **27,957 行**，30% 至少需要净减少 **8,388 行**；目前最明确的一组退役候选只有 **832 行，约 2.98%**，还需要同步核对构建和验证脚本。

这里的“候选”表示已有证据值得进入精简实施，并不表示删除后的等价性已经得到验证。本轮未做删除试验，也没有用故障注入证明剩余测试足以替代被删测试。

**Python 路径还在不在用**

当前默认手柄入口将未指定的 `GAMEPAD_RUNTIME` 设为 `native`，继而启动 `cod_native_runtime.exe`。只有显式选择 `python` 或 `fallback` 才进入旧 Python 控制器。[手柄入口](D:/work/AI/yolo-study-001/scripts/launch/gamepad_start.bat:6) 和 [native 启动脚本](D:/work/AI/yolo-study-001/scripts/launch/gamepad_native_cpp_start.bat:5) 给出了这条调用链。鼠标默认入口直接调用 native launcher；Fusion 后台入口也启动原生运行时和 Canvas。[鼠标入口](D:/work/AI/yolo-study-001/scripts/launch/mouse_start.bat:3)、[Fusion 入口](D:/work/AI/yolo-study-001/scripts/launch/gamepad_fusion_background_start.ps1)。

因此，旧 Python 控制器及 Vision 回退实现不属于当前默认运行链。本轮按照调查范围要求，不再研究这部分测试的压缩空间，也不把它们计入原生精简比例。仓库仍有显式回退和调试入口，所以不能进一步声称它们已经完全不可用或从未被使用；本轮查证的是调用关系，没有用户使用频率数据。

Python 测试目录本身仍被 `pytest.ini` 的 `testpaths = tests` 收集。目录中的内容需要按被测对象区分：

| 内容 | 已查证的关系 | 本轮处理 |
| --- | --- | --- |
| 旧 Python gamepad、mouse、Vision、recoil 应用的测试 | 对应回退、旧控制器或独立工具，不在默认 native 运行链 | 不再分析其精简机会，不删除 |
| `tests/gamepad/`、`tests/mouse/` 中的基准辅助模块 | 两个旧 Python benchmark 工具直接 import，属于可执行工具依赖 | 随旧基准路径排除；不能当纯测试文件误删 |
| 启动脚本、native 遥测、Fusion fixture、鼠标 relay、项目验收工具的 Python 测试 | 被测对象仍关联原生运行时和现有验收工具 | 保留关联说明，不因使用 Python 就判为废弃 |
| 未跟踪的训练及 Vision contention 测试 | 属于工作区已有的其他工作 | 不进入本轮删除候选 |

基准工具对测试目录的直接依赖可见 [gamepad benchmark](D:/work/AI/yolo-study-001/tools/run_gamepad_benchmark.py:17) 与 [mouse benchmark](D:/work/AI/yolo-study-001/tools/run_mouse_benchmark.py:17)。当前原生相关的 Python 测试包括 [启动入口与配置](D:/work/AI/yolo-study-001/tests/test_startup_scripts.py)、[native 遥测分析](D:/work/AI/yolo-study-001/tests/test_native_telemetry_analysis.py)、[Fusion fixture 校验](D:/work/AI/yolo-study-001/tests/test_verify_fusion_visibility_fixture.py)、[鼠标 relay 父进程退出](D:/work/AI/yolo-study-001/tests/mouse/test_virtual_relay_parent_exit.py) 及 [验收判定](D:/work/AI/yolo-study-001/tools/tests/test_native_runtime_acceptance.py)。这些例子也说明，按整个目录或文件后缀判断是否有用会误伤当前工具。

**这次统计了什么**

统计对象是 Git 已跟踪的测试源码、事故回归源码和明确的测试辅助代码。使用词法分析去掉空行、注释与 Python 文档字符串，保留 C++ 预处理指令；多行声明、字符串和花括号仍按实际代码行计算。这个数字表示维护的源码规模，不表示可执行语句数，也不表示覆盖率。构建产物、fixture 数据、图片、生产 benchmark 实现和没有测试命名的独立验证工具不纳入分母。生产运行时中的 `mouse_relay_test_mode.h` 是诊断模式实现，也不按测试源码计算。

原生部分共 **117 个文件、30,633 个物理行、27,957 个代码行**。以下分组按当前构建入口划分，互不重复；两个 runner 的入口源码计入对应 runner，公共 registry 和 incident helper 单列。

| 原生源码所属入口 | 文件数 | 代码行 |
| --- | ---: | ---: |
| 基础产品 runner：ADS、BodyLock、目标、生命周期等 | 59 | 16,499 |
| 功能 runner：recoil、AutoFire、遥测等 | 17 | 2,898 |
| 主构建中的鼠标契约测试 | 21 | 3,444 |
| Fusion 契约测试 | 1 | 386 |
| 离线 benchmark 的测试源码 | 8 | 2,681 |
| 独立构建的 mouse_link 测试与模拟 DLL fixture | 5 | 156 |
| 单独运行的 near-target slowdown 事故回归 | 1 | 741 |
| 显式 GPU 与桌面输出探针 | 2 | 316 |
| 未在当前 CMake/registry 中找到入口的事故回归 | 1 | 415 |
| 公共测试 registry 与 incident helper | 2 | 421 |
| 合计 | 117 | 27,957 |

这些都是源码数量。当前构建目录注册了 50 个 CTest 项目，而基础与功能两个执行程序通过 `--list` 分别列出 313、89 个命名用例。一个 CTest 项目可以包含很多用例，一个事故用例还可以包含多个场景，所以“减少执行程序数量”“减少 CTest 条目”“减少源码行数”不能相互替代。

完整文件清单、代码行数和 SHA-256 在 [inventory.json](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/inventory.json)，原生入口映射在 [native-scope.json](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/native-scope.json)，可复算脚本为 [audit_inventory.py](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/audit_inventory.py)。脚本也保留了范围收窄前的 Python 清单，但其中的 Python 数量不参与上述原生精简比例。

**最值得先处理的是退役功能与当前测试入口的不一致**

`recoil_profile_tools` 已被构建文件明确标为退出生产链接链的旧工具库。当前 CMake 中，它只被功能测试 runner 链接；`weapon_recognizer.cpp` 也在这个测试 runner 中单独编译。与此同时，功能 runner 仍注册旧 profile 播放、文件选择、OCR 武器识别等测试。[旧工具库定义](D:/work/AI/yolo-study-001/native/vision_native/CMakeLists.txt:171)、[功能 runner 源码与链接](D:/work/AI/yolo-study-001/native/vision_native/CMakeLists.txt:382)。

这比“某个文件很长”提供了更强的精简依据：这些测试继续维护一个已经退出生产链的实现。首批候选为：

| 文件 | 代码行 | 当前保护内容 | 建议 |
| --- | ---: | --- | --- |
| [weapon_recognizer_tests.cpp](D:/work/AI/yolo-study-001/native/controller_native/weapon_recognizer_tests.cpp:393) | 349 | 旧 OCR/文件式武器识别、切枪后 profile 选择 | 优先核定退役；从当前产品门禁移除后，再决定旧工具是否还单独维护 |
| [recoil_contract_tests.cpp](D:/work/AI/yolo-study-001/native/controller_native/recoil_contract_tests.cpp:535) | 483 | 旧 profile 播放、校准、文件匹配及相关 recoil 工具契约 | 逐项核对并清理旧实现测试；涉及现行 recoil 语义的断言应保留在当前 owner 的测试中 |

两份文件合计 832 行，注册 20 个命名用例。这是整份候选文件的毛规模；保留下来的断言、必要的新归属和辅助代码都会减少最终净收益。不能把 832 行直接写成“已经确认可无损删除”。

实施时还需要处理一个真实依赖：[native_pipeline_contract.ps1](D:/work/AI/yolo-study-001/scripts/verify/native_pipeline_contract.ps1:136) 硬性要求这两个文件存在并进入 runner。直接删文件会使验证脚本失败。正确处理是让脚本检查当前 recoil 的生产契约及退役边界，而不是只删除检查以获得绿色结果。

必须继续保留 [配置不能启用旧 profile/recognizer](D:/work/AI/yolo-study-001/native/controller_native/runtime_config_tests.cpp:394)、[生产控制器不会读取旧 profile 路径](D:/work/AI/yolo-study-001/native/controller_native/target_pipeline_integration_tests.cpp:1023) 和 [当前 recoil reducer 输入契约](D:/work/AI/yolo-study-001/native/controller_native/recoil_reducer_tests.cpp:13)。它们验证的是今天的生产行为，不能随旧工具测试一起删除。

**重复辅助代码可以压缩，但收益要按净变化计算**

原生测试中检索到 93 个局部 `require*` 函数，分布在 68 个文件，共占 385 个非空行。这一数量包括所有副本和首份实现，未扣除公共 helper、include 与调用调整，因而只是待整理代码的规模。具体位置保存在 [cpp-helpers.json](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/cpp-helpers.json)。

值得尝试的是共享少量稳定的断言辅助函数，不改变调用处的业务含义。不同 `require_near` 的错误信息和数值检查并不完全相同；例如 [recoil helper](D:/work/AI/yolo-study-001/native/controller_native/recoil_contract_tests.cpp:28) 会在失败时写出实际值和期望值。合并时必须保留诊断信息和原有有限性检查，不能靠放宽容差或省略断言来缩短代码。

另一个集中区域是事故回归的报告与执行外壳。27 份事故回归源码合计 8,994 个代码行，其中扫描到的 27 个执行入口占 1,041 个非空行，37 个报告序列化函数占 1,507 个非空行。[函数位置清单](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/incident-plumbing.json)。这里使用“非空行”描述函数体毛规模，与总体去注释的代码行口径不同，不能直接相加计算可删比例。

参数读取、报告文件创建和异常输出有继续复用的空间，但仓库已有 [incident_fixture_support.h](D:/work/AI/yolo-study-001/native/controller_native/incident_fixture_support.h) 和 [native_test_registry.h](D:/work/AI/yolo-study-001/native/test_support/native_test_registry.h)，应先扩展现有的少量公共能力。每个事故的触发条件、对照场景、判定阈值与诊断字段仍应留在具体 fixture 内。

报告外壳也并非完全一致。例如 [single-press token 回归](D:/work/AI/yolo-study-001/native/controller_native/ads_single_press_token_incident_regression.cpp:246) 会把未触发或对照无效返回为 3，registry 据此标记 `INVALID`；其他入口的异常处理方式不完全相同。整理这些外壳需要逐个保持当前判定语义，不能把所有非零退出统一改成普通断言失败。

因此，公共 helper 和报告外壳适合先做一个小范围合并试点，再测量净减少量。目前没有依据把上述函数体总行数当成可删行数，更没有依据用它们补足 30%。

**未运行、默认不运行与无用，需要分别判断**

[ads_freshness_gap_demand_reuse_incident_regression.cpp](D:/work/AI/yolo-study-001/native/controller_native/ads_freshness_gap_demand_reuse_incident_regression.cpp:428) 有独立 `main`、触发检查和失效判定，但没有在当前 CMake 或基础 runner 注册中找到入口，也没有在本次 CTest 列表中出现。它包含 415 个代码行。本轮能够确认的是默认验证没有覆盖这个源码入口，不能确认这个事故已经被其他测试替代；没有进入构建也不能证明它今天能够编译。处理前应核对原事故是否仍有产品意义，以及现在由哪条回归保护。

与它不同，[near-target slowdown 回归](D:/work/AI/yolo-study-001/native/vision_native/CMakeLists.txt:431) 明确采用单独构建和运行方式，构建注释解释了这是为了避免每次基础测试都重复长矩阵。GPU 内容一致性测试、桌面鼠标输出探针同样有显式入口。它们默认不运行有具体理由，不应作为未使用代码删除。

8 份离线 benchmark 测试共 2,681 行，默认由 `NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS=OFF` 排除出普通构建和 CTest 注册；本机现有构建目录把该选项设成了 ON。关闭普通构建的重型测试可以减少日常执行成本，但保留源码仍然有价值。这一批验证场景、计分、模拟器和适配器，不能因为正常 gameplay 不调用它们就认定无用。

鼠标测试还有 3 个显式负向对照，由 `WILL_FAIL TRUE` 表达预期失败。它们用来确认来源泄漏或排除机制失效时能被发现；不能把这些测试当成“故意失败的重复测试”移除。[鼠标契约注册](D:/work/AI/yolo-study-001/native/mouse_native/mouse_targets.cmake:38)。

**大文件也需要保留独立的保护层次**

当前较大的文件包括目标选择测试的 1,581 行和生产管线集成测试的 975 行。它们都操作目标，但覆盖的责任边界不同：前者验证 selector 如何决定目标和权限，后者验证这些决定到达生产控制器后，手动输入、开火、recoil 和最终输出是否仍符合约束。仅因两者都出现同一个目标字段或相似快照，就合并测试，会失去其中一层的保护。

同样，ADS reducer 测试可以直接构造生命周期状态，而 single-press 事故回归要经过生产控制器，在目标替换和再次按 LT 的时间序列中验证一次 snap 的约束。精简应优先共享快照构造等准备代码，保留不同入口、状态转移和失败判定。

后续每个删除或合并候选都应明确：原测试保护什么、剩余测试如何触发相同问题、错误发生后由什么断言识别。对 ADS、BodyLock、身份、手动控制权、生命周期、AutoFire、recoil 和输出安全，专项事故回归不能被一个聚合 benchmark 结果替代。这一边界来自 项目优化合同（旧验收材料已删除），本轮没有运行参数搜索或作实战接受判断。

**本轮验证结果及边界**

| 检查 | 结果 | 证据范围 |
| --- | --- | --- |
| 现有 Release 基础、功能、Fusion CTest | 11/11 通过，0.87 秒 | 原生产品测试基线；两份 runner 清单共 402 个命名用例 |
| 现有 Release 鼠标 CTest | 24/24 通过，8.09 秒 | 含 3 个预期失败的负向对照；supervisor 单项约 5.36 秒 |
| 默认 Python 收集 | 912 个用例，12.68 秒 | 证明当前 pytest 仍收集该目录，不代表全部执行通过 |
| `tools/tests` | 7/7 通过，0.09 秒 | native 验收规则与日志管理 |
| PowerShell comparison 测试 | 1/1 通过 | `test_compare_axis_stress_ab.ps1` 的报告生成 |

原生基线沿用已有 Release 二进制，本轮没有重建，因此上述结果不能当成一次“从当前源码全新构建”的证明。执行日志、命令、工具版本和基础/功能/Fusion 二进制哈希保存在 [validation.json](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/validation.json)，对应 [产品基线日志](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/native-baseline.log) 与 [鼠标基线日志](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/native-mouse-baseline.log)。

范围收窄前启动过一次全量 Python 测试；在范围调整后中断，没有最终汇总。已输出进度中出现两次失败和一次跳过，本轮没有追查，不能报告为完整通过，也不会把这些失败测试作为删除依据。[未完成运行日志](D:/work/AI/yolo-study-001/artifacts/analysis/test-code-audit-20260923/pytest-baseline.log)。

其余 15 个已注册的离线/兼容性 CTest 项目、独立 mouse_link 构建、显式 GPU/桌面探针、standalone 事故回归和打包测试未在本轮执行。也没有采集覆盖率、运行 mutation test 或进行 live A/B。全量清单与静态引用检查已经完成，对语义的审阅集中在退役候选、注册关系、重复辅助代码及关键保护边界，没有宣称逐行证明全部测试互不重复。

**建议的实施范围**

首批应围绕旧 recoil/profile/recognizer 的 832 行候选做一份具体改动：逐项确认哪些旧工具行为停止维护，保留当前 recoil 和“旧功能不能被重新启用”的断言，再同步 runner、CMake 和 pipeline 验证脚本。实际保留的测试应重新构建并运行，而不是只用本次现有二进制结果验收。

随后只选择少量断言 helper 和事故报告外壳做复用，保留用例名字、独立场景、触发检查、阈值与失败诊断。对受影响的重要回归，用冻结的旧错误实现或定向故障注入确认测试仍能失败；最终收益按删除代码减去新增 helper、表格与调用代码的净值计算。

415 行未注册事故回归先查明归属，不作为现成删除额度。旧 Python 路径继续排除在本轮精简工作之外。完成首批后再测量净减少量；如果仍要达到 30% 或更多，应依据下一批具体候选继续调查，而不是从尚无替代证据的产品回归中凑出剩余行数。

本次未改写 `.agent-context/`。建议后续同步本报告链接，以及“旧 Python 回退测试暂不纳入精简、原生测试不能按执行程序数量衡量、旧 recoil 测试与当前验证脚本存在退役范围不一致”三项调查结论。
