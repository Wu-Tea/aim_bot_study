# 鼠标转写：重启后检查

> 已完成后续真实桌面验收，当前结论和使用入口见 [桌面验收结果](MOUSE_VIRTUAL_RELAY_DESKTOP_VERIFIED_20260908.md)。下文保留当时的阶段事实，不再表示尚待首次测试。

2026-09-08，用户已回复“重启了”。沿用原授权，不使用 subagent。

## 当前事实

- `sc query mouse`、`sc query keyboard` 均为 `RUNNING`，退出码 0。
- Interception 可以枚举真实硬件 ID：当前 Razer 主输入接口编号 **13**，`HID\VID_1532&PID_00B8&REV_0100&MI_00`；编号 14 是同鼠标的另一集合，不能混选。
- FakerInput 控制端已连接，相对鼠标重新出现在 Raw Input 中，句柄 65634；总鼠标数恢复为 7。原先安装后未初始化的问题在本次重启后未再出现。
- 新 C++ `FakerOutput` 后端已经真实验证：64 个微小往返位移，两个独立接收进程均收到完整、顺序正确的 64 个相对运动包，无点击、无滚轮、无丢弃和读错误。
- 尚未开始实体鼠标接管验收。已向用户发出约 20 秒三阶段测试的准备选项，等待用户选择“开始测试”；不能把该选项默认选中或等待超时当成用户已准备好。

证据：

- `runs/mouse_virtual_relay/postreboot-preflight-20260908/inventory.txt`
- `runs/mouse_virtual_relay/postreboot-preflight-20260908/services.json`
- `runs/mouse_virtual_relay/native-output-postreboot-20260908/summary.json`
- 两个独立接收器的完整 CSV 位于上述输出测试目录的 `receiver_0`、`receiver_1`。

## 本轮新增

- `native/mouse_link/virtual_mouse_output_probe.cpp` 和 `scripts/verify/verify_native_virtual_output.py`：验证实际被转写程序使用的 C++ 输出实现，不以先前上游 DLL 的成功代替新后端验证。
- `--acceptance` 显示一个专用测试窗口，三阶段分别提示原样、零位移和反向；接收侧键时不执行应用返回等操作。窗口提示用户在中央小幅画圈，并逐个测试五键和滚轮。18 秒后自动恢复，Ctrl+Alt+F12 或关闭测试窗口提前恢复。
- 分析器新增各阶段源/输出运动量与 `three_mode_motion_verified`，三个阶段都必须有足够实体源活动且原有独立接收校验通过，才报告三阶段运动验证成功。

当前编译通过，6 项 CTest 通过；分析器和父进程退出的 7 项 Python 测试通过。测试不会替代实体接管、物理信号泄漏、实际按钮、退出恢复的人工输入验收。

## 下一步

用户准备好后，在正常桌面执行：

```powershell
artifacts/mouse_link/virtual-build/Release/mouse_virtual_relay.exe --source 13 --acceptance --out runs/mouse_virtual_relay/physical-acceptance-postreboot-20260908
```

输出目录必须不存在。测试需要用户实际画圈并操作鼠标；当前桌面输入应使用正常桌面权限执行，沙箱中的监听器可能收不到事件。随后运行：

```powershell
D:/env/python/python.exe scripts/verify/analyze_mouse_virtual_relay.py runs/mouse_virtual_relay/physical-acceptance-postreboot-20260908
```

若有失败，先检查源包、独立接收序列和会话错误定位原因。不能将有源运动的零输出与没有源输入混为一谈。完成采集后再确认真实物理输入恢复；尚未验证目标游戏、1000 Hz、完整延迟或接入 AI runtime。

本轮没有重复安装驱动，也没有更改 `.agent-context/`。建议完成实体验收后将最终事实同步至项目上下文；不要把旧的“等待重启”当成当前状态。
