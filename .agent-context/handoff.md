# 当前接续

更新：2026-10-23。用户授权先push dev、再同步clean，并要求以后改动先考虑性能、主动报告开销和可选方案。dev实现1efe8f7已先推送，clean5b7a693验证后快进合回codex/runtime-clean并推送；本交接收尾另行提交，最终HEAD以Git为准。用户可重启clean试用。

## 本次生产改动与验证

- 4个生产文件：cue_identity头/实现、target_selector头、target_selector_fusion。包含此前每4行种子、行段展开和整数颜色分类，再按当前person关联边界/活动cue范围合并搜索区间。detector自行保留完整命中标记组合及形状边界；无全局实验状态。
- 关联与搜索共用几何；身份匹配与搜索共用42像素距离。完整图像接口保持，没有固定底部屏蔽、降频或新增person置信度筛选。person漏检仍搜索活动cue，未配对陌生cue不能凭空冷启动身体参考。
- dev Release成功，CTest51/51；4800构造图20,350必需cue全部保留，无新增碎片，覆盖生产接口、RGB/BGRA、pitch、非零原点、完整命中标记和无person进入下方续跟。
- 实际生产库与冻结全图参考/离线候选三录像39,435帧selector完整记录全部一致。selector均值下拉/dev/clean录像0.772→0.155、0.739→0.128、0.784→0.145ms；包含区域构造，阶段耗时不等于实机FPS。
- 产物runs/cue-region-sync-20261023/；[发布报告](../docs/project/CUE_REGION_CLEAN_SYNC_20261023.md)、[区域实验](../docs/project/CUE_REGION_PRUNING_20261023.md)、[行段实验](../docs/project/CUE_GREEDY_EXPLORATION_20261023.md)。旧实验和截图保留，固定底部屏蔽仍属实验。
- clean Release/React构建、Base477/477及开火14/14、4800构造图、完整39,435帧输出复验全部通过。Base含独立128个2/20秒运动场景。桌面81项/隔离WebView2/隐藏关闭输出的真实初始化、IPC、热更新及停止通过；180个共有native文件一致，4个CMake职责差异保持。

## 同步边界与长期规则

- 已按顺序先push dev，再仅同步4个生产文件及clean自己的规则/交接；没有整体合并dev或复制测试/研究。个人配置、模型、DLL、曲线、历史产物保持，没有停止用户会话。第一版保留提交f4b9cbe已随dev推送。最终提交后重构建runtime刷新内嵌版本。
- 性能优先要求已写入AGENTS.md：实现前考虑帧率/延迟/每帧工作/分配/同步，主动告知可能成本并提供实测与替代方案；不能静默牺牲检测频率和索敌/跟踪效果。
- 原始透传、逐轴手动权限、显式退出、胸部参考、压枪及32/40ms边界保持；本轮只修检测成本，等价输出会保留原误识别，不能宣称旧下拉或ADS压力问题全部解决。
- 改动消费者/关联几何/匹配距离/最大cue尺寸/命中标记规则时必须维护搜索覆盖。生产复验不等于实机游戏验收。

SyncSet sync-20261023-005：沿用上下文维护授权及本次发布授权，reviewer=accept_draft。更新dev/clean handoff、追加简短会话记录，旧交接归档archive/handoff-20261023-before-cue-region-release.md及archive/clean-handoff-20261023-before-cue-region-release.md。无新外部记忆、秘密或subagent；不把发布授权记作实机效果验收。长会话历史保留，建议以后独立归档压缩。
