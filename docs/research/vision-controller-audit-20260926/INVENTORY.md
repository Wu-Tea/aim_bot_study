# Vision → Controller → DS4：输出影响清单

审查日期：2026-09-26。对象为当前工作区 native gamepad 路线；右摇杆 15–30% 曲线属于尚未部署的工作区候选。其余参数按当前 `config.toml`、构造器映射和源码默认值交叉核对。这里按语义机制归组，**编号数量不等于 if 语句数量，也不表示所有机制同时生效**。

“生效”指能进入本路线的输出因果链；“条件”指需要该行指定的事件；“停用/诊断”表示不能当成本轮输出的增减力度机制。S 编号链接到本文件末尾的源码索引。

## 输入、采样与视觉发布

| ID | 处理及条件 | 改变什么；当前数值 | 状态 / 审查判断 | 源码 |
|---|---|---|---|---|
| I01 | SDL 优先；没有 SDL reader 才用 XInput | 选择实际输入来源；掉线后重连原设备，不能把虚拟手柄当物理输入 | 生效；设备选择属于边界，不能用算法平滑替代 | [S01] |
| I02 | SDL 轴归一化、Y 反号 | 负值除 32768、正值除 32767；没有右摇杆死区或低通 | 生效；原始转写和 AI 意图是两条数据路径 | [S02] |
| I03 | 扳机初始化、按钮/触摸映射 | 扳机未初始化时为零；touchpad click 单独解析 SDL binding；最多两个触点 | 生效；只有触摸按压映射查询语义 binding，常规按钮采用 raw 布局 | [S02] |
| I04 | LT 事件迟滞 | >5% 激活视觉；≤3% 连续 3 个采样释放；当前 ≥30% 才 ready，ready 释放阈值 25% | 生效；这是扳机/事件门，不是右摇杆死区 | [S03] |
| I05 | 手动 fire 建立辅助范围 | RB 或 RT>4%；允许腰射建立 AI 范围，不伪造 LT；触摸连发不建立范围 | 生效；fire 同时改变生命周期和观察器上下文 | [S03] |
| I06 | 右摇杆 AI 意图曲线 | 每轴 `w=smoothstep((abs(M)-.15)/.15)`，解释信号 `M*w`；原始 M 保留 | 工作区候选；已安装 binary 不是此候选 | [S04] |
| I07 | 手势目的锁存 | onset/reversal 依当前是否拥有目标选择 Acquire/Correct；交接时 Handover；释放不是任意改目标许可 | 生效；连续幅度之外仍有离散语义切换 | [S04] |
| I08 | 左杆解释 | adaptive bias/noise/deadzone 仍在 IntentFilter 的左杆解释路径；实际左杆输出保持 raw | 生效但不能误称为右杆转写防抖；POV 分支读取物理左杆 | [S04] |
| I09 | 控制 tick | 配置 1000 Hz、legacy scheduler；dt 限 0.1–50 ms；绝对期限调度 | 生效；配置频率不等于实际频率，长 tick 不能靠后续循环补回真实游戏响应 | [S05] |
| V01 | VisionService 唤醒 / 保温 | active 200 Hz、idle 60 Hz；松开后 full-rate hold 1000 ms | 生效；保温不延长 AI 控制权限 | [S06] |
| V02 | 最新结果邮箱 | 单槽覆盖，epoch/aiming fence；无更新、旧 epoch、空闲结果清 authority | 生效；不同于排队逐帧回放 | [S06] |
| V03 | DXGI 捕获→GPU 预处理→TRT→解码 | 当前 640×512→480×384 等比缩放；输出坐标还原到捕获 ROI | 生效；tensor 像素和控制像素不能混用 | [S07] |
| V04 | detector 解码下限 | `conf<.40` 丢弃，输出按 [box,conf,class] 解释；本段没有另加 CPU NMS | 生效；engine 内部的检测/NMS 行为不由本次 C++ 静态审查证明 | [S08] |
| V05 | 颜色 ROI 回读与分类 | CPU 读取需要的 ROI；friendly 硬拒绝，敌方色标 bonus=10000；黄色 cue 可提供续接 | 条件生效；影响选择，也经 adapter 影响持续控制力度 | [S09] |
| V06 | motion anchor 相关匹配 | 10×10 patch；搜索半径 4–12 px；相关<.50失败、两次丢模板；≥.65 按 .12 更新 | 条件生效；当前 controller 只用 anchor 有无/score 决定一条 firing 速度分支，不直接用 anchor 坐标算速度 | [S09] |
| V07 | 发布接纳边界 | frame/source time 单调、非未来；Present 校准、epoch 一致；最大 source age 50 ms | 生效；RuntimeDeliveryGate 与 Controller 各保自己的边界，不宜为减少门直接删除 | [S06] [S12] |

## 视觉身份、选点和目标权重

| ID | 处理及条件 | 改变什么；当前数值 | 状态 / 审查判断 | 源码 |
|---|---|---|---|---|
| V08 | 类别、几何过滤 | class0；常规 H/W≥.85，wide-low≥.30，最大4.5；拾取高度≥8%/面积≥.3%，跟踪≥6%/≥.2% | 生效；检测框姿态/大小抖动可造成接纳状态切换 | [S09] |
| V09 | 置信度门 | 拾取 .65，敌色 .42，跟踪 .40；方向支持且 conf≥.50 可放宽拾取 | 生效；这些约束控制身份，不应统一乘到执行输出 | [S09] |
| V10 | 生存/尸体证据 | cue live +.30、tracking +.10、active +.05；无敌标宽低框 live -.25/risk+.60；旧敌标丢失 -.20/+.30；risk≥.85且live<.75拒绝，risk≥.50弱化 | 条件生效；主要控制筛选与排名，不能误当 controller 的同名 confidence | [S09] |
| V11 | 姿态选点和 R 区域 | 站姿 y=.35H，H/W<.65 时 y=.65H；R 水平内缩22%，垂直以该点上下 .18H / .22H | 生效；姿态阈值可能让源点跳变，随后被算成屏幕速度 | [S09] |
| V12 | 初选排名 | 距离优先容差；score含距离 -800、conf×400、身高×800、tracking最高2000、颜色10000、live×250、corpse -650、weak -1200；过大框有面积罚分 | 生效；分数只用于选择，不能与 [0,1] 可靠度混用 | [S09] |
| V13 | 方向选择 | 使用解释后的右杆，强度<.05不评分；方向一致才有最高700加分；当前目标纠正目的不投票换人 | 条件生效；15–30% 曲线下 .05 是下游选择门，不是 raw 转写死区 | [S05] [S09] |
| V14 | 身份粘连与替换 | IoU≥.12 或几何中心匹配；普通得分变化不能替换有效身份；明确 Handover 才允许方向对齐替换；失效先撤权 | 生效；保留以免跨目标拉扯，不宜简单删 tracking bonus | [S09] |
| V15 | 拾取范围与确认 | base150×(1+.75×目标高度占比)；一般2帧，足够可信的单个敌标目标可1帧 | 生效；确认时延依视觉节奏变化，帧数不是毫秒 | [S09] [S10] |
| V16 | 弱关联 .20–.40 | selector 有分支接续旧身份 | **常规生产 TRT 的 .40 解码下限使该低分输入不可达**；测试/其他生产者可以到达，勿当本路线已有低分保护 | [S08] [S09] |
| V17 | cue 几何续接 | 同 generation；最近 cue 间隔≤50 ms，总续接≤180 ms；无时间戳 fallback12帧；偏移 EMA .35；重建残差≤36 px | 条件生效；只延续身份/几何，不能重新索敌或训练运动 | [S09] |
| V18 | 视觉 fire zone | box水平收12%，顶部5%/底部15%，额外2 px边界；内部有4帧 miss grace | 条件生效；select_impl 对无权目标主动清 fire，不能据内部计数就声称丢目标还自动开4帧 | [S09] |
| A01 | selected point 协议 | 只有 selector 选中的 observation 得到 aim point/R；非选中框仅作为候选证据 | 生效；controller 不再自行选最近框 | [S11] |
| A02 | confidence 重写 | `clamp(detector.conf + color_bonus,0,1)`；随后直接成为 reliability | 生效；排序 bonus 进入物理控制和学习准入，是需优先拆开的语义耦合 | [S11] [S13] |
| A03 | cue-only 兼容构造 | 无 person candidate 时合成同身份 cue candidate；confidence=1，reliability另受目标大小限制 | 条件生效；小目标可能在 adapter 与 BodyLock 续接两处受缩放 | [S13] |

## 控制生命周期、几何、运动与学习

| ID | 处理及条件 | 改变什么；当前数值 | 状态 / 审查判断 | 源码 |
|---|---|---|---|---|
| C01 | ADS epoch 建立/重开 | ready/fire edge；已有 BodyLock 的 ready 请求等新帧，最多8帧；在 BodyLock范围内不重开 snap | 条件生效；影响是否进入135ms定位阶段 | [S14] |
| C02 | source 接纳 / selector generation | capture时效50ms、顺序去重；仅selected +合法R；cue必须同generation、单candidate、距旧源点≤80px | 生效；保护跨帧及跨目标一致性 | [S12] |
| C03 | ADS 等待 / admission | 等待目标220ms；实际 admission 使用 pickup150的尺寸扩展范围 | 生效；`ads_activation_radius=135`主要投影到诊断和 demand，并非另一个135px输出硬门 | [S12] |
| C04 | 替换 / 丢失 / 无发布 | 身份替换重置目标状态并消费已接纳snap；新鲜无目标撤力；无新发布持有同源点直到50ms；未完成ADS短暂同generation miss可保身份待恢复但不驱动 | 生效；无发布和新鲜空帧是不同事件 | [S12] |
| C05 | source、R 与 D 分离 | source夹到合法R；D保存区域内归一化位置；R变化会移动D；手动D不用于拟合source速度 | 生效；R变形可独立改变纠错误差 | [S15] |
| C06 | 自动 approach point | 仅初次admission、从中央上方接近，D最多上移R高度10%，保留2×完成半径 | 条件生效；另一处距离缩短，与姿态选点和ADS增益共同作用 | [S15] |
| C07 | 人手移动 D / 退出 | Correct目的才积分 `M*w*dt/180ms`；D夹[0,1]；边界持续≥50ms退出；开火向下不触发Y退出 | 条件生效；同一个人手既改D，也参与最终执行权仲裁 | [S15] |
| C08 | 屏幕速度 | 新source差分，observation dt夹1–100ms；目标替换清零；无新帧不投影source | 生效；box/姿态跳变与真实运动无法仅靠差分区分 | [S12] |
| C09 | firing 速度创新限制 | 75ms最近开火上下文；位移创新向量限3.5px；特定ADS/低anchor BodyLock分支需连续方向支持，否则首帧保持旧速度/反向清零 | 条件生效；限制的是速度证据而非新位置，二者动态不同 | [S12] [S13] |
| C10 | 加速度与速度限幅 | BodyLock或开火ADS速度变化≤3000px/s²×dt；速度每轴±4000；加速度诊断±20000 | 条件生效；刹车用到的速度可能比位置晚反向 | [S12] |
| C11 | ADS完成 | 半径8px、预测20ms、closing≤320px/s、连续3新帧；另有严格近中心穿越完成 | 生效；完成边界改变控制律，不能只查ADS输入曲线 | [S12] |
| C12 | ADS时间阶段 | nominal135ms→extension220ms→manual-safe；预算到期不制造到达，不自动减小求解器力度 | 生效；改变人手规则，AI solver可继续；不是最大总持续355ms | [S12] |
| C13 | BodyLock范围 | base180×(1+.75×观察到的高度占比)，范围外aim_authority=0 | 生效；失权即时raw，不为平滑延长旧目标权限 | [S12] |
| C14 | 视觉 authority | ADS接纳后1；BodyLock用reliability；无cue系数 .45/.35/.08/.24 由开关控制 | 当前 `visual_authority_enabled=false` **只关闭无cue额外系数**，未关闭reliability缩放 | [S12] |
| C15 | cue 力度 | BodyLock cue再乘 .35→1 的身高曲线（.12→.25）；ADS cue不做此缩放；禁止fire | 条件生效；加上A03和求解器预算，不能称只有一个cue nerf | [S13] |
| E01 | 响应命令账本 | 记录仲裁后、recoil前，经forward LUT的命令；以控制决策now计时；学习窗口向前移9ms | 生效；记录时未发送DS4，实际量化/发送失败不反馈账本 | [S13] |
| E02 | 通用响应学习 | 相邻命令差分拟合标量R；dt5–30ms，reliability≥.75，激励≥.04，R80–4000，单样本±50%，alpha .18/conf .08 | 条件生效；调用方把target_acceleration传0，所以声明的800加速度拒绝在此路线不起作用 | [S16] [S13] |
| E03 | 学习排除 | raw右杆模长≥.35、fire及之后75ms拒绝；无合法窗口/换目标重新积累pair | 生效；拒绝学习与停止BodyLock运动估计不是同一件事 | [S13] |
| E04 | 近目标/自由区模型 | 用目标半宽/半高定义椭圆；半径.65到1.35 smoothstep；两个R混合；learned R/conf跨目标保留 | 生效；是以几何近似游戏slowdown区域，未直接测游戏AA | [S16] |
| E05 | ADS专用响应模型 | 24anchor，至少3个有效斜率，IQR≤20%；alpha .04/conf .02按证据数增强；累计≥8且confidence≥.35才替代通用R | 条件生效；两个模型和接管门可能造成R变化；当前tick先选R后学习，更新下一tick才消费 | [S17] [S13] |
| E06 | BodyLock目标运动估计 | `motion=平均command+屏幕速度/R`（Y反号）；dt4–40ms，R50–4000；±1.25；2样本启动，3点中值；rise8/decay20/s；反向先归零；hold55ms | 生效；R/时延误差、开火相机运动可被解释成目标运动；confidence未作为solver的连续motion权重 | [S18] |
| E07 | 运动估计状态单位 | 内部保存normalized stick；消费时乘当前R变成px/s；不重标旧样本 | 生效；R变化会重解释旧状态，不能把它当独立固定单位的目标速度 | [S18] [S13] |
| E08 | 左移POV开火cue桥接 | raw左X≥.15；输入变化>.12失效；同目标/代/ADS epoch；仅保留55ms内非开火快照的向外水平运动，Y清零 | 条件生效；具象场景分支，重构应归到同一运动证据模型 | [S13] |

## 求解、执行权和最终输出

| ID | 处理及条件 | 改变什么；当前数值 | 状态 / 审查判断 | 源码 |
|---|---|---|---|---|
| U01 | AI solver 不加人手 | 调用前把generation intent右杆清零；solver产出期望总命令T | 生效；不能描述成通用的raw M + AI向量直接相加 | [S13] |
| U02 | ADS到达律 | 误差/(H×R)；H135ms，身高占比.18→.40把H缩到90ms；目标在上方时Y的H再×.84；12ms速度lookahead | 生效；CQB增益最大1.5，上方增益1/.84，均为曲线前、未饱和条件 | [S19] |
| U03 | ADS力包络 | 配置force×√2，但再与authority budget相交；当前budget1使两轴上限均为1，Y=.9也被此budget截到1 | 生效；√2不能直接解读为最终额外41%，当前Y=.9也并非最终少10% | [S19] [S21] |
| U04 | BodyLock到达律 | Hx=18/(.8×500)=45ms，Hy=18/(.6×500)=60ms；误差/(H×当前R)；有运动估计用1，无则屏幕速度×.72 | 生效；strength同时改变H和上限；`.tolerance_px=8`不是8px输出死区 | [S20] |
| U05 | 位置/运动方向约束 | normalized position .12为近中心范围；ADS反向lookahead最多抵消位置、不得穿零；BodyLock有效运动、fallback屏幕速度用不同包络 | 生效；此处消除/削弱刹车或前馈之后，后级shaper还有自身惯性 | [S21] |
| U06 | response LUT | 当前COD Dynamic legacy 20点径向LUT，参考stick .5；先按响应求解再逆映射；人手raw不经过该逆LUT | 生效；非DS4专属曲线，也没有自动切到DS4实测曲线 | [S22] |
| U07 | authority与椭圆饱和 | authority先乘linear request，映射后再限制每轴budget；椭圆超限统一缩放XY | 生效；证据既影响增益又影响上限，且两轴通过椭圆和径向LUT耦合 | [S21] |
| U08 | AI dynamics shaper | rise64/s、decay48/s，每tick最多.08；反向先卸到0再反向；目标变化清AI状态 | 生效；AI输出平滑会保存旧方向，raw人工直通不走它 | [S23] |
| U09 | 模式与cue专门整形 | ADS→BodyLock高旧值夹到新request±.08，否则限步.07；cue禁止同向增强；mouse专用timed ramp未启用 | 条件生效；不同状态有不同整形规则 | [S23] |
| U10 | 最终执行权状态机 | no-target/not-aiming/manual-exit直接raw；HandoverSeek忽略旧目标，直到新身份；新目标Capture可按2帧/timeout退出 | 生效；撤权需要即时，不能全局给final加低通 | [S24] |
| U11 | ADS人手规则 | nominal有AI工作就target-first；开火向下保护、延长/manual-safe反向允许人手；候选用15–30%的w过渡 | 条件生效；w不是所有模式一律线性混合的总开关 | [S24] |
| U12 | BodyLock协同 | AI微小/无工作时raw；同向取足够总命令；对向阻尼.35，向下.10，开火向下0；evidence=(authority-.65)/.35夹0–1 | 生效；方向符号和力度条件仍会切路径 | [S24] |
| U13 | 斜向耦合 / release | 另一轴/本轴比1.25→3提高AI权；向下不走coupling；gamepad release权=1-w；held acquire有独立保护 | 条件生效；15–30%轴向规则不等于径向强度规则 | [S24] |
| U14 | fresh位置优先 | fresh且非cue、非D correction、AI朝误差方向时，再按误差/settle radius提高AI权 | 条件分支；单独探针证实fresh/held可不同，但正常D/held-acquire有旁路，实战可达性未证实 | [S24] |
| U15 | AutoFire最终许可 | fresh观察目标、physical ADS ready、fire zone请求、误差≤16px、AI residual阈值6000/32767、2帧；人手≥8%可豁免residual检查 | 条件生效；fire许可不等于aim许可 | [S25] |
| U16 | AutoFire时序 / 接管 | pulse30/100ms；手动接管35+85ms保护；physical fire保持原值 | 条件生效；这些脉冲又影响75ms firing上下文 | [S25] |
| U17 | 触摸宏 | 右上x≥.75,y≤.5按住30/100ms连发，松手清；右下入区三角press20/gap20/press20ms，一次入区一次 | 生效；右下松触不截断已启动双击；触摸fire压制普通AutoFire填缝并排除recoil | [S26] |
| U18 | 压枪 | enabled且普通fire时固定Y=-.20（夹feedback min/max）；无目标/非ADS也能发生；触摸fire除外 | 生效；当前非识别枪械曲线、也不是闭环自适应recoil | [S27] |
| U19 | OutputComposer | raw seed→覆盖右杆→OR fire/max RT→加recoil并逐轴clip→辅助按钮→最终clip | 生效；AI包络不能约束之后增加的recoil；clip改变真实可实现命令 | [S28] |
| U20 | DS4协议 | 8bit轴127/128双侧线性量化、Y反号；十字、四键、肩键、trigger、Share/Options、L3/R3、PS和触摸按压 | 生效；触摸坐标/gyro不透传，扩展报告将触点置抬起、传感器置零 | [S29] |
| U21 | 发送与退出 | 每tick ViGEm update_ex；返回delivered/reconnect；退出/异常先neutral再等worker；重连先发neutral | 生效；驱动接收成功不等于游戏已消费，发送失败也未撤销E01账本 | [S30] [S05] |

## 容易误认成 buff/nerf 的旁路

| ID | 字段/路径 | 本路线真实状态 | 源码 |
|---|---|---|---|
| D01 | `operation_class/direction_trust/recoil_pull_strength` | 在最终仲裁之后分类，当前只填诊断；不反过来调力度 | [S13] |
| D02 | `ads_demand/bodylock_demand`、Jump/Fall标签 | plan字段/诊断，无本路线solver按这些标签额外加力 | [S12] [S19] [S20] |
| D03 | `bodylock_exit_radius_px` | 有构造赋值，未找到消费；不要据它声称存在退出距离迟滞 | [S12] [S13] |
| D04 | `ai_delta_gain/target_max_age_ms` | 仍被AutoFire readiness使用，不是当前右杆AI增益/target生存主门 | [S25] |
| D05 | `tracker.aim_height_ratio=.30`、旧 TargetGeometry 类 | 当前selected point由Vision `.35/.65`拥有；tracker比率进入viewport配置，而dynamic viewport关闭；旧geometry实现不在当前主controller求解调用链 | [S05] [S11] [S15] |
| D06 | recoil profile/recognizer/despike参数 | 当前controller成员为RecoilReducer；profile/recognizer生产启用已退休，不能据配置长列表推断运行复杂压枪模型 | [S27] [S13] |
| D07 | enemy mark / dynamic viewport / mouse专用路径 | 当前前两项关闭；鼠标point tolerance、deadzone、加减速和judgment不属于gamepad有效输出路径 | [S05] [S13] |
| D08 | `ai_proposal_*` 退休旋钮 | 当前链路同tick执行，不恢复异步AI提案；提频/降频不能跳过权限与生命周期 | [S05] |

## 源码索引

[S01]: D:/work/AI/yolo-study-001/native/runtime_app/runtime_loop.cpp:1298
[S02]: D:/work/AI/yolo-study-001/native/controller_native/sdl_gamepad_reader.cpp:404
[S03]: D:/work/AI/yolo-study-001/native/controller_native/input_edge_reducer.h:53
[S04]: D:/work/AI/yolo-study-001/native/controller_native/intent_filter.cpp:9
[S05]: D:/work/AI/yolo-study-001/native/runtime_app/runtime_loop.cpp:643
[S06]: D:/work/AI/yolo-study-001/native/runtime_app/vision_service.cpp:85
[S07]: D:/work/AI/yolo-study-001/native/vision_native/src/vision_engine.cpp:214
[S08]: D:/work/AI/yolo-study-001/native/vision_native/src/tensorrt_engine.cpp:492
[S09]: D:/work/AI/yolo-study-001/native/vision_native/src/target_selector.cpp:18
[S10]: D:/work/AI/yolo-study-001/native/pipeline_contract/target_acquisition.h:12
[S11]: D:/work/AI/yolo-study-001/native/runtime_app/vision_controller_adapter.cpp:85
[S12]: D:/work/AI/yolo-study-001/native/controller_native/target_coordinator.cpp:204
[S13]: D:/work/AI/yolo-study-001/native/controller_native/native_gamepad_controller.cpp:591
[S14]: D:/work/AI/yolo-study-001/native/controller_native/ads_reacquisition_reducer.cpp:10
[S15]: D:/work/AI/yolo-study-001/native/controller_native/target_state_reducers.cpp:56
[S16]: D:/work/AI/yolo-study-001/native/controller_native/aim_response_estimator.cpp:46
[S17]: D:/work/AI/yolo-study-001/native/controller_native/ads_response_estimator.cpp:47
[S18]: D:/work/AI/yolo-study-001/native/controller_native/bodylock_target_motion_observer.cpp:22
[S19]: D:/work/AI/yolo-study-001/native/controller_native/ads_acquisition_controller.cpp:24
[S20]: D:/work/AI/yolo-study-001/native/controller_native/bodylock_follow_controller.cpp:21
[S21]: D:/work/AI/yolo-study-001/native/controller_native/response_model_aim_solver.cpp:18
[S22]: D:/work/AI/yolo-study-001/native/controller_native/aim_response_curve_plugin.h:132
[S23]: D:/work/AI/yolo-study-001/native/controller_native/aim_dynamics_shaper.cpp:12
[S24]: D:/work/AI/yolo-study-001/native/controller_native/assist_control_state_machine.cpp:5
[S25]: D:/work/AI/yolo-study-001/native/controller_native/auto_fire_gate.cpp:140
[S26]: D:/work/AI/yolo-study-001/native/controller_native/touchpad_fire.h:12
[S27]: D:/work/AI/yolo-study-001/native/controller_native/recoil_reducer.cpp:9
[S28]: D:/work/AI/yolo-study-001/native/controller_native/output_composer.cpp:193
[S29]: D:/work/AI/yolo-study-001/native/controller_native/ds4_output_report.h:21
[S30]: D:/work/AI/yolo-study-001/native/controller_native/virtual_gamepad.cpp:232
