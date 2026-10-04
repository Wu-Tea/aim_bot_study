# 原生配置助手

当前助手为 `native/build/Release/cod_native_assistant.exe`。根目录 `启动助手.vbs` 只负责隐藏控制台并打开窗口；不再启动脚本运行时。

## 日常操作

1. 从示例准备 `config.toml`，并准备配置引用的 TensorRT engine。
2. 打开助手，选择游戏，编辑 TOML。校验使用同一份原生配置 loader，支持项目现行配置子集，不是通用 TOML 编辑器。
3. 校验、保存，然后启动。编辑器保留注释与未修改字段；保存检查外部修改冲突，并以原子替换写文件。
4. 运行中保存后点击热更新。pending 等待 tick/新视觉 policy，applied 才是生效；需重启的项会明确提示。学习状态显示四个区域的 effective 响应和样本数。
5. 点击停止，等待原生资源释放。关闭窗口保留后台程序；查看日志使用窗口“日志”按钮。

配置按原生规则解释：基础配置、运行 profile、游戏覆盖、环境与 CLI 有各自优先级。图形曲线编辑、预设库和旧采集/训练面板已退役；自定义 LUT 和曲线参数仍可直接编辑原生配置。

## 实现与边界

[desktop_main.cpp](../../native/desktop_native/desktop_main.cpp) 只拥有窗口、编辑草稿和异步操作；[DesktopSession](../../native/desktop_native/desktop_session.cpp) 拥有文件/进程/控制通道适配。控制算法仍在原生 runtime。

启动用独占记录文件串行化，子进程先 suspended，记录 PID/创建时间/可执行路径约定后才恢复执行；初始化等待真实控制通道，早退会报告日志位置。停止只操作匹配身份的程序，使用命名事件并等待退出，不按进程名终止。GUI/后台脚本共用 `runs/desktop/native-runtime.bin`；日志追加到 `runs/desktop/native-runtime.log`。

C++ IPC client 使用 [共享协议](../../native/runtime_app/runtime_control_protocol.h)，仍是 protocol=1、snapshot=640 bytes。请求成功不等于 applied，窗口定期读取真实状态。文件保存失败、冲突或配置无效不会覆盖原配置。

## 命令行与验证

```powershell
native/build/Release/cod_native_assistant.exe --action check-config
native/build/Release/cod_native_assistant.exe --action preview-start --game default
native/build/Release/cod_native_assistant.exe --action start --game default
native/build/Release/cod_native_assistant.exe --action status
native/build/Release/cod_native_assistant.exe --action reload
native/build/Release/cod_native_assistant.exe --action stop
```

`--project` 可指定项目根；`runtime-info` 输出已核对身份的实例 metadata，供 Fusion 启动脚本使用。`ui-check` 在隐藏窗口中检查控件创建/配置载入，不启动 runtime 或设备。

基础测试覆盖无效保存、外部修改冲突、Unicode、安全 game 参数和空实例；功能测试覆盖隐藏窗口创建。实际 C++ client→TensorRT runtime→控制/视觉策略热更→停止已检查，设备输出在检查中关闭。未进行完整人工 GUI 易用性验收或现场游戏测试。