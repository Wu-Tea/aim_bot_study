# yolo-study-001

Windows 上的 YOLO + TensorRT 手柄辅助瞄准研究项目。当前默认实战路径是
完整的原生 C++ runtime；Python 主要保留为 fallback、训练导出和调试工具。

## 快速启动

双击根目录的 **`启动助手.vbs`** 打开 GUI：选择通用 / COD、Apex 或 BO3，调整设置后启动。
同一个窗口可以保存配置、切换游戏、热重载、查看学习参数和正常停止；Fusion 使用单独的启停按钮。GUI、主程序和 Fusion 各自运行，关闭窗口后后台继续工作，再打开会接管已有实例。
详细说明见 [手柄助手 GUI](docs/project/DESKTOP_ASSISTANT.md)。

所有游戏共用 `config.toml` 和原来的原生程序；游戏差异放在 `[games.apex.*]`、`[games.bo3.*]` 分块中。
GUI 保存前校验配置并备份原文件；力度、压枪、fire 按键、友方过滤与目标点支持热重载，成功后清理响应学习。模型、帧率等参数会提示重启。旧后台脚本作为兼容入口保留，也使用同一套启停管理。

以下前台入口用于开发与排障：

前台启动：

```powershell
.\scripts\launch\gamepad_start.bat
```

直接启动原生 runtime：

```powershell
.\scripts\launch\gamepad_native_cpp_start.bat
```

双击后台启动和停止：

```text
scripts\launch\gamepad_native_background_start.vbs
scripts\launch\gamepad_native_background_stop.vbs
```

后台脚本只隐藏控制台窗口，不会隐藏 Windows 进程。

本机调参使用根目录下被 Git 忽略的 `config.toml`。没有该文件时使用代码默认值；
可从 `config.native.example.toml` 复制一份作为起点。

## 当前运行契约

- 默认 runtime：`native/vision_native/build/Release/cod_native_runtime.exe`
- 本机共用捕获尺寸：`640x512`，模型输入：`480x384`（以 `config.toml` 为准）
- 共用 TensorRT engine：`models/best_480x384.engine`；Apex 使用游戏分块指定的专属 engine
- 原生入口：`native/runtime_app/main.cpp`
- Vision：`native/vision_native/`
- tracker/controller/output：`native/controller_native/`
- Python fallback：`main.py`

捕获尺寸与模型输入须遵守配置中的缩放约束。修改模型输入尺寸前必须准备对应 engine，
并重新跑启动和 Vision smoke test。

## 本地结构化日志

需要保存可分析的本地运行数据时，在 `config.toml` 中启用：

```toml
[runtime.telemetry]
enabled = true
mode = "profile"
manual_controller_hz = 100
vision_on_new_frame = true
candidate_details = "on_event"
queue_capacity = 8192
rotate_size_mb = 256
max_files = 10
```

日志写入 `runs/native_perf/<session>/`。`--perf-log` 是控制台性能诊断开关，
不能替代结构化 telemetry。更多说明见
[Native Debug Log Sessions](docs/project/NATIVE_LOG_SESSIONS.md) 和
[Native runtime telemetry](docs/benchmarks/native-runtime-telemetry.md)。

## 构建与验证

原生构建依赖 Windows、Visual Studio 2022 C++、CUDA、TensorRT 和 pybind11。

```powershell
.\tools\build_native_vision.ps1
.\scripts\verify\native_pipeline_contract.bat
python -m unittest tests.test_main_cli tests.test_startup_scripts -v
```

更完整的构建、fallback 和 smoke test 说明见
[Native C++ Runtime](docs/project/NATIVE_CPP_RUNTIME.md) 与
[Native Vision](docs/project/NATIVE_VISION.md)。

## 当前架构

```text
Vision evidence + physical intent
  -> selector
  -> TargetCoordinator / tracker-owned TargetPlan
  -> ADS acquisition or BodyLock follow
  -> one dynamics shaper
  -> one vector intent fusion path
  -> ADS-only brake
  -> recoil feed-forward
  -> ViGEm virtual DualShock 4 (native C++ gamepad output)
```

详细现状、已验证边界和正在进行的方向见
[Current State](docs/project/CURRENT_STATE.md)。

原生 gamepad 已直接转写为有线 DS4，沿用原启动器，无需增加配置项。
映射、验证与兼容性边界见 [DS4 输出切换](docs/project/DS4_OUTPUT_20260919.md)。

## 文档入口

- [完整文档地图](docs/README.md)
- [当前项目状态](docs/project/CURRENT_STATE.md)
- [项目参考文档](docs/project/README.md)
- [Benchmark 入口](docs/benchmarks/README.md)
- [历史归档](docs/archive/README.md)
- [跨项目实时控制优化方法](docs/methods/REALTIME_CONTROL_OPTIMIZATION_START_HERE.md)

历史计划、旧基线和阶段验收不代表当前 runtime 行为；需要追溯原因时从归档入口进入。
