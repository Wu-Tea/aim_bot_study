# 完整原生 runtime

`native/CMakeLists.txt` 是整个 C++ 项目的统一构建入口。Vision 是捕获、推理
和目标选择模块；它与控制器、runtime、鼠标和 Fusion 一起由顶层项目组装。

| 构建清单 | 责任 |
| --- | --- |
| `vision_native/CMakeLists.txt` | Vision 核心库 |
| `controller_native/controller_targets.cmake` | 当前控制器核心与隔离的历史 recoil 工具库 |
| `runtime_app/runtime_targets.cmake` | 手柄 runtime、构建身份与运行依赖 |
| `overlay_canvas/fusion_targets.cmake` | Fusion Canvas 与显示契约测试 |
| `mouse_native/mouse_targets.cmake` | 鼠标 runtime、传输和契约测试 |
| `cmake/ProductTests.cmake` | Base 基础契约与可选 Feature 功能测试 |
| `cmake/OfflineBenchmarks.cmake` | 显式启用的离线 benchmark 与扫描目标 |

包含的 `.cmake` 文件按 `native/` 解析源码路径；Vision 使用独立子目录。
现有 C++ 模块目录、namespace、include 路径和 target 名称保留。

## 构建

从项目根目录运行：

```powershell
.\tools\build_native_runtime.ps1
```

默认输出位于 `native/build/Release/`，包括手柄与鼠标 runtime、Fusion
Canvas、Vision smoke 工具和原生助手 `cod_native_assistant`。
脚本默认启用 ViGEm。可通过 `-CudaArchitectures` 指定 CUDA 架构，通过
`-BuildDir` 指定独立构建目录，通过 `-OfflineBenchmarks` 显式启用离线测试。
未指定架构时使用 CMake 中的默认值。

手动配置使用 `cmake -S native -B native/build ...`。旧
`vision_native/build*` 保留为本机历史构建；它们的 CMake cache 绑定旧源码
入口，不能直接搬到新位置继续使用。当前启动器和原生助手使用新构建。

## 验证

```powershell
ctest --test-dir native/build -C Release --output-on-failure
```

基础测试按配置/协议、视觉、freshness、ADS、BodyLock 和端到端归属组织；旧 incident 报告驱动已精简，当前产品约定直接在基础测试断言。Feature、Mouse、Fusion 保留现行功能和资源边界检查。离线仿真由 `-OfflineBenchmarks` 显式启用，不恢复比较门禁或发布裁决。

手柄启动入口仍是根目录 `启动助手.vbs` 和 `scripts/launch/`。本机配置、
模型、日志和数据集仍以项目根目录作为资源根。

HidHide 的应用允许列表绑定可执行文件的完整路径。使用 HidHide 隐藏物理
手柄时，应将 `native/build/Release/cod_native_runtime.exe` 登记到允许列表；
旧构建路径的登记不会自动覆盖新路径。项目保留手动配置方式，启动脚本不
自动修改 HidHide 的设备或应用设置。
