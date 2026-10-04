# yolo-study-001

Windows 上的 C++ 原生视觉辅助控制项目。当前手柄、鼠标、Vision、Fusion、配置助手和离线仿真均走原生路径。项目不再提供 Python 运行、构建、桥接、训练或测试入口。

## 启动与配置

构建后双击根目录的 `启动助手.vbs`，打开原生助手。选择 `default`、`apex` 或 `bo3`，编辑 TOML 后校验、保存；运行中点击热更新，等待“已生效”或“需要重启”。启动、停止和学习状态也在同一窗口。关闭助手保留后台 runtime，再次打开仍可操作已登记实例。

本机配置是被 Git 忽略的 `config.toml`，可从 `config.native.example.toml` 复制。游戏覆盖放在 `[games.apex.*]`、`[games.bo3.*]`。模型、输出启用状态及部分基础设施参数需要重启；控制与部分视觉策略按现有 tick/policy 边界热更。保存会校验并检查外部修改冲突；保存成功本身不代表运行参数已生效。

助手现在使用 TOML 文本编辑，旧曲线绘图/预设管理界面已退役；曲线和响应参数仍由原生配置支持。完整使用方法见 [原生助手](docs/project/DESKTOP_ASSISTANT.md)。

## 构建与检查

依赖 Windows、Visual Studio 2022 C++、CUDA 和 TensorRT SDK。原生设备 DLL 的准备见 [runtime_deps](runtime_deps/README.md)。

```powershell
.\tools\build_native_runtime.ps1 -CudaArchitectures 75
ctest --test-dir native/build -C Release --output-on-failure
```

CUDA 架构应匹配实际 GPU；75 是本 worktree 使用的值。SDK 路径、DLL 路径和构建目录可通过脚本参数指定。离线仿真测试显式加 `-OfflineBenchmarks`。无 Python/pybind11 依赖。

模型使用已有 TensorRT engine。当前本机默认为 `models/best_480x384.engine`，捕获 `640x512`，实际配置与缩放约束由原生 loader 校验。已有 ONNX 可由 SDK 原生工具构建新 engine：

```powershell
.\tools\build_engine.ps1 -OnnxPath models/model.onnx -EnginePath models/candidate.engine
.\tools\run_native_vision_smoke.ps1 -EnginePath models/candidate.engine
```

动态模型可用 `-InputShape 'images:1x3x384x480'` 指定模型实际输入名称/形状。训练、采集研究和旧分析脚本不再维护，模型和数据等本机资产没有删除。

## 目录与入口

| 位置 | 责任 |
| --- | --- |
| [native](native/README.md) | 统一 CMake 构建、runtime、控制器、Vision、鼠标及 Fusion |
| native/desktop_native | 原生助手窗口、配置文件/进程/IPC 适配器 |
| scripts/launch | 手柄、鼠标与 Fusion 的 Windows 启停入口 |
| tools | 原生构建、engine 构建、Vision 检查与静态工具 |
| runtime_deps | 本机原生设备 DLL 的输入位置 |
| models、runs、artifacts、training_data | 模型、日志和本机资产，非源码运行依赖清单 |
| [docs/project/CURRENT_STATE.md](docs/project/CURRENT_STATE.md) | 能力、依赖、状态归属、源码依据及当前任务记录 |

前台排障使用 `scripts/launch/gamepad_start.bat` 或 `scripts/launch/mouse_start.bat`；后台手柄使用 `gamepad_native_background_start.vbs` / `stop.vbs`。Fusion 仍有单独的后台启停脚本；需要启用了通道的 runtime，attach-only 不会启动它。

## 现行约定

完整控制链保持同 tick 执行。手动输入保持零软件死区；AI 手动 intent 权重在 15%–30% 连续变化。目标搜索、取得、身份、交接、停止和资源释放由现有原生所有者维护。诊断和 Fusion 不取得控制权。

普通基础/功能测试与数值仿真保留；历史 incident 报告、SHA256 溯源和 benchmark 比较/发布裁决框架已退役。仿真通过不等于真实设备或游戏表现验收。历史文档中的 Python 入口与旧测试说明只记录当时状态，不能用于当前接续。