> 当前 dev 合并约定：runtime、Vision 和构建走 C++；GUI 沿用 dev 的 Python/Tk 配置工作室。新 C++ GUI 未合入。本文原生助手相关旧检查不作为当前要求，界面操作见 [桌面助手](DESKTOP_ASSISTANT.md)。

# 手柄能力概览

当前手柄链路由 `cod_native_runtime.exe` 单进程执行；Python GamepadController、fallback、KBM 桥接和旧 recoil_app 已退役。启动入口为根目录桌面助手或 `scripts/launch/gamepad_native_cpp_start.bat`。

runtime 从原生配置 loader 取得有效游戏配置，创建 Vision、物理 SDL/XInput 输入和按需 ViGEm 输出。正常 tick 由 RuntimeLoop 串联最新视觉交付、物理输入、NativeGamepadController 和输出；配置读取及文件工作在 Bridge 后台准备，控制链仍保持同 tick 顺序。

TargetCoordinator 拥有目标身份、生命周期和模式；ADS acquisition 与 BodyLock follow 负责互斥的目标解算；AimDynamicsShaper 进行一次 proposal 整形；AssistControlStateMachine 拥有最终手动/AI 权限及 pre-recoil 命令。AutoFire 和 recoil 保有各自的安全及 profile 责任。输出接收端的实际交付回执决定学习配对，诊断不能自行推定已交付。

原始手动 passthrough 保持零死区。AI intent 权重在 15%–30% 连续变化；目标搜索、取得、身份交接、强手动权限及资源释放保持现有契约。新鲜空目标释放通用权限；没有新帧不能制造新鲜检测证据。退役 ai_proposal knobs 继续 unknown/inert。

配置提交分为纯控制项、视觉 policy 和要求重启的结构项；前两者在各自现有边界生效。停止时释放目标/按键权限、发送必要 neutral、停止工作线程并排空诊断。输出关闭时不创建 ViGEm 设备。

入口、模块源码和生命周期关系见 [当前项目模型](CURRENT_STATE.md)，原生细节见 [Native C++ Runtime](NATIVE_CPP_RUNTIME.md)，配置操作见 [原生助手](DESKTOP_ASSISTANT.md)。基础/功能契约与数值仿真保留，旧 SHA256 和 benchmark 验收裁决退役；实际游戏行为仍须真实设备和现场验证。
