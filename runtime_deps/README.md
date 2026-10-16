# 原生设备库

将 Windows x64 的 `SDL2.dll` 和 `ViGEmClient.dll` 放在 `windows-x64/`，或向 `tools/build_native_runtime.ps1` 传入 `-SDL2Dll` / `-ViGEmClientDll`。启用 ViGEm 输出需要安装对应设备驱动。

这些 DLL 是原生依赖，保留在本地并被 Git 忽略。原生控制链无需 Python；React/WebView2 桌面助手需要 Python/pywebview，参见 [构建与运行](../docs/BUILD_AND_RUN.md)。
