# 2026-10-23 Vision/controller 提交与 clean 同步

用户明确要求“改动提交并push，然后同步到clean”。dev 实现提交 `3e2ab8d` 已推送 origin/dev；clean 生产同步 `9aad8f8` 已推送 origin/codex/runtime-clean。上下文收尾另行提交，最终 dev HEAD 以 Git 为准。

同步内容为前五项跟踪修复及本次 cue 独立运动参考、弱框身体参考修复，13 个生产文件。从 clean `71f9104` 建立 `codex/cue-motion-sync-20261023`，验证通过后快进合回 clean。测试、录像轨迹及研究报告留在 dev，未整体合并 dev。没有修改用户模型、Profile、曲线、压枪参数、力上限或手动权限。

## 验证结果

- clean 原生 Release 和 React 资源构建通过。180 个共有 native 文件与 dev 文本相同，4 个 CMake 文件保留既有构建职责差异。
- dev 夹具链接 clean 库：Base 473 项全通过，开火 14/14。首次 Base 为 466/473，7 个 SDL 输入集成用例因 runner 同目录缺少 SDL2.dll 失败；补齐开发测试目录内的 DLL 后，所属 BaseRuntimeFreshness 67/67，其余 406 项沿用首次通过结果。没有为此修改产品代码。
- Base 包含新 cue 独立运动回归与两个独立种子 23102301/98102713：128 个 2/20 秒场景、1,408,000 tick。运动、身份、输出有限性不变量通过。dev 修复阶段完整 CTest 51/51。
- 10 月 22 日录像的两个片段重新经过 clean/dev 原生 TensorRT 和 selector：665 帧、358 帧有目标、87 帧 cue_hold，完整 records 完全一致。固定像素和近似相机闭环的 controller CSV 也分别完全一致。每种模式输出 32 行案例组，其中 feedback 开关有等价重复，不能将它们算成 32 个独立场景。
- 桌面模块 81/81、隔离 WebView2 交互、隐藏真实 GUI 的模型初始化、IPC、保存并应用 Applied、正常停止通过；临时配置关闭设备输出与震动，没有停止用户原有会话。

产物：`runs/downpull-20261023/clean-build.txt`、`clean-base.txt`、`clean-runtime-recheck.txt`、`clean-fire.txt`、`clean-gui.txt`、`sync-live-gui/result.json`、`sync-video/`。测试套件在 `runs/clean-incident-fix-20261016/suite-sync-20261023/`。首次编译复用 MSBuild 节点时遇到写入权限错误，关闭节点复用后构建通过；这与产品算法无关。

## 使用与限制

从 clean 根目录 `启动助手.vbs` 启动；已有原生助手需要停止再启动才能加载更新。只同步代码和交接记录，个人资产及旧实验结果保持。

两处已证明的参考归属错误已修复；不能据固定录像认定真实突然下拉的唯一原因或全部消失。固定视频第二段另时刻峰值增大、近似闭环第一段平均纵向误差退步仍保留在 [修复报告](DOWNPULL_DEV_FIX_20261023.md)。模拟相机平移不包含真实重渲染、变焦/旋转/视差及完整武器扰动，无法替代实机验证。之前 ADS 开火走位/固定偏置压力反例也没有因同步成功而关闭。
