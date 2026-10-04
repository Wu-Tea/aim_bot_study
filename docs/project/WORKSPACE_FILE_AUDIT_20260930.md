# 配置、GUI 与启动文件盘点

2026-09-30 按工作区改动、启动调用链、源码引用和测试用途检查。当前入口是根目录 `启动助手.vbs`，游戏共用原路径 C++ 程序与 `config.toml` 的游戏分块。
这次提交保留有用的实现和验证工具；退役实验仅登记，文件仍留在原位置，没有删除或移动用户数据。

## 提交的内容

| 内容 | 用途与判断 |
| --- | --- |
| `native/controller_native/game_stick_transfer.h` 与配置、输出、送达确认接入 | BO3 等游戏的输出转换；真实生产路径引用，保留 |
| `native/runtime_app/runtime_control_bridge.h`、`runtime_stop_signal.h` 与调用方 | 热重载、学习快照和正常退出；GUI 的运行控制依赖这些协议，保留 |
| 响应估计器、fire 判断、视觉策略及对应原生测试 | 重载时清理学习，统一 fire 绑定，避免旧视觉帧混入新策略，保留 |
| `desktop_app/{gui,runtime,settings,fields,control}.py`、`__init__.py` | 独立管理界面、配置校验与保存、进程身份检查、控制通道，保留 |
| 根目录 `启动助手.vbs` | 当前用户入口，保留 |
| 通用/Apex/BO3 后台入口与 Fusion 的 AttachOnly 模式 | 兼容入口及独立启停；实际启动调用链仍使用，保留 |
| `tests/test_desktop_app.py`、`test_apex_launcher.py`、`test_bo3_launcher.py`、`test_startup_scripts.py` | 当前配置与启动管理的验证，保留 |
| `scripts/verify/benchmark_game_stick_transfer.cpp` | BO3 说明文档引用的算术微基准；用于复查转换成本，保留，不能代替 runtime/实战性能对照 |
| `config.native.example.toml`、GUI/Apex/BO3 说明、README | 可移植默认值与使用边界，保留；不包含本机调参文件 |

## 退役或伴生文件

“没有运行引用”指当前启动与生产源码没有调用，不表示文件内容毫无历史价值。
引用检查覆盖受版本控制的源码、脚本、文档、测试，以及这次提交的新实现；运行日志和生成产物不作为生产依赖。

| 文件或目录 | 证据与现状 | 本次处理 |
| --- | --- | --- |
| `scripts/launch/bo3_config.ps1` | `ConvertTo-Bo3ConfigText` 用正则生成另一份配置；当前启动脚本直接传 `--game bo3`，没有调用该助手 | 未跟踪的退役实验，留在本地，不纳入功能提交 |
| `scripts/launch/gamepad_input_access.ps1` | 旧路线自动注册 HidHide 路径；当前后台入口不调用 `Ensure-HidHideAccess` 或 `Test-NativeGamepadInput`，统一使用已部署路径 | 未跟踪的退役实验，留在本地，不纳入功能提交 |
| `tests/test_gamepad_input_access.py` | 仅测试上述退役助手，模拟的是旧 `artifacts/runtime/bo3` 程序路径；不能证明当前实体输入已接通 | 与退役助手一起保留本地，不作为本次提交的产品测试 |
| `scripts/launch/apex_config.ps1` | 已跟踪的旧 `ConvertTo-ApexConfigText` 助手；当前 Apex 入口使用原生游戏分块，没有运行调用 | 登记退役，维持现有跟踪状态，未删除 |
| `config.bo3.toml` | 旧本机调参文件；普通启动不读取，`ConfigStore.migrate_games()` 的显式迁移仍可读取 | 保留本地并加入精确忽略规则，既不发布也不删除 |
| `config _higher-.toml` | 当前代码、脚本没有引用；来源与意图未经用户确认 | 按本机参数副本保留，加入精确忽略规则，未改写内容 |
| `artifacts/runtime/bo3/` | 旧独立程序产物；当前启动与 GUI 指向 `native/vision_native/build/Release/cod_native_runtime.exe` | 已在 artifacts 忽略范围内，保留本地，不提交 |
| `native/vision_native/build-bo3/` | Release 编译与测试使用的暂存构建目录，名称不是生产游戏入口 | 保留构建用途，已忽略，不提交二进制 |
| `runs/gui-*`、`runs/bo3-*` 与 `runs/desktop/` | 集成截图、进程生命周期证据、微基准结果、二进制备份、用户配置备份及学习导出 | 已忽略，保留本地；文档登记证据位置 |
| `__pycache__/`、`.pyc` | Python 缓存，不是源码依赖 | 已忽略，不提交 |

不向 `.gitignore` 添加退役源码的忽略规则，避免以后误以为这些文件已经清理；剩余未跟踪文件应与本表中的三份退役实验一致。

## 验证与限制

提交前运行原生 10 组 Base/Feature 套件，以及当前产品相关的四个 Python 测试模块。既有 BO3 262,144 组随机输入、21,200 tick 时序检查和热重载 8,000 tick 场景由这些原生套件覆盖。
此前 Python 63 项结果包含退役 HidHide 助手的 5 项模拟测试；本次提交的产品相关模块为 58 项，不把退役模拟当作设备验收。
GUI 真实进程退出、重新接管、runtime 停止/重启和 Fusion 独立关闭的证据在 `runs/gui-lifecycle-20260930/`；热重载证据在 `runs/gui-hot-reload-20260930/`。

实体手柄未连接期间的验证不能证明输入接通，离线与桌面模拟也不等于游戏手感验收。
旧前台或直接 exe 启动缺少统一状态记录，GUI 完整接管仍有缺口；手柄连接状态也仍是启动时的识别结果。
模型、帧率、响应曲线、死区补偿等结构设置仍需重启；学习参数当前只支持查看和导出。

部署程序 SHA-256 为 `86ca2882af1e6ea8bc900bdae9b4291ce8c9107819610694b947a43dc09ca40e`，位于原生产路径。
该程序在本次整理提交前构建，内嵌构建提交号仍是旧值；不能仅凭提交号声称与新提交二进制相同，应结合程序哈希与本次源码/测试记录核对。

## 后续处理条件

退役脚本与测试可以在用户明确授权删除后再清理。不要把旧构建目录作为新游戏入口，也不要恢复配置转写或自动 HidHide 注册路线来绕过上述状态接管缺口。
本机参数文件需要历史核对时仍可使用；未来发布初值应改 `config.native.example.toml`，而不是提交本机文件。

## 2026-10-04：按用户授权清理未接入生产的实验

本节更新上述历史盘点的文件现状。用户授权删除未接入生产的工作区实验，BF6 相关工作暂不处理。

- 已删除未跟踪的 `scripts/launch/bo3_config.ps1`、`scripts/launch/gamepad_input_access.ps1` 和 `python/tests/test_gamepad_input_access.py`；当前启动入口没有调用这些助手。
- 已删除未接入原生生产链的拟人加速研究源文件 `native/controller_native/human_acceleration_research.cpp`、两个 Python 研究脚本 `python/tools/verify/research_human_acceleration.py` / `research_endpoint_trajectory.py` 及其研究文档 `docs/benchmarks/COD_CONTINUOUS_ACCELERATION_RESEARCH_20261002.md`。
- 已移除 `native/cmake/OfflineBenchmarks.cmake` 中该未跟踪研究的构建目标，恢复原有目标集合。历史运行证据、构建备份、模型和本机配置未删除。
- `target_selector.h` 的剩余变化只有行尾格式，已恢复版本库内容。
- BF6 专属训练、比较脚本、测试和 cue 调研记录保留在工作区，不纳入本轮提交。

这些被删除的源码原本未受版本控制，所以本次提交以本节记录清理结果，不会出现对应的 Git 删除条目。保留的桌面改动提供设置分组、详细参数折叠、中文选项、切换游戏时立即保存选择，以及将继承值单独保存为游戏专属配置。视觉评估工具的多边形标签解析修复独立提交。
