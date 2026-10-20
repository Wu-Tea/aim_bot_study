# 2026-10-20 实机接受后的提交与 clean 同步

用户试用当前 dev 后反馈“提升很大，允许提交，同步到clean”。本次同步该实测组合，包括 person OR cue / 同帧身体参考，以及此前叠加的 ADS 维持运动和可行手动协作。保留原权限、胸部参考、32/40 ms、手动退出、固定压枪与个人配置，不把用户的整体改善反馈解释为全部历史仿真反例已解决。

## 提交范围

- dev `a93dbaa`：ADS 维持运动、可行手动贡献、已知手动与未知开火扰动分离；对应回归及实验材料留在 dev。
- dev `642dc26`：可信 person 独立取得目标、同帧 cue/person 身体参考、持续可靠 cue 超过 180 ms 延续；像素和完整链回归、录像重放材料。
- clean 生产提交 `91b116a`：16 个生产文件，来源为以上两次 dev 提交。先在 `codex/person-cue-or-sync-20261020` 验证，全部通过后快进回 `codex/runtime-clean`。
- 原有模型、DLL、Profile、配置和日志不覆盖；未提交 runs/output、二进制或个人资产。dev 正在运行的原生进程保留，未为更新提交版本停止其会话。

## 同步验证

- clean 原生 Release 与 React 桌面资源构建通过。180 个共有 native 文件文本与 dev 一致；4 个不同文件是原有产品/测试范围构建差异：根 CMake、controller/runtime/Fusion targets。没有把 dev 测试或离线工具加入 clean 构建。
- clean 桌面模块的 81 项测试和隔离 WebView2 交互检查通过。使用 dev 保存的夹具，生产 Python 模块全部来自 clean。
- 隐藏真实 GUI、独立临时配置，输出/震动关闭：模型初始化、IPC、GUI 保存固定压枪参数并取得 Applied 回执、正常停止全部通过。结果 `runs/person-cue-sync-20261020/live-gui/result.json`；未修改用户配置或停止用户 dev 程序。
- 原生 Base **462/462**、开火 **14/14** 通过；dev 夹具链接 clean 生产库，包含本轮 OR 与长时参考用例。日志为 `runs/person-cue-sync-20261020/base.log` / `fire.log`。夹具生成目录 `runs/clean-incident-fix-20261016/suites`，构建目录 `suite-sync-20261020`，不进入 clean。
- 两个独立验证种子 49980167 / 104396113，2/20 秒，共 192 场景、2,112,000 控制 tick；同一 fixture.toml、原固定 1575+50 ms 目标节奏、完整生产控制链。clean 的 cases.csv 与 trace.csv 四个文件均与 dev 已验证的 final-validation/after 对应文件完全一致。只证明同步行为一致，原先的大误差反例也被保留，没有用一致性宣称游戏准确率。

## 复跑与边界

在 dev 执行 `tests/clean_incident_20261016/prepare_suites.py <clean路径>`，生成纯 dev 测试目录并以 clean 生产头文件覆盖对应文件。使用同目录的 `build.ps1` 构建 Base/开火套件。额外 closed_loop_clean 目标编译 `tests/ads_cooperation_20261019/closed_loop.cpp`，使用同一套 clean 头文件和生产库；以 `tests/ads_cooperation_20261019/fixture.toml <输出目录> <种子>` 执行。GUI 复跑为同目录 `gui_checks.py` 与 `live_gui.py`，后者必须使用新输出目录、独立配置并关闭输出与震动。

实机认可覆盖本次用户所试组合，不能据此推断所有枪械、姿态、遮挡、错误标记和控制压力场景都已验收。颜色误识别、固定偏移对姿态/距离变化的局限及历史 ADS/BodyLock 反例继续保留在原报告中。后续若有退步，优先关联真实录像、输入和遥测；不要为掩盖它们放宽时效或手动边界。
