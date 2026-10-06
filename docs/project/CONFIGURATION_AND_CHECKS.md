# 配置项与检查工作流

GUI 参数同时跨越三个边界：配置文档需要保存它，原生程序需要读取并应用它，界面需要说明它怎样影响操作。过去新增一个字段，需要分别修改表单列表、范围校验、原生键列表、默认值输出和热加载白名单，漏改任何一处都会出现“能保存但不生效”或“两边允许的范围不同”。

新增数值参数现在登记在 `native/controller_native/editable_parameters.json`。其中包括 L2 阈值、两阶段半径、归一化输出上限、独立响应时间、腰射保留比例、压枪直接输出、手动意图阈值及开火节奏；标记 legacy 的项仅用于旧配置校验与转换，不进入 GUI。GUI 读取名称、范围、页面、分组、输入方式、说明和默认值，原生构建由 CMake 生成常量、读取路径、边界校验、配置输出、热加载与学习保留规则。原生运行时不读取这个 JSON，也不依赖 Python。

## 增加一个数值参数

1. 在原生配置结构增加字段，用目录中生成的 `k<Constant>` 作为默认值；在控制算法的实际拥有者使用该字段。
2. 在 JSON 的 `parameters` 中登记字段路径、相对 `GamepadRuntimeConfig` 的成员路径、常量名称、默认值、上下界和 GUI 信息。现阶段该目录支持手柄配置中的浮点标量，枚举、设备身份和结构项继续使用已有接入方式。
   需要显示百分比时可登记 `display_scale: 100` 和 `unit: "%"`；界面在输入边界换算，校验提示和参数说明显示相同单位。原生、配置文件、草稿与热加载仍使用原数值，例如界面的 30% 对应保存的 0.3。切换配置或打开旧配置不会因为单位变化改写数据。
3. 有跨字段限制时加入 `relations`。目前支持严格小于 `lt` 和小于等于 `le`，GUI 和原生都检查关系。L2 预搜索阈值必须低于开镜辅助阈值，两个手动阈值不允许重合或反序，按住时长不得大于连发周期。
4. 确认是否允许热加载及是否保留响应学习。热加载赋值自动生成，但算法缓存、执行器和状态机仍需在所属组件中应用新值。已有的意图过滤器重配置保留手势状态；自动开火门重配置取消旧脉冲。
5. 运行 `--build --scope config`，随后为实际控制行为补回归和随机场景。配置检查能证明字段被接受和范围一致，不能证明算法已经正确使用它。

GUI 会自动收集这些字段，写入独立配置 JSON 和运行 TOML。`page` 默认为 `assist`，L2 与范围相关项设为 `ads`；相应页面按分组登记生成输入控件。高级项的搜索会展开对应区域；默认参数页保留 12 项常用参数，跟随力度集中到范围页。旧参数尚未全部迁入目录，特殊路径也不能仅靠登记变成安全的热加载项。

ADS 预览还跨越一个边界：图示必须与原生目标选择使用相同几何规则。共享规则位于 `native/pipeline_contract/target_acquisition.h`，Vision 使用它计算普通及宽矮目标的瞄点、回退区域，以及新目标和跟踪目标的尺寸 / 外形条件。原生只读入口 `--describe-ads-geometry` 输出规则和参考结果，在配置、模型、设备与输出初始化前返回。GUI 后台读取规则，预览计算只用于绘图；Python 回归逐项比较 35 组原生参考结果，包括外形分类临界点和尺寸准入结果。改动规则时同时检查生产 Vision 与预览，不在 GUI 中另设控制策略。

## 用一个入口检查应用

在项目根目录执行：

```powershell
D:\env\python\python.exe python/tools/check_app.py --build
```

该命令先构建原生程序和普通测试，再运行原生行为检查与全部桌面检查。任一步失败都会返回失败状态，测试产物放在 `runs/app-checks/`。不启动游戏，不启用原生输出，不修改个人配置。

| 命令选项 | 检查范围 |
| --- | --- |
| `--scope config` | 原生配置、字段边界、热加载约束、手动意图区间、开火门及 Python 配置检查；不创建 Tk 窗口 |
| `--scope gui` | 真实 Tk 交互、配置隔离、菜单键盘与滚轮、曲线编辑和 IPC 展示；使用独立桌面 |
| 默认 `--scope all` | 原生控制器基础与功能测试，以及完整桌面测试 |
| `--build` | 检查前构建 Release；参数目录变更会触发 CMake 重新生成 |

只验证一个 GUI 回归时，可设置 `PYTHONPATH=python;python/tests` 后执行：

```powershell
D:\env\python\python.exe -m desktop_app.test_desktop -m unittest test_desktop_workspace.DesktopWorkspaceTests.test_catalog_advanced_parameters_search_persist_relations_and_native_defaults
```

## 为什么不再抢屏幕和焦点

旧测试在用户桌面创建 `Toplevel`，菜单测试又调用 `focus_force`，因此会让测试窗口和菜单参与日常桌面的焦点竞争。单纯移动窗口到屏幕外仍不能解决焦点问题。

新的启动器创建临时 Windows desktop，通过真正的 Win32 `STARTUPINFOW.lpDesktop` 启动测试子进程；子进程退出后关闭桌面句柄。测试桌面不会被切换到前台，鼠标不需要移动，Tk 仍可建立真实窗口并测试焦点。Windows 的桌面创建和进程绑定行为分别见 [CreateDesktopW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createdesktopw) 和 [STARTUPINFOW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow)。隔离失败会报告错误，不会退回用户桌面执行。

常规测试保留所有真实 Tk 断言，新增回归检查实际 desktop 名称、强制聚焦后的归属、中文输出和失败退出码。交互耗时脚本也自动进入独立桌面。真实游戏手感、实际交付与 DWM 合成表现仍需现场核对，隐藏桌面的检查不替代这些验证。

## 本次参数发现

| 项目 | 现有能力与后续处理 |
| --- | --- |
| 手动意图忽略和渐变区间 | 本次开放两个边界；保持默认 15%～30%、逐轴计算、平滑权重和零死区实体透传 |
| 自动开火节奏 | 本次开放周期和按住时长；沿用现有脉冲调度器及手动接管规则 |
| 持续跟随的横纵力度 | 已共同进入“范围与跟随”的跟随响应组，支持热加载并保留学习；配置校验和 GUI 范围由同一目录持有 |
| L2 预搜索与开镜辅助位置 | “范围与跟随”已开放两项并支持热加载，默认保持 5% / 80%；前者唤醒搜索，后者允许实际辅助。实体按下时刻仍归输入与作用域状态机持有，禁火窗口不从开镜就绪重新计时 |
| ADS 大小与外形范围 | 已实现配置预览、目标拖动、基础半径和普通 / 宽矮瞄点比例；基础半径跨视觉与控制器，需要重启，两种瞄点比例支持视觉热加载 |
| 两阶段范围、输出与响应 | 半径控制目标范围；`output_limit_x/y` 是 0～1 上限；`response_time_ms` 或 `response_time_x/y_ms` 独立控制规划时间。上限和时间可热加载，半径仍需重启。旧增益/距离在配置边界换算，不进入新 GUI |
| 过期视觉结果和手动退出规则 | 属于安全与所有权边界，继续保留原有保护；不作为普通力度旋钮开放 |

新意图区间由两组独立随机种子的 120 个短、长场景检查，每轴边界、过渡权重、热加载、作用域和原始输出都被验证。默认配置继续运行原有目标、交接和鼠标回归；模拟结果不代表实际游戏验收。


## 可编辑数值一致性检查（2026-10-06）

当前 GUI 的 37 个数值字段逐一检查最小值、默认值、最大值，111 次通过真实原生 `--dump-effective-config` 读回；另检查界面拒绝越界输入。涉及关系约束时给出有效搭配，而不是绕过校验。浮点存储按 float32 比较，界面百分比仅做 ×100 的单位显示转换。

| 配置键 | 界面允许范围 | 说明 |
| --- | --- | --- |
| `runtime.vision.target_height_ratio` | 1～99% | 目标点高度比例 |
| `runtime.vision.target_wide_low_height_ratio` | 1～99% | 宽矮目标瞄点比例 |
| `gamepad.ai_aim.body_free_initial_scale` | 0～4000 | 跟随普通区响应初值（0=默认）；0 继承，否则 80～4000 |
| `gamepad.ai_aim.body_slow_initial_scale` | 0～4000 | 跟随减速区响应初值（0=默认）；0 继承，否则 80～4000 |
| `gamepad.ai_aim.ads_free_initial_scale` | 0～4000 | ADS 普通区响应初值（0=默认）；0 继承，否则 80～4000 |
| `gamepad.ai_aim.ads_slow_initial_scale` | 0～4000 | ADS 减速区响应初值（0=默认）；0 继承，否则 80～4000 |
| `gamepad.recoil.hipfire_multiplier` | 0～100% | 腰射压枪倍率 |
| `gamepad.output_transfer.deadzone` | 0～50% | 游戏死区补偿量 |
| `gamepad.output_transfer.game_exponent` | 1～3 | 游戏响应幂指数 |
| `gamepad.ads.activation_trigger` | 0～95% | L2 预搜索阈值；必须低于开镜辅助阈值 |
| `gamepad.ads.scope_ready_trigger` | 0.1～100% | L2 开镜辅助阈值 |
| `gamepad.ads.pickup_base_radius_px` | 0.001～2000px | 基础抓取半径 |
| `gamepad.bodylock.activation_range_px` | 1～2000px | 基础跟随半径；不得低于 ADS 完成距离 |
| `gamepad.ai_aim.manual_intent_begin` | 0～99% | 忽略手动意图阈值；必须低于完整意图阈值 |
| `gamepad.ai_aim.manual_intent_full` | 1～100% | 完整手动意图阈值 |
| `gamepad.auto_fire.ads_press_delay_ms` | 0～5000 | 开镜后禁火（ms） |
| `gamepad.auto_fire.pulse_period_ms` | 1～5000 | 自动连发周期（ms） |
| `gamepad.auto_fire.pulse_width_ms` | 1～5000 | 单次按住时长（ms）；不得超过周期 |
| `gamepad.ads.output_limit_x` | 0～100% | 横向输出上限 |
| `gamepad.ads.output_limit_y` | 0～100% | 纵向输出上限 |
| `gamepad.bodylock.output_limit_x` | 0～100% | 横向输出上限 |
| `gamepad.bodylock.output_limit_y` | 0～100% | 纵向输出上限 |
| `gamepad.ads.response_time_ms` | 60～350ms | 响应时间 |
| `gamepad.bodylock.response_time_x_ms` | 5～1000ms | 横向响应时间 |
| `gamepad.bodylock.response_time_y_ms` | 5～1000ms | 纵向响应时间 |
| `gamepad.assist.hipfire_ratio` | 0～100% | 腰射辅助保留比例 |
| `gamepad.recoil.output_amount` | 0～100% | 开镜压枪输出 |
| `gamepad.assist.input_deadzone` | 0～100% | AI 输入死区 |
| `runtime.vision.capture_fps` | 1～1000 | 检测帧率 |
| `runtime.vision.idle_capture_fps` | 1～240 | 空闲检测帧率 |
| `runtime.vision.capture_width` | 32～8192 | 捕获宽度 |
| `runtime.vision.capture_height` | 32～8192 | 捕获高度 |
| `runtime.vision.tensor_width` | 32～8192 | 模型输入宽度 |
| `runtime.vision.tensor_height` | 32～8192 | 模型输入高度 |
| `runtime.input.controller_index` | 0～3 | XInput 手柄编号 |


本轮确证并处理的差异：四项尺寸 GUI 原允许 1 而原生最低 32；普通/宽矮瞄点比例的原生范围比界面更宽，现统一为 1%～99%；ADS 实际规划时间受目标大小/方向偷偷缩短；跟随半径被完成距离抬高；旧压枪量被 14%～34% 隐藏范围截断；原生手柄编号缺少 0～3 校验；旧 tracker 瞄点比例/观测年龄解析时静默截断。现在通过范围/关系校验或删除隐式改值解决。

旧 ADS 增益、BodyLock 力度/距离和旧腰射倍率已不属于可编辑 GUI 参数，仍保留明确的历史换算以打开已有配置；它们与新字段并存时，新字段优先。旧压枪量改为直接别名，历史最低/最高值不再生效。不会因打开配置而重写个人文件。

“配多少就是多少”指参数值不被偷偷改写。输出上限不是每帧固定输出，范围是基础值、目标大小会按公开规则放宽，响应初值在启用学习后会被实际测量更新，周期按控制器采样时钟调度，帧率是调度目标且受硬件吞吐限制。手动权限、目标准入、输出联合限幅和手动补偿的显式开关仍按各自定义工作。

检查入口：`python -m desktop_app.test_desktop`；新增逐项检查在 `IndependentStorageTests.test_every_editable_numeric_field_roundtrips_native_without_replacement`。原生 `BaseContracts` 覆盖新增死区与越界拒绝，`BaseBodyLock` 覆盖真实合成链、热更新、逐轴死区边界、近目标输出和带延迟的随机模拟。几何预览协议使用 `range-response-v3`，旧原生程序不会被当成新版演示的计算依据。


检查期间曾两次出现成功退出但配置输出不完整；后续 150 次独立读取与单独全量 GUI 检查未复现，截断根因尚未确认。新增原生输出结束标记与 GUI 完整性校验，在跨进程读取边界拒绝不完整结果，防止缺失值被当成默认值。此项是独立的边界加固，不声称解决了尚未定位的截断原因；没有重试或补默认值来掩盖错误。


最终验证：Release 构建成功；50 组 CTest 通过；109 项 Python/隔离 Tk 检查通过。瞄点范围最后统一后，再次通过全部 CTest 和 105 组逐项数值读回、截断拒绝专项检查。新版原生与预览对照扩展到 72 组，包含仅关闭单轴的边界。日志为 `output/parameter-audit-build.log`、`output/parameter-audit-ctest.log`、`output/parameter-audit-gui-tests.log` 和 `output/parameter-audit-boundaries-final.log`。


2026-10-06 距离与瞄点修订（中间版本，最终收尾行为以下节为准）：新增 `gamepad.assist.minimum_position_stick`（0～1，默认 0.20）与 `gamepad.assist.arrival_radius_px`（1～64 px，默认 2）。低速参考位于曲线之后、输出限幅以内；瞄点距离统一 ADS 完成、输入状态机和求解器。进入目标框不会完成位置任务。旧完成距离显式迁移，规范字段优先。原生与 GUI 的静态示例扩展为 864 组（不同范围、时间、限幅、最低力度和方向），协议为 `range-response-v3`。本轮构建和检查日志使用 `output/range-response-*.log`；前述 parameter-audit 日志属于上一轮检查。

距离修订的数值结果：160 组延迟模型中，10% 低速参考有 4 组在固定 2 秒内未进入 2 px；15% 与 20% 组均在各自固定窗口内进入，所有 12 秒组均进入。最大过冲约 1.283 px。保留这些未到达记录，不把成功执行当成到点保证。生产链固定时隙跟随检查在默认 20% 下通过；15% 的非线性响应仍有进入跟随超时，因而选用用户建议范围内的 20% 默认值。

本轮最终检查：Release 构建成功，50 组 CTest 全部通过，111 项 Python/隔离 Tk 检查通过，`git diff --check` 通过。最小 840×680 窗口的范围/跟随、32 px 近目标示例和到点停止画面已截图检查。


2026-10-06 实录断续修正：会话 `20261006T025439Z_26916_1` 的去重开镜采样中，125 个零请求有 115 个处于 inactive，仅 4 个是有效目标进入 2 px 范围。这是事件采样数量，不代表时间占比。用户确认手柄规范化控制采用最多 32 ms 的同身份漏检跟随：期限从最后一次真实观测算起，且不超过配置的观测年龄；预测位移使用已投递、经过响应曲线的镜头历史及已通过现有规则确认的目标运动。预测不修改原始观测几何、不参与学习、不授予自动开火；友方/不可靠拒绝、身份改变、手动交接、缺失投递历史、超时停止。原生状态名为 `observation_gap`。鼠标适配器维持原策略。

新增检查包含真实合成链与 160 个随机短/长序列（种子 10632/938117，每序列 128 或 2000 次更新），覆盖 32 ms / 更短观测期限、反复漏检、无观测更新和明确失效。日志及事件统计在 `output/incident-20261006-105443/`。该修改处理短暂漏检导致的输出中断，不声称修复检测模型本身；32 ms 以上的漏检仍停止。COD 持续开镜约 60 FPS 的原因尚未确认：对应旧会话性能与遥测均关闭，等待开启性能统计后复现，不把空闲 60 FPS 配置当作已证实原因。


COD 复现与修复补充：11:06 会话 `20261006T030616Z_30968_1` 确认持续开镜也只有约 65.8～69 FPS。取帧间隔中位 15.523 ms，其中取帧调用中位 0.069 ms、两帧处理之间空档中位 12.833 ms；推理均值约 1.1～1.3 ms。视觉线程的条件变量定时等待替换为 Windows 高精度 waitable timer + 自动复位唤醒事件，开镜、配置切换和停止可立即中断等待，提前到达的通知不会丢失。未增加忙等、重试或改写用户帧率。

11:12 同配置实机复测 `20261006-111218-63c0e4`：多个完整连续开镜窗口达到约 166～167 FPS（配置目标仍为 180），每次采集累计画面从约 2.6～2.7 降至约 1.08。对照在 `output/incident-20261006-105443/cod-before-after.csv`。这验证消除了约 60 FPS 的异常等待限制，不宣称达到硬件无关的精确 180 FPS。空闲 60 FPS 配置保留。最终 Release 构建及 50 组 CTest 通过；新增定时等待检查覆盖提前通知、等待中唤醒和到期，不以微基准分数代替实机结果。32 ms 跟随仍需 Apex 实机手感复测。


## 2026-10-06 最终收尾与连续跟随

低速参考是追赶阶段的参考量，不再覆盖近点刹车。求解器保留按实际抓取范围缩放的预算，在实际瞄点附近叠加连续收尾与名义 25 ms 刹车预算；可信移动补偿独立保留。`gamepad.assist.arrival_radius_px` 的界面名称改为“近点收尾半径”，它作为连续收尾尺度，同时仍用于 ADS 完成判断。进入半径不会让 BodyLock 突然把位置任务归零。输入死区仍按配置逐轴过滤，未引入隐藏最小输出。

速度估计用最多五个有效视觉观测间隔、最长 25 ms 的镜头补偿后位移累计，替代三次瞬时速度中值。没有新增光流、固定 10 帧预测后 2 帧或归零延时。原生与 Python 静态线性预览通过 v3 协议的 864 组样例对照。

原生完整构建、50 组 CTest 和 111 项桌面检查通过。最终模拟是 192 场景 / 134.4 万步，96 组配置最低力度对比中 90 组平均误差下降；六组静止场景残余误差增加但在各自到点范围内。模拟不含完整游戏、选择器、手动仲裁及后坐力，实机 Apex 手感仍需验证。最终产物及历史中间资料见 [输出索引](../../output/README.md)。
