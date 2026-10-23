# clean 当前接续

更新：2026-10-24，sync-20261024-015。用户授权先push dev再同步clean。dev实现84fc8b8已推送，clean从5b7a693的临时分支同步15个生产文件并验证，验证后快进合回codex/runtime-clean并push；最终提交与远端状态以Git为准。

## 当前产品

- 敌人#FF00FF、友军#00FFFF；共享配色模块统一cue、人物分类和续跟。桌面“目标与识别”提供敌我HEX配置，Profile字段runtime.vision.enemy_marker_color/friendly_marker_color，缺字段用新默认，不自动重写个人Profile。更改后需重启助手。
- 同组自动标记敌人开关映射gamepad.enemy_mark.enabled。关闭仅停止自动生成标记，游戏已有/手动标记仍可能干扰。
- 包含此前dev验证的腰射AI接管连续性、ADS倍率平滑修复；Off被动减速已撤回，不恢复。原始手动透传、逐轴权限、源身体参考、索敌和cue续跟规则保持。
- 保留原生手柄runtime及React/WebView2桌面GUI，从启动助手.vbs启动。没有复制dev测试/研究/鼠标独立工具或个人资产。

## 验证与边界

- 原生Release、React资源构建成功。dev夹具链接clean生产库：Base483/483、开火14/14；桌面82/82、隔离WebView2、隐藏窗口且关闭输出的实际模型初始化、IPC热更新和正常停止通过。
- 两录像合成换色对照3983帧与dev行为一致，两处真实菱形局部换色的敌人关联/续跟和友军排除通过。15个同步生产文件一致；180个原有共有native文件一致，4个构建职责差异保留。
- 配色解析只在启动，当前配色整数快速路径，未降频/增扫描。dev三轮selector阶段整体未测到回退；独立全图cue一段约多0.03ms，阶段耗时不是实机FPS。
- 换色不能彻底消除区域占比法的背景误判；人体框几何突变、person/cue参考不一致仍未修，游戏新配色实拍验收待用户反馈。
- 使用方法见docs/BUILD_AND_RUN.md；详细实验/报告均在dev docs/project/PALETTE_CLEAN_SYNC_20261024.md及runs/palette-clean-sync-20261024/，不要复制测试回clean。

SyncSet015沿用发布与上下文维护授权，reviewer=accept_draft；旧交接归档在dev .agent-context/archive/clean-handoff-before-palette-sync-20261024.md。无外部记忆/秘密/新brief，不把push许可记为实机效果验收。
