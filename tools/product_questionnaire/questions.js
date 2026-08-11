(() => {
  "use strict";

  const option = (value, label, description = "", tag = "", exclusive = false) => ({ value, label, description, tag, exclusive });

  const yesNo = [
    option("yes", "是", "把它作为明确的产品能力或规则。"),
    option("no", "否", "明确不提供，避免实现时自行猜测。")
  ];

  const immediateOrSmooth = [
    option("immediate", "立即释放", "停止 AI 施力，下一帧就把完整控制权交还用户。"),
    option("bounded", "极短且有上限的平滑释放", "允许消除跳变，但必须定义最长交接时间。"),
    option("contextual", "按场景决定", "不同退出原因采用不同释放规则。")
  ];

  window.QUESTIONNAIRE_SPEC = {
    schemaVersion: "aim-product-questionnaire/v1",
    project: "yolo-study-001",
    title: "瞄准控制产品定义问卷",
    principles: [
      "先定义用户能感受到的行为，再讨论用什么算法实现。",
      "一个目标身份 I、一个期望瞄准位置或区域 D、一个最终输出 T。",
      "基准测试服务于产品验收，不能反过来替产品定义成功。"
    ],
    sections: [
      {
        id: "positioning",
        title: "产品定位",
        kicker: "01 / WHY",
        description: "先确定它到底是在帮助谁、替用户做什么，以及哪些事永远不该替用户做。",
        questions: [
          {
            id: "product_role",
            depth: "core",
            type: "single",
            required: true,
            title: "这套产品最准确的角色是什么？",
            context: "这会决定后面所有“AI 和人工冲突时谁优先”的答案。",
            options: [
              option("intent_executor", "用户决定意图，AI 帮忙执行", "用户决定何时瞄准、瞄谁、何时脱离；AI 负责更稳定地到达并维持。", "建议起点"),
              option("assist_blend", "持续辅助，但始终以人工为主", "AI 只改变手感和误差，不拥有目标。"),
              option("autonomous_lock", "自动锁定和跟随", "启用后 AI 可以主导目标选择与持续跟随。"),
              option("selectable", "提供多种可切换模式", "面向不同用户提供不同控制权边界。")
            ]
          },
          {
            id: "primary_promise",
            depth: "core",
            type: "single",
            required: true,
            title: "如果只能把一件事做到最好，最核心的承诺是什么？",
            context: "其他指标只能在不破坏这个承诺时优化。",
            options: [
              option("controllability", "可控、听话", "用户想修正或退出时，系统立即理解。", "建议优先"),
              option("accuracy", "最终命中更准", "以屏幕上的实际误差和命中表现为主。"),
              option("stability", "持续跟随更稳", "减少抖动、脱钩和反复拉扯。"),
              option("speed", "获取目标更快", "优先缩短从启用到进入有效瞄准的时间。"),
              option("automation", "减少人工操作", "让系统承担更多选择和控制。")
            ]
          },
          {
            id: "target_user",
            depth: "core",
            type: "multi",
            required: true,
            title: "V1 首先服务哪类用户？",
            context: "可多选，但请只选真正需要在 V1 里照顾的人群。",
            options: [
              option("owner_expert", "你本人／熟练调参者", "允许学习成本和高级配置。"),
              option("advanced", "熟悉手柄射击的高级玩家", "希望强大，但不能依赖阅读代码。"),
              option("general", "普通手柄玩家", "默认设置必须安全、直观。"),
              option("research", "研发与算法验证", "主要价值是可重复实验和数据分析。")
            ]
          },
          {
            id: "allowed_contexts",
            depth: "core",
            type: "multi",
            required: true,
            title: "允许在哪些环境使用和验收？",
            context: "产品边界会影响真实游戏测试、数据留存和发布方式。",
            options: [
              option("private_training", "私人训练／靶场", "在可控环境里测试行为。"),
              option("offline", "离线或本地模式", "不涉及在线对局。"),
              option("custom_lobby", "自定义房间", "通过固定场景复现实验。"),
              option("live_match", "真实对局", "需要应对不可控地图、玩家和网络条件。"),
              option("dev_replay", "仅开发回放", "只做离线回放和算法研究。")
            ]
          },
          {
            id: "user_owned_decisions",
            depth: "core",
            type: "multi",
            required: true,
            title: "哪些决定必须永远由用户拥有？",
            context: "选中的项目即使牺牲成绩，也不能被 AI 偷偷接管。",
            options: [
              option("activation", "何时启用辅助", "没有用户触发就不介入。"),
              option("identity", "瞄准哪个人", "AI 不得擅自换人。"),
              option("aimpoint", "瞄身体哪个位置", "允许用户在同一目标内主动修正。"),
              option("escape", "何时退出或脱钩", "用户有明确且及时的退出权。"),
              option("fire", "何时开火", "AI 不替用户扣扳机。"),
              option("none", "没有绝对保留项", "启用后可以让 AI 全权决策。", "", true)
            ]
          },
          {
            id: "autonomous_decisions",
            depth: "detail",
            type: "multi",
            title: "AI 可以自主承担哪些决定？",
            options: [
              option("rank", "候选目标排序"),
              option("predict", "预测目标运动"),
              option("stabilize", "消除检测与输出抖动"),
              option("recoil", "补偿后坐力"),
              option("reacquire", "短暂丢失后重获同一目标"),
              option("switch", "当前目标失效后换目标"),
              option("fire", "自动开火")
            ]
          },
          {
            id: "non_goals",
            depth: "detail",
            type: "textarea",
            title: "V1 明确不做什么？",
            context: "写出看起来有用、但现阶段会让产品失焦或架构膨胀的能力。",
            placeholder: "例如：不做全自动作战、不训练额外人体模型、不追求跨所有游戏即插即用……"
          }
        ]
      },
      {
        id: "scope",
        title: "范围与模式",
        kicker: "02 / SCOPE",
        description: "把 V1 的功能集合和不支持的场景框住，防止重构时把所有可能性一起背上。",
        questions: [
          {
            id: "v1_capabilities",
            depth: "core",
            type: "multi",
            required: true,
            title: "V1 必须形成闭环的能力有哪些？",
            context: "只有选中的能力才需要进入首轮产品规格和验收门槛。",
            options: [
              option("detect", "检测可瞄目标"),
              option("select", "选择并保持目标身份"),
              option("acquire", "进入 ADS 后获取目标"),
              option("track", "持续跟随目标"),
              option("manual_intent", "理解人工修正与退出"),
              option("cue", "遮挡或机瞄遮挡下短时维持"),
              option("recoil", "开火时压枪"),
              option("autofire", "自动开火"),
              option("telemetry", "可复盘遥测与回放验收")
            ]
          },
          {
            id: "v1_priority",
            depth: "core",
            type: "single",
            required: true,
            title: "首个真正可验收的纵向切片是什么？",
            context: "这是重构后第一条必须从画面走到手柄输出的完整路径。",
            options: [
              option("ads_track_manual", "ADS 获取 + 跟随 + 人工修正／退出", "先证明最基本的人机协作闭环。", "建议"),
              option("ads_track_cue", "再包含 cue 遮挡维持", "把机瞄遮挡作为首轮必要能力。"),
              option("ads_track_recoil", "再包含开火与压枪", "首轮就覆盖交火时的纵向控制。"),
              option("full", "所有 V1 能力一起验收", "只有完整链路通过才算有价值。")
            ]
          },
          {
            id: "hipfire_scope",
            depth: "core",
            type: "single",
            required: true,
            title: "腰射状态应该怎样处理？",
            options: [
              option("passthrough", "完全不辅助，原样透传", "腰射不进入目标控制状态。", "低风险"),
              option("selection_only", "只允许预选目标", "不输出瞄准量，但可为 ADS 做准备。"),
              option("assist", "提供独立的腰射辅助", "需要单独定义触发、强度和验收。")
            ]
          },
          {
            id: "input_devices",
            depth: "core",
            type: "single",
            required: true,
            title: "V1 的输入设备边界是什么？",
            options: [
              option("controller_only", "只支持手柄", "把控制语义和测试资源集中在手柄。", "建议聚焦"),
              option("controller_future_mouse", "V1 手柄，架构预留鼠标", "产品只验收手柄，但接口避免彻底锁死。"),
              option("both", "手柄和鼠标都必须支持", "两种输入需要分别定义人机控制权。")
            ]
          },
          {
            id: "game_scope",
            depth: "core",
            type: "single",
            required: true,
            title: "首轮产品要覆盖多少游戏？",
            options: [
              option("one", "先把一个游戏做对", "共享核心保持通用，但只承诺一个真实游戏通过。", "建议"),
              option("few_profiles", "两到三个游戏配置", "用少量不同游戏验证核心没有写死。"),
              option("universal", "尽量开箱支持多数游戏", "必须接受更复杂的校准、检测和配置。")
            ]
          },
          {
            id: "manual_fallback",
            depth: "core",
            type: "single",
            required: true,
            title: "任何 AI 条件不成立时，默认退路是什么？",
            options: [
              option("raw_passthrough", "立即回到原始手柄输入", "不保留旧目标或旧输出。", "安全默认"),
              option("last_output_decay", "短暂衰减上一次输出", "减少突跳，但可能产生残余控制。"),
              option("neutral", "输出归零", "不会继续 AI 施力，但也可能吞掉人工输入。")
            ]
          },
          {
            id: "movement_scope",
            depth: "detail",
            type: "multi",
            title: "V1 必须覆盖哪些动态场景？",
            options: [
              option("stationary", "双方静止"),
              option("target_strafe", "目标横向移动"),
              option("target_vertical", "目标纵向移动／跳跃"),
              option("player_strafe", "玩家自身横移"),
              option("camera_fast", "快速转身或甩枪"),
              option("mixed", "双方同时运动")
            ]
          }
        ]
      },
      {
        id: "activation",
        title: "启用、接管与释放",
        kicker: "03 / AUTHORITY",
        description: "定义 AI 什么时候有权介入、什么时候必须让路，以及交接时不能出现什么感觉。",
        questions: [
          {
            id: "activation_trigger",
            depth: "core",
            type: "single",
            required: true,
            title: "什么动作代表用户正式请求瞄准辅助？",
            options: [
              option("lt_threshold", "LT 超过固定阈值", "简单、明确、可测试。"),
              option("ads_confirmed", "游戏画面确认已进入 ADS", "更贴合实际画面，但引入识别延迟。"),
              option("lt_and_visual", "LT 请求 + 画面确认", "分成准备阶段与正式接管阶段。", "建议考虑"),
              option("toggle", "独立按键或开关", "辅助状态与 ADS 不完全绑定。")
            ]
          },
          {
            id: "ads_transition_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "LT 已按下、但 ADS 动画尚未稳定时，AI 应该做什么？",
            options: [
              option("observe_only", "只观察和预选，不输出", "等画面可用后再开始控制。", "稳妥"),
              option("limited_output", "允许受限预瞄", "提前移动，但必须限制力度和目标资格。"),
              option("full_output", "立即按正式模式输出", "追求速度，承担动画和视野变化误判风险。")
            ]
          },
          {
            id: "lt_release_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "用户松开 LT 时怎样交还控制权？",
            options: immediateOrSmooth
          },
          {
            id: "emergency_override",
            depth: "core",
            type: "single",
            required: true,
            title: "用户如何无条件抢回控制权？",
            options: [
              option("stick_threshold", "右摇杆强输入立即抢回", "最自然，但要避免把普通修正误判为退出。"),
              option("dedicated_button", "独立按键／组合键", "明确但增加操作负担。"),
              option("lt_release", "只需松开 LT", "规则最简单，但按住 ADS 时不能紧急抢回。"),
              option("both", "强输入和松 LT 都可以", "提供两条独立安全路径。", "建议")
            ]
          },
          {
            id: "no_target_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "辅助已启用但没有可信目标时，输出应该是什么？",
            options: [
              option("raw_passthrough", "完整人工输入", "系统不凭空产生瞄准量。", "建议"),
              option("slowdown", "只提供通用减速", "无目标身份也会改变手感。"),
              option("hold_last", "短时保持最后方向", "可能造成用户拉不动或幽灵控制。")
            ]
          },
          {
            id: "held_ads_reacquire",
            depth: "core",
            type: "single",
            required: true,
            title: "用户持续按住 ADS、当前目标消失后，系统可以重新找目标吗？",
            options: [
              option("same_only", "只允许重获同一目标", "身份连续，找不到就回人工。", "建议起点"),
              option("new_with_intent", "用户再次给出方向意图后可找新目标", "换目标需要新的用户证据。"),
              option("auto_new", "可自动选择新目标", "效率高，但有擅自换人的风险。"),
              option("require_retrigger", "必须松开并重新按 ADS", "最明确，但操作更慢。")
            ]
          },
          {
            id: "release_latency_limit",
            depth: "detail",
            type: "number",
            title: "从退出意图出现到 AI 不再施力，最长能接受多少毫秒？",
            context: "这是用户主观感到“拉得动”的关键上限。",
            placeholder: "例如 50",
            unit: "ms",
            min: 0,
            max: 1000
          },
          {
            id: "authority_handover_feel",
            depth: "detail",
            type: "textarea",
            title: "理想的接管与释放手感，用普通话描述是什么？",
            placeholder: "例如：AI 可以帮我贴近，但我稍微推一下就能在人体范围内移动；明显推开时不能粘住……"
          }
        ]
      },
      {
        id: "targeting",
        title: "目标资格与选择",
        kicker: "04 / IDENTITY",
        description: "先确认“谁才是目标”，再谈怎么瞄。身份选错时，后面的控制再平滑也没有意义。",
        questions: [
          {
            id: "hostile_evidence",
            depth: "core",
            type: "multi",
            required: true,
            title: "什么证据足以把一个检测对象认定为可瞄目标？",
            context: "可多选。后续需要定义证据如何组合，而不是只看一个检测框。",
            options: [
              option("body_detection", "人物／身体检测"),
              option("hostile_marker", "敌方 UI 标记或颜色"),
              option("motion_continuity", "与已知目标的运动连续性"),
              option("game_specific_cue", "游戏专属敌我线索"),
              option("history", "连续多帧历史"),
              option("manual_direction", "用户瞄准方向与选择意图")
            ]
          },
          {
            id: "friendly_rule",
            depth: "core",
            type: "single",
            required: true,
            title: "出现友方证据时，规则有多硬？",
            options: [
              option("hard_reject", "硬拒绝，绝不瞄友方", "即使会漏掉敌人，也优先避免误锁。", "建议"),
              option("confidence_tradeoff", "与敌方证据综合打分", "在证据冲突时允许继续。"),
              option("ignore", "不处理敌我", "只把视觉人物当作候选。")
            ]
          },
          {
            id: "partial_target",
            depth: "core",
            type: "single",
            required: true,
            title: "只露出头、肩或很小身体区域的敌人，是否可成为新目标？",
            options: [
              option("yes_same_rules", "可以，沿用正常资格"),
              option("yes_stricter", "可以，但需要更强证据", "避免把环境或 UI 误认为人。", "建议"),
              option("track_only", "只能延续已有目标，不能新建"),
              option("no", "不可成为目标")
            ]
          },
          {
            id: "cue_only_acquisition",
            depth: "core",
            type: "single",
            required: true,
            title: "只有 cue、没有直接人体证据时，能否创建一个全新目标？",
            options: [
              option("never", "不能，只能延续同一已知目标", "防止 UI 或机瞄遮挡线索凭空造人。", "当前安全边界"),
              option("strong_only", "强 cue 可以创建新目标", "需要定义哪些 cue 足够可靠。"),
              option("yes", "可以按普通候选参与选择", "获取更快，但误锁风险最高。")
            ]
          },
          {
            id: "first_target_ranking",
            depth: "core",
            type: "single",
            required: true,
            title: "多个合格目标同时出现时，第一目标主要按什么选？",
            options: [
              option("crosshair", "最靠近准星", "结果直观、容易解释。"),
              option("manual_vector", "最符合用户右摇杆方向", "把人工意图作为主要选择证据。", "建议"),
              option("weighted", "距离、置信度、方向综合排序", "更灵活，但必须可解释、可回放。"),
              option("sticky_previous", "优先最近一次目标", "连续性强，但可能妨碍换人。")
            ]
          },
          {
            id: "multi_target_intent",
            depth: "core",
            type: "single",
            required: true,
            title: "用户朝另一个目标明显推摇杆时，系统应怎样理解？",
            options: [
              option("switch_intent", "视为换目标意图", "越过明确阈值后释放旧目标并选择新目标。", "建议"),
              option("escape_only", "只退出旧目标，不自动选新目标", "让用户重新触发。"),
              option("same_target_correction", "仍视为当前目标内修正", "除非旧目标失效，不主动换人。"),
              option("ignore", "锁定期间忽略", "最粘，但容易与用户对抗。")
            ]
          },
          {
            id: "automatic_switch",
            depth: "core",
            type: "single",
            required: true,
            title: "在没有明确人工换人意图时，AI 何时可以切换目标身份？",
            options: [
              option("never", "绝不自动切换", "当前身份失效就回人工。", "最可控"),
              option("invalid_only", "旧目标已确定失效时", "不能只是另一个目标分数更高。"),
              option("better_margin", "新目标明显更优时", "需要分数差和稳定时间。"),
              option("always_best", "持续选择当前最优目标", "成绩可能更高，但容易跳人。")
            ]
          },
          {
            id: "identity_continuity",
            depth: "core",
            type: "single",
            required: true,
            title: "什么情况下仍算“同一个人”？",
            options: [
              option("strict_track", "只有明确的跟踪身份连续", "宁可丢失，不猜测替换。"),
              option("spatiotemporal", "位置、速度、外观和时间综合连续", "允许短暂漏检后恢复。", "建议"),
              option("nearest", "回到附近的人就算同一目标", "实现简单，但多人交错时易串人。")
            ]
          },
          {
            id: "target_edge_cases",
            depth: "detail",
            type: "textarea",
            title: "哪些目标边界情况需要单独规则？",
            context: "例如倒地、死亡动画、屏幕边缘、低置信度、烟雾、多人交错。",
            placeholder: "逐条写出场景和希望的结果。"
          }
        ]
      },
      {
        id: "aimpoint",
        title: "人体目标位置",
        kicker: "05 / DESIRED POINT",
        description: "这里定义的是用户想把准星放到哪里，不先假设必须用检测框、关键点或 Body 模型实现。",
        questions: [
          {
            id: "aim_representation",
            depth: "core",
            type: "single",
            required: true,
            title: "系统追求的是一个固定点，还是一个可接受区域？",
            options: [
              option("region", "人体上的可接受区域", "进入区域后以稳定和人工自由为主。", "建议"),
              option("point", "人体上的精确点", "持续把准星拉向唯一中心。"),
              option("region_with_point", "区域 + 区域内偏好点", "外部快速纠偏，内部柔和维持。")
            ]
          },
          {
            id: "default_body_region",
            depth: "core",
            type: "single",
            required: true,
            title: "默认应该瞄人体哪个位置？",
            options: [
              option("head", "头部"),
              option("upper_chest", "上胸", "兼顾命中面积和爆头可能。"),
              option("chest", "胸部中心"),
              option("contextual", "按可见部位和武器决定"),
              option("user_config", "由用户设置")
            ]
          },
          {
            id: "posture_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "站立、蹲下、趴下或跳跃时，目标位置应怎样变化？",
            options: [
              option("anatomy_relative", "跟随真实人体部位", "无论框形状怎么变，都尽量保持同一解剖区域。", "产品目标"),
              option("visible_region", "跟随当前可见人体区域", "只保证落在可见轮廓内。"),
              option("box_ratio", "按检测框固定比例", "成本低，但姿态和遮挡下可能错位。")
            ]
          },
          {
            id: "head_only_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "目标只露出头部时，期望点在哪里？",
            options: [
              option("visible_center", "可见头部中心"),
              option("lower_head", "头部偏下", "降低越过轮廓的风险。"),
              option("do_not_engage", "不进入主动控制"),
              option("profile", "按游戏／武器配置")
            ]
          },
          {
            id: "same_target_micro_adjust",
            depth: "core",
            type: "single",
            required: true,
            title: "锁在同一个人身上时，用户能否用右摇杆上下左右调整落点？",
            context: "参考案例里，用户想向下拉但被系统顶住，最后加大力度才脱钩。这个问题必须有明确答案。",
            options: [
              option("free_within_region", "可以，在人体允许区域内自由调整", "AI 保持身份，不强行拉回原点。", "针对案例"),
              option("offset_then_hold", "可以，形成新的人工偏移并维持", "松手后仍保留用户选择的落点。"),
              option("temporary", "可以，但松手后缓慢回默认点"),
              option("no", "不可以，AI 始终保持默认点")
            ]
          },
          {
            id: "micro_adjust_persistence",
            depth: "core",
            type: "single",
            required: true,
            title: "人工调整过的人体内落点，什么时候重置？",
            options: [
              option("target_change", "更换目标身份时", "同一个人期间持续保留。", "建议"),
              option("ads_release", "松开 ADS 时"),
              option("stick_release", "右摇杆回中时"),
              option("timed", "一段时间后回默认"),
              option("never_auto", "只由用户显式复位")
            ]
          },
          {
            id: "aimpoint_bounds",
            depth: "core",
            type: "single",
            required: true,
            title: "人工落点偏移能否超出当前人体范围？",
            options: [
              option("clamp_then_escape", "先限制在人体内，继续推则转为退出／换人意图", "需要清晰的两段手感。", "建议"),
              option("free", "可以自由移出，身份仍保持", "不会突然脱钩，但可能对空跟随。"),
              option("hard_clamp", "永远夹在人体内", "最粘，可能出现拉不动。"),
              option("immediate_escape", "一越界就释放目标")
            ]
          },
          {
            id: "firing_aimpoint",
            depth: "core",
            type: "single",
            required: true,
            title: "开火后，期望落点是否应该变化？",
            options: [
              option("same", "保持用户当前落点", "压枪只抵消扰动，不改意图。", "清晰边界"),
              option("safer_region", "自动移向更稳的身体区域"),
              option("weapon_profile", "按武器配置"),
              option("adaptive", "按命中反馈动态调整")
            ]
          },
          {
            id: "cue_aimpoint_source",
            depth: "core",
            type: "single",
            required: true,
            title: "进入 cue 遮挡维持时，纵向目标位置从哪里来？",
            context: "如果 cue 的高度或坐标算错，AI 可能错误抵消人工下拉。",
            options: [
              option("freeze_last_desired", "冻结遮挡前最后有效的人体落点", "cue 只帮助延续身份和运动，不重算人体高度。", "建议"),
              option("predict_last_geometry", "按最后人体几何和运动预测"),
              option("cue_geometry", "直接由 cue 自己的高度和位置计算", "必须证明 cue 坐标与人体语义一致。"),
              option("release_vertical", "cue 期间不主动控制纵轴", "把纵向完全交回用户。")
            ]
          },
          {
            id: "aimpoint_evidence_cost",
            depth: "detail",
            type: "single",
            title: "为了得到稳定的人体落点，可以接受多大的感知成本？",
            context: "这是实现约束，不等于现在就决定上 Body／Pose 模型。",
            options: [
              option("existing_only", "只能使用现有检测输出", "不新增训练和推理成本。"),
              option("light_geometry", "允许轻量几何／时序估计", "先用已有框、可见区域和历史修正。", "建议先验证"),
              option("extra_model_if_proven", "证明确有必要后可增加 Body／Pose 模型", "先用失败案例证明收益再付训练成本。"),
              option("best_quality", "效果优先，可直接增加专用模型")
            ]
          }
        ]
      },
      {
        id: "intent",
        title: "用户意图与人工修正",
        kicker: "06 / HUMAN INTENT",
        description: "摇杆不是一股简单相加的力，而是用户在表达微调、换人、逃离或压枪意图。这里把这些语义分开。",
        questions: [
          {
            id: "small_input_meaning",
            depth: "core",
            type: "single",
            required: true,
            title: "锁定期间的小幅右摇杆输入默认表示什么？",
            options: [
              option("aimpoint_adjust", "调整同一目标内的落点", "身份不变，D 随用户移动。", "建议"),
              option("blend_force", "与 AI 输出按比例混合", "不区分意图，只做数值合成。"),
              option("noise", "视为噪声并抑制", "更稳，但用户可能觉得拉不动。"),
              option("escape_start", "开始退出当前目标")
            ]
          },
          {
            id: "sustained_input_meaning",
            depth: "core",
            type: "single",
            required: true,
            title: "朝同一方向持续推摇杆，但仍在人体范围内，应该怎样响应？",
            options: [
              option("move_desired", "持续移动期望落点 D", "AI 继续帮助跟随新落点。", "建议"),
              option("increase_blend", "逐渐增加人工权重"),
              option("release_ai", "超过时间就释放 AI"),
              option("resist", "只要目标仍有效就抵消输入")
            ]
          },
          {
            id: "strong_input_same_target",
            depth: "core",
            type: "single",
            required: true,
            title: "强输入出现、附近又没有其他目标时，应怎样处理？",
            options: [
              option("escape", "立即视为退出意图", "不要求先证明有新目标。", "安全"),
              option("move_then_escape", "先移动到人体边界，继续推才退出", "同一手势可以完成修正和脱离。", "自然"),
              option("stay_locked", "仍保持当前目标", "只在目标失效时退出。")
            ]
          },
          {
            id: "opposing_input_correct_ai",
            depth: "core",
            type: "single",
            required: true,
            title: "AI 正确朝期望落点移动时，用户给出反方向输入，谁优先？",
            context: "这里不能只靠“AI 非零就覆盖人工”这种数值规则。",
            options: [
              option("human_changes_desired", "人工优先，并改变期望落点", "系统认为用户知道自己想去哪。", "产品可控性优先"),
              option("contextual", "小输入修正，大输入退出", "按强度和持续时间解释不同意图。"),
              option("ai_until_ready", "AI 优先，直到到达当前点"),
              option("blend", "双方按权重抵消")
            ]
          },
          {
            id: "opposing_input_wrong_ai",
            depth: "core",
            type: "single",
            required: true,
            title: "AI 的目标位置本身算错时，用户怎样纠正它？",
            options: [
              option("direct_override", "普通幅度即可直接纠正", "不需要先用很大力量打破锁定。", "针对案例"),
              option("explicit_escape", "先退出，再由人工重新瞄"),
              option("confidence_based", "AI 证据弱时容易纠正，证据强时更粘"),
              option("strong_override", "必须达到较大阈值")
            ]
          },
          {
            id: "axis_semantics",
            depth: "core",
            type: "single",
            required: true,
            title: "横轴和纵轴的人工意图是否要分别解释？",
            options: [
              option("separate", "是，两个轴可处于不同控制状态", "例如横向继续跟人，纵向允许用户下拉。", "适合案例"),
              option("vector", "否，始终把摇杆当成一个二维整体", "任一方向退出就一起释放。"),
              option("mode_dependent", "按场景决定", "cue、压枪、换人使用不同轴规则。")
            ]
          },
          {
            id: "handover_continuity",
            depth: "core",
            type: "single",
            required: true,
            title: "AI 与人工控制权变化时，最不能出现什么？",
            options: [
              option("jump", "输出突然跳变", "交接必须连续。"),
              option("resistance", "人工明显推了却拉不动", "响应意图优先于轨迹平滑。", "建议"),
              option("lag", "释放后仍残留 AI 力量"),
              option("overshoot", "为脱离而加力后突然甩过头", "参考案例中的脱钩问题。")
            ]
          },
          {
            id: "explicit_escape_action",
            depth: "core",
            type: "multi",
            required: true,
            title: "哪些操作明确表示“不要再控制这个目标”？",
            options: [
              option("lt_up", "松开 LT"),
              option("strong_stick", "右摇杆越过强输入阈值"),
              option("leave_body", "期望落点被推离人体区域"),
              option("flick_other", "朝另一个候选快速甩动"),
              option("button", "独立取消键"),
              option("target_invalid", "目标身份失效")
            ]
          },
          {
            id: "left_stick_semantics",
            depth: "detail",
            type: "single",
            title: "左摇杆移动是否也应作为瞄准意图证据？",
            options: [
              option("compensate_only", "只用于补偿玩家自身运动"),
              option("selection_intent", "也用于推断想靠近／绕开哪个目标"),
              option("ignore", "完全不参与瞄准决策")
            ]
          },
          {
            id: "intent_threshold_feel",
            depth: "detail",
            type: "textarea",
            title: "微调、退出和换人的手感边界应该怎样描述？",
            placeholder: "不要先写算法阈值；写“轻推时怎样、中推时怎样、快速大推时怎样”。"
          }
        ]
      },
      {
        id: "tracking",
        title: "获取与持续跟随",
        kicker: "07 / TRACKING",
        description: "目标不是让曲线看起来漂亮，而是让屏幕上的准星在真实运动、帧率和延迟下仍然听话。",
        questions: [
          {
            id: "tracking_success",
            depth: "core",
            type: "single",
            required: true,
            title: "“跟住了”最重要的判断是什么？",
            options: [
              option("screen_region", "准星持续落在期望人体区域", "以最终画面结果为准。", "建议"),
              option("center_error", "到目标点的平均误差最小"),
              option("command_smooth", "输出摇杆曲线足够平滑"),
              option("hit_result", "实际命中率／击杀表现")
            ]
          },
          {
            id: "inside_region_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "准星已经进入可接受区域后，AI 应该怎样做？",
            options: [
              option("minimum_needed", "只输出维持所需的最小量", "给人工留出空间，避免追着噪声跑。", "建议"),
              option("center", "继续拉向区域中心"),
              option("zero", "完全停止输出"),
              option("match_velocity", "主要匹配目标速度，不纠正小位置误差")
            ]
          },
          {
            id: "target_motion_response",
            depth: "core",
            type: "single",
            required: true,
            title: "目标开始移动时，优先追求什么？",
            options: [
              option("stay_in_region", "先保证不离开人体区域", "允许区域内有小误差。", "建议"),
              option("zero_lag", "尽可能零延迟贴住目标点"),
              option("smoothness", "保持输出平滑，即使短暂落后"),
              option("predictive", "主动预测并领先")
            ]
          },
          {
            id: "player_motion_response",
            depth: "core",
            type: "single",
            required: true,
            title: "玩家自己横移导致准星相对目标变化时，系统应怎样处理？",
            options: [
              option("compensate", "把它当成需要补偿的相对运动", "仍围绕用户选择的 D 跟随。"),
              option("intent", "把它当成用户想改变瞄准"),
              option("mixed", "结合左右摇杆和画面结果判断", "需要真实场景验收。", "建议")
            ]
          },
          {
            id: "abrupt_motion",
            depth: "core",
            type: "single",
            required: true,
            title: "目标突然跳跃、滑铲或快速变向时，宁可出现哪种不足？",
            options: [
              option("brief_lag", "短暂落后，但不猛拉", "稳定和可预测优先。"),
              option("brief_overshoot", "可以略过冲，但尽快追上"),
              option("temporary_release", "证据不稳时短暂让回人工"),
              option("contextual", "按置信度决定")
            ]
          },
          {
            id: "jitter_latency_tradeoff",
            depth: "core",
            type: "single",
            required: true,
            title: "抖动和响应延迟冲突时，偏向哪边？",
            options: [
              option("response", "宁可有少量细动，也不能感觉迟钝", "必须通过视觉结果限制抖动。"),
              option("balanced", "两者都有硬上限", "任何一边都不能无限换另一边。", "建议"),
              option("smoothness", "宁可慢一点，也要输出很稳")
            ]
          },
          {
            id: "acquire_track_transition",
            depth: "core",
            type: "single",
            required: true,
            title: "从快速获取切换到稳定跟随时，应该有什么可感知变化？",
            options: [
              option("seamless", "用户不应感觉到模式切换", "力度可以变，但轨迹和控制权连续。", "建议"),
              option("snap_then_slow", "允许明显吸附后减速"),
              option("uniform", "始终使用相同控制特性")
            ]
          },
          {
            id: "max_force_feel",
            depth: "core",
            type: "single",
            required: true,
            title: "AI 达到最大输出时，用户应该感受到什么？",
            options: [
              option("bounded_help", "只是帮助到上限，不保证追上", "不能为了追踪而无限放大或吞人工。", "建议"),
              option("must_follow", "只要目标有效就应尽量追上"),
              option("release_on_saturation", "持续饱和后退出 AI")
            ]
          },
          {
            id: "tracking_edge_cases",
            depth: "detail",
            type: "textarea",
            title: "还有哪些跟随场景必须单独验收？",
            placeholder: "例如低帧率、检测隔帧、镜头震动、FOV 切换、目标穿过准星、左右反复变向……"
          }
        ]
      },
      {
        id: "cue",
        title: "cue、遮挡与生命周期",
        kicker: "08 / OCCLUSION",
        description: "cue 只能弥补直接证据的短暂中断。它是否能控制、能控制多久、能否开火，都要单独拍板。",
        questions: [
          {
            id: "cue_role",
            depth: "core",
            type: "single",
            required: true,
            title: "cue 的产品角色是什么？",
            options: [
              option("same_target_bridge", "短时延续同一已知目标", "不创建新身份，不改变用户选择。", "当前边界"),
              option("independent_detector", "可独立发现和控制目标"),
              option("visual_hint_only", "只记遥测，不参与控制"),
              option("remove", "V1 不保留 cue")
            ]
          },
          {
            id: "cue_entry",
            depth: "core",
            type: "multi",
            required: true,
            title: "进入 cue 维持必须同时满足哪些条件？",
            options: [
              option("had_direct", "刚才存在直接人体目标"),
              option("same_generation", "属于同一次 ADS／目标生命周期"),
              option("temporal_close", "中断时间足够短"),
              option("spatial_consistent", "位置与速度连续"),
              option("cue_confident", "cue 本身置信度达标"),
              option("user_holds", "用户仍保持启用意图")
            ]
          },
          {
            id: "cue_duration_policy",
            depth: "core",
            type: "single",
            required: true,
            title: "cue 最长维持时间应该怎样定义？",
            options: [
              option("hard_short", "固定且很短的硬上限", "到期无条件释放。", "安全起点"),
              option("confidence_decay", "随证据衰减，仍有硬上限"),
              option("while_visible", "只要 cue 仍在就继续"),
              option("until_ads_up", "一直到用户松开 ADS")
            ]
          },
          {
            id: "cue_manual_behavior",
            depth: "core",
            type: "single",
            required: true,
            title: "cue 期间用户向下／向侧面拉摇杆时，AI 应怎样响应？",
            context: "这可能正是参考案例里“下拉拉不动”的根因之一。",
            options: [
              option("manual_updates_desired", "人工直接更新落点，cue 只维持身份／运动", "cue 不能用错误坐标顶住用户。", "建议"),
              option("release_axis", "有输入的轴立即交给人工"),
              option("exit_cue", "任何明显输入都退出 cue"),
              option("blend", "人工与 cue 控制继续混合"),
              option("cue_priority", "cue 仍优先")
            ]
          },
          {
            id: "cue_stray_detection",
            depth: "core",
            type: "single",
            required: true,
            title: "附近出现不属于当前人的 cue 或人物检测时，怎么处理？",
            options: [
              option("ignore_until_release", "忽略，不改变当前身份", "除非用户换人或当前身份失效。", "建议"),
              option("rank_switch", "重新排序并可能切换"),
              option("release_ambiguous", "身份变得模糊就回人工")
            ]
          },
          {
            id: "direct_reappearance",
            depth: "core",
            type: "single",
            required: true,
            title: "直接人体证据重新出现时，cue 怎样退出？",
            options: [
              option("seamless_same", "确认同一身份后无缝切回", "D 和输出不跳变。", "建议"),
              option("reacquire", "按一次新获取重新计算"),
              option("delay_confirm", "连续确认若干帧再切回")
            ]
          },
          {
            id: "cue_loss_release",
            depth: "core",
            type: "single",
            required: true,
            title: "cue 也消失或到期时，怎样释放？",
            options: immediateOrSmooth
          },
          {
            id: "cue_fire_rule",
            depth: "core",
            type: "single",
            required: true,
            title: "只有 cue、没有直接人体证据时，允许触发自动开火吗？",
            options: [
              option("never", "绝不允许", "cue 只能瞄准维持，不能开火。", "当前安全边界"),
              option("strong_only", "高置信 cue 可以"),
              option("same_as_direct", "与直接检测相同")
            ]
          },
          {
            id: "cue_max_duration_ms",
            depth: "detail",
            type: "number",
            title: "cue 维持的硬上限是多少？",
            placeholder: "例如 180",
            unit: "ms",
            min: 0,
            max: 5000
          },
          {
            id: "cue_game_specificity",
            depth: "detail",
            type: "single",
            title: "cue 规则需要按游戏配置到什么程度？",
            options: [
              option("generic_lifecycle", "生命周期通用，只有识别参数按游戏"),
              option("full_profile", "识别、坐标、时长和控制都按游戏"),
              option("per_weapon", "还要细到武器／机瞄类型"),
              option("avoid_profiles", "不接受专门适配，应移除无法通用的 cue")
            ]
          }
        ]
      },
      {
        id: "fire_recoil",
        title: "开火与后坐力",
        kicker: "09 / FIRE",
        description: "瞄准、扣扳机和压枪是三种不同权限。把它们拆开定义，避免一个状态顺便接管所有输出。",
        questions: [
          {
            id: "autofire_scope",
            depth: "core",
            type: "single",
            required: true,
            title: "自动开火是否属于 V1？",
            options: [
              option("no", "不属于，始终由用户开火", "先把瞄准控制闭环做对。", "建议聚焦"),
              option("optional", "属于可选模式，默认关闭"),
              option("yes", "属于核心能力")
            ]
          },
          {
            id: "autofire_trigger",
            depth: "core",
            type: "multi",
            required: true,
            dependsOn: { id: "autofire_scope", notEquals: "no" },
            title: "允许自动开火时，哪些条件必须同时成立？",
            options: [
              option("direct_target", "存在直接人体证据"),
              option("identity_stable", "目标身份稳定"),
              option("inside_fire_region", "准星进入开火区域"),
              option("dwell", "保持足够时间"),
              option("user_armed", "用户持续给出开火授权"),
              option("hostile", "敌方证据通过"),
              option("not_cue", "不是 cue-only 状态")
            ]
          },
          {
            id: "physical_fire_passthrough",
            depth: "core",
            type: "single",
            required: true,
            title: "用户物理扣下开火键时，系统能否阻止或延迟？",
            options: [
              option("never_block", "不能，必须原样及时透传", "AI 可以辅助瞄准，但不否定用户明确开火。", "建议"),
              option("safety_block", "不满足目标安全条件时可以阻止"),
              option("aim_delay", "可以短暂延迟到准星就绪"),
              option("mode_based", "由用户选择模式")
            ]
          },
          {
            id: "recoil_scope",
            depth: "core",
            type: "single",
            required: true,
            title: "压枪模块的职责是什么？",
            options: [
              option("disturbance_only", "只抵消武器造成的画面扰动", "不改变用户选择的目标落点。", "建议边界"),
              option("keep_center", "把准星持续拉回默认人体点"),
              option("weapon_pattern", "执行武器专属反向轨迹"),
              option("not_v1", "V1 暂不包含压枪")
            ]
          },
          {
            id: "manual_down_while_firing",
            depth: "core",
            type: "single",
            required: true,
            title: "开火时用户主动向下拉，默认表示什么？",
            options: [
              option("user_aimpoint", "用户想把落点下移", "压枪不能把它当后坐力全部抵消。", "针对案例"),
              option("manual_recoil", "用户在自己压枪", "减少 AI 压枪量，但不改变 D。"),
              option("infer_context", "结合画面运动判断是哪一种", "需要明确证据和失败退路。"),
              option("ai_priority", "仍由 AI 保持默认落点")
            ]
          },
          {
            id: "unknown_weapon_fallback",
            depth: "core",
            type: "single",
            required: true,
            title: "无法识别武器或压枪参数缺失时，怎样退化？",
            options: [
              option("disable_recoil", "关闭压枪，保留其他瞄准能力", "不猜测错误力度。", "安全默认"),
              option("generic_low", "使用低强度通用补偿"),
              option("last_profile", "沿用上一个武器配置"),
              option("disable_all", "关闭全部辅助")
            ]
          },
          {
            id: "fire_ready_definition",
            depth: "detail",
            type: "textarea",
            dependsOn: { id: "autofire_scope", notEquals: "no" },
            title: "怎样才叫“可以开火”？",
            placeholder: "描述允许区域、稳定时间、敌我证据、目标运动、遮挡和例外。"
          },
          {
            id: "weapon_adaptation",
            depth: "detail",
            type: "multi",
            title: "压枪允许依赖哪些适配信息？",
            options: [
              option("weapon_profile", "武器配置表"),
              option("optic_profile", "瞄具／倍率"),
              option("visual_feedback", "实时画面反馈"),
              option("user_calibration", "用户校准"),
              option("online_learning", "运行中自适应学习")
            ]
          }
        ]
      },
      {
        id: "safety",
        title: "故障、安全与配置",
        kicker: "10 / FAIL SAFE",
        description: "任何输入过期、身份不清、设备异常或配置错误时，都要有一致且可验证的退路。",
        questions: [
          {
            id: "fresh_no_target",
            depth: "core",
            type: "single",
            required: true,
            title: "当前帧明确没有目标时，能否继续使用旧目标输出？",
            options: [
              option("no", "不能，除非进入有界 cue 生命周期", "普通跟踪不得偷偷复用过期目标。", "建议"),
              option("short_hold", "可短时保持"),
              option("until_replaced", "保留到出现新目标或松 ADS")
            ]
          },
          {
            id: "stale_data_release",
            depth: "core",
            type: "single",
            required: true,
            title: "视觉帧、目标或控制状态超过时限时，怎样处理？",
            options: [
              option("passthrough", "立即停止 AI 并透传人工", "过期数据不再拥有控制权。", "安全默认"),
              option("decay", "短时衰减输出"),
              option("hold", "保持最近输出等待恢复")
            ]
          },
          {
            id: "ambiguous_identity",
            depth: "core",
            type: "single",
            required: true,
            title: "多人交错导致身份不确定时，怎样处理？",
            options: [
              option("release", "回到人工，不猜", "宁可短暂失去辅助。", "建议"),
              option("freeze", "短时冻结原目标位置"),
              option("best_candidate", "选择当前最像的候选")
            ]
          },
          {
            id: "friendly_appears",
            depth: "core",
            type: "single",
            required: true,
            title: "当前目标位置出现友方强证据时，怎样处理？",
            options: [
              option("immediate_release", "立即释放并禁止开火", "友方安全优先。", "建议"),
              option("confirm", "连续确认后释放"),
              option("aim_only", "可继续瞄准但禁止自动开火"),
              option("ignore", "维持原身份")
            ]
          },
          {
            id: "vision_failure",
            depth: "core",
            type: "single",
            required: true,
            title: "截屏、推理或视觉线程异常时，主控制链路应该怎样做？",
            options: [
              option("passthrough_alive", "保持手柄透传，禁用 AI 并明确报警", "辅助故障不能让基础输入失效。", "建议"),
              option("stop_output", "停止所有虚拟手柄输出"),
              option("restart_hold", "自动重启期间维持旧输出")
            ]
          },
          {
            id: "controller_failure",
            depth: "core",
            type: "single",
            required: true,
            title: "物理手柄或虚拟手柄异常时，产品必须做到什么？",
            options: [
              option("neutral_and_alert", "输出安全归零、停止 AI、明确报警", "不得留下卡住的摇杆或按键。", "建议"),
              option("retry", "自动重连并尽量维持状态"),
              option("exit", "立即退出程序")
            ]
          },
          {
            id: "unknown_config",
            depth: "core",
            type: "single",
            required: true,
            title: "配置项拼错、缺失或版本不兼容时，应该怎样启动？",
            options: [
              option("fail_closed", "阻止相关能力启动，并说清错误", "避免静默使用错误默认值。", "建议"),
              option("warn_defaults", "警告并使用默认值"),
              option("ignore", "忽略未知或无效配置")
            ]
          },
          {
            id: "telemetry_failure",
            depth: "core",
            type: "single",
            required: true,
            title: "遥测写盘失败时，能否影响实时控制？",
            options: [
              option("never_block", "不能阻塞控制，但必须报警并记录丢失", "实时链路与观测链路隔离。", "建议"),
              option("stop_session", "停止本次辅助，保证每次运行都有证据"),
              option("ignore", "静默继续")
            ]
          },
          {
            id: "kill_switch",
            depth: "detail",
            type: "textarea",
            title: "是否需要独立总开关、状态指示和异常恢复流程？",
            placeholder: "描述用户如何知道 AI 正在控制、怎样一键关闭、故障后是否自动恢复。"
          }
        ]
      },
      {
        id: "adaptation",
        title: "不同游戏与用户设置",
        kicker: "11 / ADAPTATION",
        description: "通用的应该是控制语义和生命周期；游戏差异可以进入数据配置，但不能无限复制整套算法。",
        questions: [
          {
            id: "first_game",
            depth: "core",
            type: "text",
            required: true,
            title: "第一个必须真正做好的游戏和模式是什么？",
            context: "写到可复现的粒度，例如游戏、训练场／模式、视角和输入设备。",
            placeholder: "例如：Call of Duty: Black Ops 7，训练场，手柄，第一人称"
          },
          {
            id: "shared_core_boundary",
            depth: "core",
            type: "single",
            required: true,
            title: "跨游戏的代码边界应该是什么？",
            options: [
              option("shared_engine_profiles", "一套共享状态机／控制核心 + 游戏数据配置", "差异进入标定、识别和参数，不复制控制逻辑。", "建议"),
              option("game_modules", "共享基础设施 + 每游戏策略模块", "允许游戏拥有独立行为实现。"),
              option("separate", "每个游戏单独实现", "适配自由，但维护和一致性成本最高。"),
              option("universal_no_profile", "不接受游戏配置，必须完全通用")
            ]
          },
          {
            id: "per_game_data",
            depth: "core",
            type: "multi",
            required: true,
            title: "哪些差异允许放进游戏配置？",
            options: [
              option("capture_roi", "采集区域／分辨率／FOV"),
              option("visual_cues", "敌我与 cue 视觉定义"),
              option("coordinates", "坐标换算与 ADS 缩放"),
              option("response_curve", "摇杆响应曲线与死区"),
              option("body_region", "默认人体瞄准区域"),
              option("timing", "ADS、遮挡和生命周期时间"),
              option("weapons", "武器／瞄具参数"),
              option("control_semantics", "人工意图和控制权语义")
            ]
          },
          {
            id: "calibration_flow",
            depth: "core",
            type: "single",
            required: true,
            title: "新增游戏或用户设置时，校准应该怎么完成？",
            options: [
              option("guided_tool", "由引导工具采集并验证", "减少手工改配置和隐含知识。", "建议长期形态"),
              option("maintainer_profile", "维护者手工制作配置"),
              option("auto_detect", "运行时自动识别和校准"),
              option("code_change", "允许修改代码后重新编译")
            ]
          },
          {
            id: "user_settings",
            depth: "core",
            type: "multi",
            required: true,
            title: "普通用户真正需要看到哪些设置？",
            context: "不选的高级参数应由配置、标定或合理默认值承担。",
            options: [
              option("strength", "总体辅助强度"),
              option("aim_region", "瞄准部位"),
              option("stick_sensitivity", "游戏内灵敏度／曲线"),
              option("manual_freedom", "人工修正自由度"),
              option("recoil", "压枪开关／强度"),
              option("autofire", "自动开火开关"),
              option("game_profile", "游戏配置选择"),
              option("none", "V1 不提供用户调参", "", "", true)
            ]
          },
          {
            id: "advanced_settings_policy",
            depth: "core",
            type: "single",
            required: true,
            title: "算法细节参数应该怎样暴露？",
            options: [
              option("hidden", "默认隐藏，只提供产品级设置", "避免把内部缺陷变成用户调参责任。", "建议"),
              option("advanced_mode", "放在高级模式并附解释"),
              option("all", "全部开放给用户"),
              option("config_only", "只允许维护者编辑配置文件")
            ]
          },
          {
            id: "missing_profile",
            depth: "core",
            type: "single",
            required: true,
            title: "进入未适配游戏或配置不匹配时，产品怎么做？",
            options: [
              option("refuse_assist", "不启用 AI，只保留安全透传并提示", "不能假装通用。", "建议"),
              option("generic_safe", "启用低强度通用模式"),
              option("best_guess", "自动猜测最近配置"),
              option("user_choose", "要求用户手动确认使用哪个配置")
            ]
          },
          {
            id: "adaptation_cost_limit",
            depth: "detail",
            type: "textarea",
            title: "每增加一个游戏，最多能接受多少人工、素材和训练成本？",
            context: "把“针对性适配”从模糊担忧变成可以衡量的产品约束。",
            placeholder: "例如：允许 2 小时标定和少量截图，不接受重新训练检测模型；或允许专项数据集……"
          },
          {
            id: "profile_versioning",
            depth: "detail",
            type: "multi",
            title: "游戏配置需要哪些维护能力？",
            options: [
              option("version", "版本号和兼容检查"),
              option("import_export", "导入／导出"),
              option("rollback", "一键回滚"),
              option("validation", "启动前自动验证"),
              option("provenance", "记录来源和对应游戏版本")
            ]
          }
        ]
      },
      {
        id: "acceptance",
        title: "验收与发布",
        kicker: "12 / PROOF",
        description: "这部分决定 benchmark 要模拟什么、什么证据算数，以及线上看起来提升但实际更差时如何拦住发布。",
        questions: [
          {
            id: "hard_failures",
            depth: "core",
            type: "multi",
            required: true,
            title: "哪些现象一旦出现，不管总分多高都必须判失败？",
            options: [
              option("wrong_identity", "定位／跟随错人"),
              option("manual_blocked", "用户想修正却拉不动", "参考案例的前半段。"),
              option("detach_overshoot", "为抢回控制加力后突然脱钩甩飞", "参考案例的后半段。"),
              option("auto_switch", "无意图换人"),
              option("stale_control", "使用过期目标继续施力"),
              option("cue_fire", "cue-only 状态开火"),
              option("friendly", "瞄准／攻击友方"),
              option("input_loss", "辅助故障导致原始手柄输入丢失"),
              option("output_spike", "异常输出尖峰或卡键")
            ]
          },
          {
            id: "primary_outcome_metric",
            depth: "core",
            type: "single",
            required: true,
            title: "首要成绩必须测量哪一层结果？",
            options: [
              option("screen_effect", "游戏画面中的准星—人体关系", "衡量最终发生了什么，而不只是发了什么命令。", "建议"),
              option("controller_output", "虚拟手柄输出曲线"),
              option("model_error", "检测／预测误差"),
              option("hit_stats", "命中率、击杀等游戏结果"),
              option("human_rating", "真人主观手感")
            ]
          },
          {
            id: "benchmark_loop_level",
            depth: "core",
            type: "single",
            required: true,
            title: "自动 benchmark 至少要闭环到哪一层？",
            context: "现有表面测试的问题，是只评价中间量，没有模拟“输出如何改变下一帧画面”。",
            options: [
              option("closed_loop_plant", "画面／目标 → 全链路 → 手柄输出 → 游戏响应模型 → 下一帧画面", "用可标定的游戏响应模型形成闭环。", "建议核心"),
              option("hardware_loop", "再经过真实虚拟手柄和采集链路", "覆盖调度、延迟和设备行为。"),
              option("replay_open_loop", "视频回放 → 全链路 → 输出，但不反馈到画面", "可回归感知与决策，无法单独证明控制手感。"),
              option("live_only", "只承认真游戏自动化／真人测试")
            ]
          },
          {
            id: "benchmark_scenarios",
            depth: "core",
            type: "multi",
            required: true,
            title: "首版仿真必须覆盖哪些场景族？",
            options: [
              option("acquisition", "ADS 获取静止／移动目标"),
              option("tracking", "双方运动下持续跟随"),
              option("micro_adjust", "同一人体内上下左右修正"),
              option("escape", "主动退出与换人"),
              option("cue", "机瞄遮挡和 cue 坐标偏差"),
              option("recoil", "开火、后坐力与人工下拉"),
              option("identity", "多人交错和身份连续"),
              option("failure", "延迟、丢帧、过期数据和设备故障"),
              option("frame_variance", "不同帧率／采集延迟／推理节奏")
            ]
          },
          {
            id: "benchmark_human_input",
            depth: "core",
            type: "single",
            required: true,
            title: "仿真中的“用户摇杆意图”从哪里来？",
            options: [
              option("scripted_semantic", "按语义编排的输入脚本", "明确标记微调、换人、退出等意图，结果可重复。", "建议主干"),
              option("recorded_trace", "真实玩家输入录制", "手感真实，但需要和画面、状态精确对齐。"),
              option("human_model", "参数化用户反应模型", "可大规模扫参，但要先校准。"),
              option("mixed", "语义脚本 + 真人录制 + 参数化模型", "分别覆盖可解释性、真实性和规模。")
            ]
          },
          {
            id: "benchmark_ground_truth",
            depth: "core",
            type: "multi",
            required: true,
            title: "每个仿真案例必须带哪些真值或预期？",
            options: [
              option("identity", "正确目标身份 I"),
              option("acceptable_region", "每帧可接受人体区域／落点 D"),
              option("human_intent", "用户当时的意图标签"),
              option("authority", "每帧应由 AI、人工或混合控制哪些轴"),
              option("lifecycle", "预期状态与进入／退出时刻"),
              option("forbidden", "禁止动作和硬失败"),
              option("timing", "允许的响应时间窗口"),
              option("screen_motion", "游戏对输入的响应／下一帧画面运动")
            ]
          },
          {
            id: "evidence_hierarchy",
            depth: "core",
            type: "single",
            required: true,
            title: "发布判断时，不同证据的优先级是什么？",
            options: [
              option("gates_then_score_live", "硬失败门禁 → 闭环仿真 → 硬件回放 → 固定真游戏场景 → 真人手感", "任何上游硬门禁失败都不能被平均分抵消。", "建议"),
              option("benchmark_primary", "自动 benchmark 总分为主"),
              option("live_primary", "真实游戏表现为主，benchmark 只诊断"),
              option("human_primary", "真人主观评价为主")
            ]
          },
          {
            id: "promotion_rule",
            depth: "core",
            type: "single",
            required: true,
            title: "候选版本满足什么条件才能替换当前基线？",
            options: [
              option("all_gates_no_regress", "所有硬门禁通过，关键场景无回退，主指标再提升", "总分提升不能掩盖单个严重退化。", "建议"),
              option("weighted_score", "加权总分高于基线"),
              option("live_win", "少量真实对局体验更好"),
              option("owner_decision", "由维护者综合判断，不设固定规则")
            ]
          },
          {
            id: "incident_regression",
            depth: "core",
            type: "single",
            required: true,
            title: "真实游戏里发现一次严重问题后，是否必须先变成固定回归案例才能算解决？",
            options: [
              option("must", "必须，先有能稳定失败的 RED 案例，再修复", "防止同类问题下次复发。", "建议"),
              option("critical_only", "只有关键故障需要"),
              option("optional", "尽量做，但不阻塞修复"),
              option("no", "视频确认修好即可")
            ]
          },
          {
            id: "run_provenance",
            depth: "core",
            type: "multi",
            required: true,
            title: "每次成绩必须绑定哪些运行信息，才允许比较？",
            options: [
              option("commit", "代码 commit／工作区状态"),
              option("config", "完整配置及哈希"),
              option("model", "模型版本及哈希"),
              option("scenario", "场景集版本"),
              option("hardware", "GPU、显示器、手柄和驱动"),
              option("timing", "帧率、延迟和线程调度摘要"),
              option("video", "同步视频证据"),
              option("telemetry", "逐帧遥测")
            ]
          },
          {
            id: "live_trial_requirement",
            depth: "core",
            type: "single",
            required: true,
            title: "自动测试全部通过后，还需要怎样的真实游戏验收？",
            options: [
              option("fixed_plus_free", "固定场景复现 + 一段自由实战", "既可比较，也能发现未建模问题。", "建议"),
              option("fixed_only", "只跑固定动作脚本"),
              option("free_only", "只做自由体验"),
              option("none", "自动测试通过即可发布")
            ]
          },
          {
            id: "acquisition_p95_ms",
            depth: "detail",
            type: "number",
            title: "从有效启用到进入目标区域，P95 最长多少毫秒？",
            unit: "ms",
            min: 0,
            max: 5000
          },
          {
            id: "manual_ack_p95_ms",
            depth: "detail",
            type: "number",
            title: "从人工修正意图出现到屏幕开始按意图变化，P95 最长多少毫秒？",
            unit: "ms",
            min: 0,
            max: 2000
          },
          {
            id: "pipeline_latency_p95_ms",
            depth: "detail",
            type: "number",
            title: "从画面采集到虚拟手柄提交，P95 最长多少毫秒？",
            context: "这只是链路预算，不等于最终屏幕响应。",
            unit: "ms",
            min: 0,
            max: 1000
          },
          {
            id: "metric_threshold_notes",
            depth: "detail",
            type: "textarea",
            title: "还有哪些验收指标必须给出数值门槛？",
            placeholder: "例如：区域内占比、脱离过冲、目标切换次数、输出反转次数、丢帧率、真人盲测胜率……"
          }
        ]
      },
      {
        id: "examples",
        title: "真实案例与补充",
        kicker: "13 / REALITY",
        description: "最后用真实感觉和失败案例校准前面的抽象答案。这些材料会直接成为规格示例和回归测试来源。",
        questions: [
          {
            id: "ideal_behavior",
            depth: "core",
            type: "textarea",
            required: true,
            title: "请描述一次“完全正确”的使用过程。",
            context: "从发现人、按 ADS、AI 介入、你微调、目标移动、开火到退出，按时间顺序说。",
            placeholder: "用普通人话描述你希望手上和画面上发生什么。"
          },
          {
            id: "reference_incident",
            depth: "core",
            type: "textarea",
            required: true,
            title: "参考视频 7 秒开始的案例，正确行为应该是什么？",
            context: "已知现象是：定位没落到人身上；你想向下拉却拉不动；最后加大力度导致脱钩。请补充每一步正确结果。",
            placeholder: "例如：AI 初始点错了以后……当我轻微向下拉时……到达人体区域后……如果我继续大力拉时……"
          },
          {
            id: "worst_incidents",
            depth: "core",
            type: "textarea",
            required: true,
            title: "再列出 3—5 个你最想消灭的真实问题。",
            context: "每条尽量写“当时场景 → 你做了什么 → 系统做错什么 → 正确结果”。",
            placeholder: "1. …\n2. …\n3. …"
          },
          {
            id: "never_behaviors",
            depth: "core",
            type: "multi",
            required: true,
            title: "无论如何优化，都不能让产品出现哪些感觉？",
            options: [
              option("fight_user", "和用户对抗／拉不动"),
              option("sticky", "该放手时还粘住"),
              option("detach_snap", "脱钩时突然甩飞"),
              option("jitter", "围绕目标来回抖"),
              option("laggy", "输入像隔了一层"),
              option("wrong_target", "擅自换人或锁错人"),
              option("opaque", "出错后无法解释为什么"),
              option("config_fragile", "换配置就出现不可预测行为")
            ]
          },
          {
            id: "reference_feel",
            depth: "detail",
            type: "textarea",
            title: "有没有接近理想手感的游戏、产品、版本或历史录像？",
            placeholder: "写名称、版本、录像路径或大致时间点，以及具体好在哪里。"
          },
          {
            id: "delegated_decisions",
            depth: "detail",
            type: "textarea",
            title: "哪些问题你愿意让我在 review 后按原则替你做默认决定？",
            placeholder: "例如：内部文件格式、遥测字段、默认数值初稿、测试目录结构……"
          },
          {
            id: "additional_notes",
            depth: "detail",
            type: "textarea",
            title: "还有哪些前面没有问到、但会改变产品方向的条件？",
            placeholder: "硬件限制、性能预算、维护人数、发布时间、不能更改的依赖、已有素材等。"
          }
        ]
      }
    ]
  };
})();
