# 2026-10-23 cue区域跳搜发布

用户授权“先push然后同步到clean”，并明确以后项目改动先考虑性能、主动报告成本及可选方案。dev实现1efe8f7先推送；clean生产同步5b7a693随后快进合回codex/runtime-clean并推送。性能规则已加入两边AGENTS.md。dev交接收尾另行提交，最终HEAD以Git为准。

## 发布范围

本次将已验证的第二版行段/整数优化和关联/续跟区域跳搜接入生产。CueDetector保留默认全图接口，区域版由selector显式传入坐标范围；person关联成本与搜索共用association_region，cue身份匹配与搜索共用42像素常量，最大形状及命中标记范围也由detector统一维护。复用区域/行段缓冲，无消费者时清空结果并跳过搜索。

没有加入固定底部屏蔽、降低检测频率或改变controller权限/力参数。当前person的新关联区域始终加入；person漏检时保留活动cue范围；完整命中标记与跨区域连通形状保持。测试、离线生成代码、截图和实验报告留dev，clean仅同步四个生产文件及项目规则/交接。

## dev验证

- 原生Release构建成功，CTest51/51通过；新增区域边界、空区域清理、完整命中标记、无person时进入底部续跟三个回归。
- 两独立种子4800构造图、20,350必需cue保留，无新增碎片；同时核对生产区域接口与离线候选的数量、顺序、坐标、大小、颜色和评分，覆盖RGB/BGRA、pitch及非零图像原点。
- 三录像665+23,198+15,572=39,435帧，实际生产selector、冻结全图参考及离线候选的完整记录全部一致。
- 全图参考→生产selector平均耗时：下拉0.772→0.155ms、早期dev录像0.739→0.128ms、较晚clean录像0.784→0.145ms；P95分别1.441→0.474、1.151→0.488、1.377→0.555ms。包含区域构造成本；同次轮换顺序测量、6个E核、统计排除前30帧。

产物runs/cue-region-sync-20261023/：dev-build.txt、dev-ctest.txt、geometry.txt、dev-videos/summary.json和逐帧CSV。此前全色域/随机/完整录像验证见[行段优化](CUE_GREEDY_EXPLORATION_20261023.md)及[区域实验](CUE_REGION_PRUNING_20261023.md)。

## clean同步状态

- 从clean9aad8f8建立codex/cue-region-sync-20261023，验证后提交5b7a693并快进合回、push。仅四个生产文件及AGENTS.md/当前handoff，共六个文件；180个共有native文件与dev相同，四个CMake文件保留既有产品/测试构建职责差异。
- clean原生Release及React资源构建成功；dev夹具链接clean库，Base477/477、开火14/14通过，包含两个独立种子128个2/20秒运动场景（1,408,000tick）。首次套件并行构建未完成，单进程重跑构建和全部测试成功，无产品代码绕过。
- clean区域接口4800构造图20,350必需cue保留、无新增碎片；三段录像39,435帧完整selector输出与同一冻结全图参考、离线候选及已验证dev实现一致。clean复验部分与构建/功能测试并行，clean计时不作为新增性能比较结论；性能数据使用前述dev同帧对照。
- 桌面81项、隔离WebView2通过；独立配置/隐藏窗口/关闭输出和震动的真实模型初始化、IPC、热更新Applied、正常停止通过。没有停止用户原有会话。
- 产物同在runs/cue-region-sync-20261023/：clean-build.txt、clean-web-build.txt、clean-base.txt、clean-fire.txt、clean-geometry.txt、clean-gui.txt、clean-live-gui/result.json和clean-videos/。最终提交后重配置并构建原生runtime，刷新内嵌提交号；模型、个人配置、曲线及历史实验保持。

## 限制

这次修复的是检测搜索成本，不是原有误识别/死亡目标续跟/下拉问题的全面修复。固定输入录像不能替代真实画面反馈，阶段耗时不等于游戏FPS。陌生cue无person无身体参考的冷启动并非当前入口；未来修改cue消费者、尺寸或匹配范围时需同步区域覆盖。保留逐轴手动权限、退出、原始透传、胸部参考、压枪及32/40ms边界，个人配置和模型保持。
