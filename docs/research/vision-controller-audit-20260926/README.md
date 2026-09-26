# 审查证据与复现

主报告：[从画面到 DS4：系统审查](D:/work/AI/yolo-study-001/docs/research/VISION_CONTROLLER_GAMEPAD_SYSTEM_AUDIT_20260926.md)。

- `INVENTORY.md`：82项语义机制及旁路，按真实数据路径排列。
- `effective-config.txt`：已有运行程序加载本次当前配置的只读输出；不是历史日志配置，且dump未覆盖所有默认参数。
- `probe.cpp` / `probe-result.json`：调用现有C++组件的确定性局部探针。没有硬件I/O，不创建虚拟手柄，不替换生产可执行文件。
- `manifest.json`：源文件与产物SHA-256、HEAD、现有运行程序标识及证据限制。

在 VS 2022 x64 Native Tools Command Prompt 中，以仓库根目录为工作目录，先建立 `runs\vision_controller_audit_20260926`，执行：

```bat
cl /nologo /std:c++17 /EHsc /O2 /MD /I native docs\research\vision-controller-audit-20260926\probe.cpp native\controller_native\assist_control_state_machine.cpp native\controller_native\aim_dynamics_shaper.cpp native\controller_native\ads_acquisition_controller.cpp native\controller_native\bodylock_follow_controller.cpp native\controller_native\bodylock_target_motion_observer.cpp native\controller_native\response_model_aim_solver.cpp /Fo:runs\vision_controller_audit_20260926\ /Fe:runs\vision_controller_audit_20260926\probe.exe
runs\vision_controller_audit_20260926\probe.exe
```

probe exit 0表示其行为断言成立，不表示系统没有过冲。它在刻画现有算法，未实现修复，也不是RED→GREEN验收。它直接构造arbiter输入，因此fresh分支探针不能代替完整controller的可达性证明；observer输入的真实R是明确的合成假设。ADS比例探针使用linear映射来隔离到达律，生产仍使用配置的LUT。

本次只读导出配置的命令如下。入口源码确认该模式在创建RuntimeLoop/ViGEm之前返回：

```powershell
& native/vision_native/build/Release/cod_native_runtime.exe --config config.toml --dump-effective-config
```

后续可以将主报告链接加入 `.agent-context/`，让后续会话从这份调用链继续；本次未改写该目录。
