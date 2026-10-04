# Native device libraries

Place the Windows x64 `SDL2.dll` and `ViGEmClient.dll` in `windows-x64/`, or
pass `-SDL2Dll` / `-ViGEmClientDll` to `tools/build_native_runtime.ps1`.
These are native device libraries, not Python modules. Machine-local DLLs
are ignored; install the corresponding device driver separately.

The existing worktree libraries were copied from the previous native build.
Fresh machines need their own licensed SDK/runtime distribution. SDL is an
optional physical input adapter (XInput remains available); enabled ViGEm
output needs its client library and driver. No Python installation is required.
