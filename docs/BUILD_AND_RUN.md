# 构建与运行

本分支构建两个原生可执行文件：手柄 `cod_native_runtime.exe` 和可选显示 `fusion_canvas.exe`；桌面配置界面由 Python 宿主加载 React 静态资源。

## 本机依赖

- Windows x64、MSVC 2022、Windows SDK 和 CMake；原生构建脚本默认使用 Visual Studio 2022 Professional。
- CUDA Toolkit 与 TensorRT SDK。脚本提供本机默认路径，也可通过 `-CudaPath`、`-TensorRTRoot` 调整。
- SDL2 / ViGEmClient DLL 放在 `runtime_deps/windows-x64/`，启用手柄输出还需要 ViGEmBus 驱动。DLL 不进入 Git，说明见 [设备库](../runtime_deps/README.md)。
- Python 3.11+、`python/desktop_app/requirements.txt` 中的 pywebview，以及 Windows WebView2 Runtime。桌面资源构建需要 Node.js/npm。
- 本机 TensorRT engine，参见 [模型目录](../models/README.md)。引擎应匹配当前 TensorRT/GPU；模型选择时 GUI 读取真实输入尺寸。

## 构建

在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/setup_desktop.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_native_runtime.ps1
```

第一条安装桌面依赖并生成 `python/desktop_app/web_ui/dist/`。第二条生成 `native/build/Release/`，只构建 runtime 与 Fusion；原生构建和控制链不依赖 Python。测试和 benchmark 不进入 clean 的构建图。

`启动助手.vbs` 当前使用本机 `D:\env\python\pythonw.exe`。其他机器可调整该路径，或在安装好依赖后使用：

```powershell
$env:PYTHONPATH = "$PWD/python"
python -m desktop_app.gui --root .
```

GUI 的配置库位于 `profiles/library/`；配置、曲线、模型和 `runs/` 均为本地数据。迁移时不要复制旧运行 PID 状态作为新实例。GUI 可以保留未暴露的原生配置字段；读取旧配置不会自动覆盖个人数据。

## 启动检查与日志

```powershell
native/build/Release/cod_native_runtime.exe --config config.native.example.toml --dump-effective-config
```

该命令校验并导出实际生效配置，在模型与输出设备初始化前退出。真实启动仍须在 GUI 中选择有效模型和输入设备。日志位于 `runs/desktop/sessions/` 和原生运行日志目录；`runs/desktop/ui.json` 是界面选择偏好，runtime 状态由 PID、创建时间和路径校验。

性能采集是保留的 GUI 功能，默认关闭；它调用 `tools/long_capture/`，使用本机 `nvidia-smi` 和脚本指定的 PresentMon，记录写到本地 `output/`。已有实验结果不随分支迁移。Fusion 开关使用 `scripts/launch/gamepad_fusion_background_start.ps1`，其捕获排除检查失败时不会强行显示。

行为验证从 DEV 取夹具，并链接当前 clean 构建的生产库；验证数据留在 DEV 或临时目录。实际 GUI 启停与热更新检查应使用临时配置、关闭输出，并验证 `applied` 回执。游戏中的识别效果、手感和设备实际交付仍需实机确认。

## 敌我标识配色

桌面“目标与识别”提供敌人/友军标识颜色HEX输入，默认敌人 `#FF00FF`、友军 `#00FFFF`，须与游戏设置一致。接受六位HEX（可带#），更改后重启助手生效；格式错误、过暗、无彩色或敌我色相范围重叠会在启动前报错。

Profile字段为 `[runtime.vision]` 下的 `enemy_marker_color`、`friendly_marker_color`。旧Profile缺少字段时使用上述新默认值，不会自动重写个人配置。识别允许色相、亮度与压缩变化，不要求像素精确等于HEX。独立cue、人物颜色分类和续跟共用规则，配色在加载时编译，不增加整图扫描或降低检测频率。

同组“自动标记敌人”映射 `gamepad.enemy_mark.enabled`；修改后同样需重启。关闭后不再合成自动标记按键，游戏已有标记与手动标记仍可能干扰识别。颜色切换能减少部分背景干扰，但不能保证所有地图无误判；近身人体框几何突变仍是独立限制。
