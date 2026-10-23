# 当前接续

更新：2026-10-24，sync-20261024-015。用户要求先push再同步clean；dev实现84fc8b8已先推送。clean从5b7a693建立临时分支，15个生产文件同步并验证完成，验证后快进合回codex/runtime-clean并push；clean发布提交1524c65，最终远端状态以Git为准。

## 当前实现

- 用户选定敌人#FF00FF、友军#00FFFF。共享MarkerPalette模块，独立cue/人物分类/外观续跟统一。Profile字段runtime.vision.enemy_marker_color/friendly_marker_color，桌面“目标与识别”可填HEX，默认新配色、重启生效；启动前拒绝无效/过暗/无彩色/色相范围重叠。
- 自动标记敌人开关映射gamepad.enemy_mark.enabled，重启生效。小菱形形状已支持；自动标记空心双菱形仍可能干扰，用户选择自行关闭生成。
- 保留此前腰射AI接管连续性、ADS倍率进入原平滑链修复。Off被动敌人减速已由用户撤回，禁止无新请求恢复。逐轴权限、源身体瞄点、原始手动透传保持。
- 颜色加载时编译，洋红/青色整数快速路径；没有新增扫描/同步锁/逐帧解析或降频。配色和控制实施报告分别见[MarkerPalette](../docs/project/MARKER_PALETTE_DEV_20261024.md)、[腰射接管](../docs/project/HIPFIRE_HANDOFF_DEV_FIX_20261023.md)。

## 验证与发布

- dev CTest51/51、Base483/483、桌面82/82。clean生产库由dev夹具验证Base483/483、开火14/14、桌面82/82、隔离WebView2及真实关闭输出的初始化/IPC热更新/停止通过。
- 两录像合成换色对照3983帧clean/dev行为一致；15个同步生产文件一致，180个原有共有native文件一致、4处构建职责差异保留。测试/研究只在dev，未改个人Profile/模型/曲线/历史素材。
- 三轮selector均值菱形0.135–0.140→0.131–0.136ms、近身0.190–0.196→0.182–0.186ms；独立全图cue菱形多约0.03ms。不是实机FPS，旧录像合成换色不是新配色实拍。
- 详情：[发布记录](../docs/project/PALETTE_CLEAN_SYNC_20261024.md)；产物runs/palette-clean-sync-20261024/及runs/palette-20261023/。

## 已知限制与下一步

- 新配色实际游戏效果待用户反馈，不能把发布授权记成实机验收。区域占比法仍有青色背景友军误判（20:17片段978帧）；两处树叶案例仅局部换标识后可正确关联/排除。
- 22:52 BEST PLAY近身错身场景已定位observed人体框形状突变与person/cue身体参考不一致：源瞄点跳117px、后续57–59px跳变；本轮未修几何算法。报告[录像调查](../docs/project/NEAR_PASS_VIDEO_DIAGNOSTIC_20261023.md)。没有对应真实手柄日志，不能把模拟输出当实战重建。
- 上次控制回放的所有模型假设、限制见对应报告；不要再查已经确认关闭的遥测，优先使用现有录像/明确输入假设。

SyncSet015沿用维护和发布授权，reviewer=accept_draft；旧dev/clean交接原文已存archive/handoff-before-palette-clean-sync-20261024.md及archive/clean-handoff-before-palette-sync-20261024.md。无新正式decision/brief/外部记忆/秘密，session-log长历史保留，建议以后独立压缩。
