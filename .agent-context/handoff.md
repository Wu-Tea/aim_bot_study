# clean 当前接续

更新：2026-10-23。用户授权先push dev再同步clean。dev生产实现1efe8f7已先推送，本次从clean9aad8f8建立临时分支验证后合回codex/runtime-clean。最终clean提交和远端状态以Git为准；发布授权不代表实机效果全部验收。

## 当前程序

- 同步4个生产文件：cue_identity头/实现、target_selector头、target_selector_fusion。包含整数颜色分类、每4行种子及行段展开，再按当帧person关联范围/已锁定cue移动范围跳过整片无关区域。
- 搜索与关联、身份匹配共用几何定义；detector保留完整命中标记组合及跨边界形状。没有固定底部屏蔽或降频；person漏检时仍搜索活动cue，当前新person区域每帧加入。
- person OR cue、可靠同一cue超过180ms续跟、胸部参考和独立cue运动参考保持。原始透传、逐轴手动、显式退出、ADS/BodyLock、压枪及32/40ms边界不变。
- 性能优先已写入本分支AGENTS.md：改动前评估帧率/延迟/每帧工作/分配/同步，主动告知可能开销并提供实测及替代方案，让用户参与算法思路。
- 原生Release和React资源已构建；从根目录启动助手.vbs启动，已有原生助手需重启加载新版本。没有停止用户现有会话、修改个人模型/配置/曲线或复制测试进入clean。

## 验证与限制

- dev CTest51/51；clean生产库由dev夹具验证Base477/477、开火14/14，包括两个独立种子128个2/20秒运动场景。单进程重跑解决测试套件并行构建未完成的问题，未因此改产品代码。
- clean区域接口4800构造图20,350必需cue全部保留；三段录像39,435帧完整selector输出与冻结全图版本和已验证dev实现一致。
- 桌面81项及隔离WebView2通过；独立配置、隐藏窗口、关闭输出/震动的真实模型初始化、IPC、热更新Applied及正常停止通过。
- dev同帧性能对照：全图参考→生产selector均值约0.74–0.78ms→0.13–0.15ms，含区域构造成本。不是游戏FPS恢复保证，也没有宣称修复原误识别、死亡目标续跟或旧下拉/ADS压力问题。
- 改动cue消费者、匹配距离、关联/形状/命中标记规则时，必须维护搜索覆盖。陌生cue无person无身体参考冷启动不属于当前语义。测试和完整报告留dev docs/project/CUE_REGION_CLEAN_SYNC_20261023.md，产物runs/cue-region-sync-20261023/。

SyncSet sync-20261023-005，沿用上下文维护及本次发布授权，reviewer=accept_draft。只同步当前产品交接和性能规则，无平台记忆或秘密；原交接归档于dev .agent-context/archive/clean-handoff-20261023-before-cue-region-release.md。
