# DEC-2026-10-12-001: DSE 方向震动与新版 GUI

Status: accepted
Date: 2026-10-12
Confirmed by: 用户要求实现震动、增加配置开关和力度控制，删除旧 GUI，最终明确要求“这次的决策和改动都记录然后提交”。
Related sessions: 本次 DSE 实机探索、方向触感确认、功能实现及 GUI 清理。
Related files: `native/runtime_app/dse_haptics.*`、`detection_haptics_policy.h`、`runtime_loop.*`、`native/controller_native/editable_parameters.json`、`python/desktop_app/web_ui/`、`web_service.py`、`gui.py`、`AGENTS.md`。
Supersedes: 2026-10-04 合并记录及旧 AGENTS 中“保留 Tk GUI”的适用性；原生运行时及用户资产边界继续有效。
Superseded by: none

## Context and decision

用户希望在应用中利用现有手柄辅助提示，先试验了 USB DualSense Edge 的音频触觉，再确认左、中、右三种方向提示。最终范围为人物出现时震动，以及可保存、热加载的开关与力度；其他手柄能力暂不扩展。

唯一 GUI 是 `启动助手.vbs` 打开的 React/WebView2。旧 Tk 窗口、控件及其专用测试/截图/交互工具直接删除，由 Git 保留历史。`gui.py` 仍是新版薄启动入口与原生命令入口；配置、曲线、进程管理、IPC、模型和个人配置保留。后续界面改动进入 `web_ui/` 和 `web_service.py`。

## Implementation choices and reasons

以下是本次授权范围内的实现选择，不表示用户逐项指定了数值：

- 默认关闭；力度默认 24%，来自用户最后认可的一轮触感测试，可调 0–100%。每次 100 ms、120 Hz、10 ms 起落包络；左侧/双侧/右侧对应捕获画面横向三等分。
- 首次出现有效人物时通知一次；持续有人不重复，连续约 250 ms 的新鲜空画面后重新允许提醒。重复帧和停滞不作为离场证据。多个人物选择横向最靠近中心者，遵守已有友方标记；不依赖瞄准目标已锁定，也不增加瞄准或开火权限。
- 原生独立工作线程保持 WASAPI 四声道流，只向后两路触觉声道写入波形。根据当前实体 USB DSE 的 ContainerId 匹配唯一音频端点，不选系统默认扬声器或另一只手柄。设备失败不阻塞控制线程；恢复重试仅发生在音频线程。
- 尊重音频静音。端点音量为零时临时设为 50%，退出时在用户未另行改动的情况下恢复。提醒默认复用空闲检测；原本关闭空闲检测时才额外请求 DetectionOnly。
- 只修改提醒参数时不重置控制器状态或响应学习。GUI 中 24% 对应配置 `gamepad.detection_haptics.strength = 0.24`。

## Rejected alternatives

- 额外购买提示配件、继续扩展灯光/扳机等表现形式：用户本次收敛为现有 DSE 方向震动。
- 在控制循环同步打开音频设备或逐帧持续震动：会增加控制阻塞或重复打扰。
- 继续维护旧 Tk GUI，或删除整个 Python GUI/配置服务：前者违背用户清理要求，后者会破坏实际新版入口。

## Evidence and limits

- 用户实际感受了逐步增强的左、中、右波形，在 24% 一轮认可方向可用；不是完整游戏触发验收。
- 生产音频工作线程的设备匹配、三方向播放、关闭与退出实机检查通过。HidHide 原有隐藏状态保留，临时探测程序授权已清理。
- 原生测试包含方向/友方/无目标/重复帧/停滞、连续离场、两组各十万帧的固定随机种子、波形边界、配置校验和热加载保留。四组 Feature 测试通过。
- 79 项保留桌面测试与真实 WebView2 震动设置保存/关闭保护通过，正常及最小窗口布局检查通过；旧 Tk 交互测试删除不代表 WebView2 已有等量覆盖。
- 提交前将本次原生改动与 HEAD 组成独立快照；快照和暂存区原生源文件逐项比对一致，Release runtime/功能测试构建成功，四组 Feature 测试通过，不依赖其他未提交的控制器、转发线程或遥测改动。
- 当前支持 USB DSE，范围是模型捕获区域；实际游戏自动触发体验、蓝牙及其他手柄不在已验证结论中。原始探测/截图/构建产物保留本地 `output/dse-capability-probe-20261012/`，不纳入提交。

## Consequences and review triggers

用户在“参数调校 → 目标与识别 → 人物震动提醒”启用并保存应用。遇到过于频繁、方向不清或设备兼容问题时，以实际新鲜检测帧和音频状态定位；不要把通知问题改成控制器权限或目标交接策略。只有用户提出新方向时才恢复其他 GUI 或扩展其他硬件能力。
