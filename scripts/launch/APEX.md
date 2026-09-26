# Apex 手柄入口

- 双击 `apex_native_background_start.vbs`：后台启动。
- 双击 `apex_native_background_stop.vbs`：停止该入口记录的 Apex 进程。

每次启动从仓库根目录的 `config.toml` 生成
`runs/runtime/background/apex/config.apex.toml`，仅覆盖两项：

```toml
[runtime.vision]
model_path = "artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine"

[gamepad.aim_response_curve]
algorithm = "linear"
```

其他参数继承当前配置。修改参数后需要停止再启动；不要直接编辑生成文件。
这里的线性是工具的响应曲线模型，不会修改 Apex 游戏内设置；游戏内响应曲线也应由玩家设为线性。

模型使用此前训练并导出的 Apex specialist，480×384、单类 person。
模型留在本机实验目录，不随 Git 分发；缺失时启动失败，不回退到其他模型。
这次接入不代表重新完成 Apex 实战效果验收。

切换游戏前先用原游戏入口停止运行，再启动另一套入口，避免两个进程同时输出。
Apex 的进程记录和启动日志单独放在 `runs/runtime/background/apex/`；
普通入口的配置和进程记录保持原路径。

仅检查启动参数、不运行游戏控制器：

```powershell
powershell -NoProfile -File scripts/launch/gamepad_native_background_start.ps1 -Game apex -PrintOnly
powershell -NoProfile -File scripts/launch/gamepad_native_background_stop.ps1 -Game apex -PrintOnly
```
