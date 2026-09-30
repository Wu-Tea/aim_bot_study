# BO3 大死区适配

实现日期：2026-09-29。已编译并通过自动化检查；游戏实际死区、响应曲线和手感尚未实测验收。

## 交付后修复：独立程序读不到实体手柄

首次交付遗漏了 HidHide 对可执行文件路径的访问控制。本机启用了隐藏，DualSense Edge 被隐藏；
旧的 `native/vision_native/build/Release/cod_native_runtime.exe` 在应用放行列表中，BO3 新路径不在。
配置解析成功和进程存在都不能证明实体输入已接通。

最终按用户要求统一使用原来的 `native/vision_native/build/Release/cod_native_runtime.exe`。
BO3、Apex 和默认入口只切换配置，不再使用独立 BO3 程序，也不在启动时自动采样或注册路径。
`--probe-input` 仅保留为人工排障命令，不创建虚拟输出、不运行视觉。
原程序已更新为包含 BO3 转换逻辑的版本，原二进制备份于
`runs/bo3-input-fix-20260929/shared-runtime-before.exe`。

修复前新路径采样为 0/200；确认访问权限因果后切回原路径。
最终实际启动日志确认原路径程序选中 DualSense Edge、ViGEm 虚拟 DS4 上线、视觉初始化完成并进入控制循环。
这证明设备接入和初始化链路恢复，不等同于游戏死区/手感验收。

## 启动和调整

2026-09-30 起，双击根目录的 `启动助手.vbs`，选择 COD：Black Ops III，在 GUI 中调整、启动和停止。
当前设置统一为 `config.toml` 的 `[games.bo3.gamepad.output_transfer]` 等分块，原生 `--game bo3` 直接读取。
旧文件的本机补偿量 0.20 已迁移；旧 `config.bo3.toml` 不再参与启动，也不再生成完整配置。
其他参数继承共用值，所有游戏使用原路径程序。详情见 [手柄助手 GUI](DESKTOP_ASSISTANT.md)。

| 参数 | 初值 | 含义 |
|---|---:|---|
| `enabled` | `true` | 是否转换右摇杆最终输出 |
| `deadzone` | `0.16` | 假设的游戏内死区；允许 0～0.5，**不是测得的 BO3 数值** |
| `axial` | `false` | 默认径向补偿，保留方向；true 为 X/Y 各自补偿，适用于轴向死区 |
| `game_exponent` | `1.0` | 假设的游戏响应幂指数；允许 1～3，取逆后以线性摄像机响应为目标 |

先只调 `deadzone`：若小幅推动仍不动，每次增加 0.01；若刚推动就跳，每次减少 0.01。
确认水平、垂直及斜向的小幅移动；只在斜向/贴轴移动表现出死区形状不匹配时比较 axial。
`game_exponent=1` 只补死区，不声称已经消除 BO3 未测量的非线性。取得响应采样后才能校准指数；
这版没有把旧的 `cod_dynamic_legacy_lut` 当作现代 COD 动态曲线的精确复刻。

## 转换归属

`game_stick_transfer.h` 定义无状态正反转换。令 r 为右杆幅度、d 为配置死区、g 为配置游戏指数，
对 0 < r < 1 输出 `d + (1-d) * r^(1/g)`；零输入仍为零。径向模式保留方向及单位圆外的方形报告边界，
轴向模式逐轴转换。该函数不创建缓冲、等待、识别推理或线程。

`OutputComposer::finalize` 在手动/AI 仲裁、压枪相加与限幅之后转换一次。游戏内 ADS 和腰射共用映射，
不因开关镜而切换补偿。左杆、扳机及按钮保持原行为。物理输入用于意图判断的坐标不变。
默认配置 `enabled=false`，其他入口没有隐式启用补偿。

控制器送达确认先把真实 DS4 量化输出逆变换回内部坐标，再进入现有 AI 响应曲线和估计历史。
压枪诊断也使用内部坐标，避免将反死区增量误认为压枪。遥测 `final_x/y` 仍是最终发送前的摇杆值，
`pre_recoil` 和 `recoil` 是适配前的值：启用适配后不能直接用它们的和对比 `final`，需应用配置中的转换。
断连/失败确认仍使响应历史失效，未改变原生命周期。

真正的硬件漂移不会被这个功能消除。输入非零就会补偿；DS4 每轴 8 bit 量化也会留下极小的台阶或残余不响应区。
因此“0 死区”是操作目标，不是对未校准硬件/游戏的数学保证。若 Steam Input 或其他软件也启用了反死区，应只保留一处补偿。

## 验证及证据

证据目录：`runs/bo3-adaptation-20260929/`。

- 新建独立 Release 构建：`native/vision_native/build-bo3`。运行时、基础测试、功能测试构建成功。
- 已有 2026-09-28 构建与 BO3 候选的 10 组 Base/Feature CTest 均通过，包含 ADS、BodyLock、输出、压枪、AutoFire 与遥测相关既有门禁。
- 新增 262,144 组确定性随机输入：两种死区形状、4 个死区值、4 个指数、两个固定 seed、每组 4096 输入；检验单调、方向、边界、正反变换、禁用时精确直通，以及 DS4 量化误差上限。
- 21,200 tick 短/长序列：两组 seed，快速切换 ADS/腰射、开火、断连、回中；检查最终阶段一次转换及其他控件保持一致。
- 单独的真实 controller facade 验证回中同 tick 清零、腰射/ADS 压枪补偿。合成 500 px/s 响应模型的估计对照验证反变换接入，要求实际发生输出和学习，避免空通过。
- Windows PowerShell 启动器及既有入口测试：38 passed。包括 UTF-8 中文注释、配置隔离、重复实例阻断、真实启动脚本到进程创建边界；测试没有创建虚拟手柄。
- 新程序 `--dump-effective-config` 确认模型继承 `models/best_480x384.engine`、640×512 捕获及四个适配参数生效。
- 算术微基准 `scripts/verify/benchmark_game_stick_transfer.cpp`：262,144 输入/轮，预热后 9 轮。
  本机中位数 disabled 2.33 ns、默认径向 encode 11.90 ns、decode 13.51 ns、指数 2 的 encode 27.41 ns。
  这些数值只描述函数算术成本，不能当作整个运行时或游戏延迟的 A/B 结论。

此构建包含工作区原有的 9 月 28 日状态机重构，未整理或提交其他未提交改动，最终交付已更新原路径运行程序；未运行的旧版本留有备份。
未启动 BO3 实机或进行玩家手感验收；线性幂函数只是明确的假设模型。后续实际采样应同时覆盖水平/垂直/斜向、ADS/腰射、缓推/回中/快速反向。

参考背景：Steamworks 对 anti-deadzone 的定义见
[Input Source Modes](https://partner.steamgames.com/doc/features/steam_controller/input_source_modes?l=english)；
现代 COD 的曲线名称说明见
[Activision Modern Warfare controls](https://blog.activision.com/call-of-duty/2019-10/Getting-Started-in-Modern-Warfare-Controls-and-Settings)。
两者均不提供本配置所需的 BO3 实测传递函数。
