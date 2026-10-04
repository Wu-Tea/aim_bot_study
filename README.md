# yolo-study-001

Windows 上的 C++ 原生视觉辅助控制项目。手柄、鼠标、Vision、Fusion 和离线仿真由原生代码执行。按用户最新合并要求，桌面 GUI 沿用 dev 的 Python/Tk 配置工作室；Python 仅保留 GUI 及其配置、曲线、进程管理、IPC 和界面检查，不恢复 Python 控制器、视觉 fallback、训练或分析工具。

## 启动与配置

双击根目录 `启动助手.vbs` 打开现有配置工作室。创建或导入独立配置，选择 TensorRT engine，调整参数与响应曲线后保存并启动。新配置默认不指定模型，需要主动选择可用 engine。切换编辑配置不等于切换实际运行实例。

GUI 使用现有 `D:\env\python\pythonw.exe`，只依赖 Python 3.11+ 标准库和 Tk，无需安装旧 requirements 中的视觉/控制器包。构建好的 runtime 位于 `native/build/Release/cod_native_runtime.exe`。关闭 GUI 保留运行中的 runtime。

完整操作见 [桌面助手](docs/project/DESKTOP_ASSISTANT.md)。个人 config.toml、profiles、模型、数据及运行记录保留。

## 原生构建与验证

依赖 Windows、Visual Studio 2022 C++、CUDA、TensorRT 和原生设备 DLL，准备方式见 [runtime_deps](runtime_deps/README.md)。原生构建不依赖 Python 或 pybind11。

```powershell
.\tools\build_native_runtime.ps1 -CudaArchitectures 75 -OfflineBenchmarks
ctest --test-dir native/build -C Release --output-on-failure
$env:PYTHONPATH = "$PWD\python"
D:\env\python\python.exe -m unittest discover -s python/tests -p 'test_desktop*.py'
```

CUDA 架构应匹配 GPU；SDK 和 DLL 路径可由脚本参数指定。OfflineBenchmarks 是可选数值仿真，无 SHA256 清单、比较门禁或发布裁决。

已有 ONNX 可使用 SDK 原生工具构建新 engine：

```powershell
.\tools\build_engine.ps1 -OnnxPath models/model.onnx -EnginePath models/candidate.engine
.\tools\run_native_vision_smoke.ps1 -EnginePath models/candidate.engine
```

动态模型可通过 InputShape 指定实际输入名称/形状。旧训练、采集研究和分析脚本已退役，资产没有随工具删除。

## 代码责任

| 位置 | 责任 |
| --- | --- |
| [native](native/README.md) | 统一 CMake、runtime、控制器、Vision、鼠标及 Fusion |
| python/desktop_app、python/project_paths.py | 保留的 dev GUI、独立配置工作室、曲线、进程和 IPC |
| python/tests/test_desktop*.py | 保留 GUI 功能与原生配置衔接检查 |
| scripts/launch | 手柄、鼠标、Fusion 的启停入口 |
| tools | 原生构建、engine 构建和 Vision 检查 |
| runtime_deps | 本机原生设备 DLL 输入 |

职责、生命周期和验证边界见 [当前项目模型](docs/project/CURRENT_STATE.md)。原始手动零死区、15%–30% intent、身份/交接及同 tick 控制链保持；仿真通过不等于现场游戏验收。
