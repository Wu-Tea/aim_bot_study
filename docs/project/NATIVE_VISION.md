# Native Vision Scaffold

Last updated: 2026-04-21

## Current Status

Native vision is currently a smoke-test scaffold, not the production gamepad path.

The scaffold proves three things:

- the Windows C++ toolchain can build inside this repo
- C++ TensorRT can load `models/best.engine`
- Python can later call into a native extension through pybind11

Production still uses the Python `vision` package. The native scaffold is only used through the explicit tools in `tools/`.

## Environment

Expected local paths:

```powershell
CUDA:     C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1
TensorRT: D:\env\TensorRT-10.15.1.29
Python:   D:\env\python\python.exe
```

Required components:

- Visual Studio 2022 C++ build tools
- CUDA Toolkit 13.1
- TensorRT 10.15.1.29 Windows SDK
- pybind11 installed in the active Python environment

## Build

```powershell
.\tools\build_native_vision.ps1
```

The script configures `native/vision_native` with VS CMake and builds Release outputs under:

```text
native\vision_native\build\Release
```

## Smoke Test

```powershell
.\tools\run_native_vision_smoke.ps1 -BuildFirst
```

Success means the C++ executable loads `models/best.engine` and prints the TensorRT input/output tensor names, modes, dtypes, and shapes.

The current `best.engine` is an Ultralytics-exported engine container. It starts with a small metadata prefix before the TensorRT plan bytes. The C++ inspector explicitly detects and skips this prefix before calling TensorRT deserialization; direct `trt.Runtime.deserialize_cuda_engine(path.read_bytes())` will fail on this file.

## What This Does Not Do Yet

- it does not capture frames
- it does not preprocess images
- it does not run controller targeting
- it does not replace `vision.runner`
- it does not promise any FPS or latency improvement yet

The next real phase is DXGI ROI capture plus GPU preprocessing inside native code. Only after that should we compare performance against the Python vision baseline.
