# Native Vision Scaffold

Last updated: 2026-04-21

## Current Status

Native vision is currently a Phase 2 capture/inference scaffold, not the production gamepad path.

The scaffold proves five things:

- the Windows C++ toolchain can build inside this repo
- C++ TensorRT can load `models/best.engine`
- Python can later call into a native extension through pybind11
- C++ can accept one CPU RGB frame, preprocess it on CUDA, run TensorRT, and return `DetectionBatch`
- C++ can capture a centered desktop ROI into a native `D3D11Texture` `FramePacket`

Production still uses the Python `vision` package. The native scaffold is only used through the explicit tools in `tools/`.

## Migration Protocol

Native vision migration uses three explicit data contracts. These contracts are the boundary between phases and should not be bypassed by returning raw frames or raw TensorRT buffers to Python.

### `FramePacket`

`FramePacket` is the capture-to-inference payload.

```cpp
enum class PixelFormat {
    RGB8,
    BGRA8,
};

enum class MemoryKind {
    CpuHwc,
    D3D11Texture,
};

struct FramePacket {
    uint64_t frame_id;
    uint64_t captured_at_ns;
    int width;
    int height;
    PixelFormat format;
    MemoryKind memory_kind;
    int row_pitch;
    void* data;
};
```

Phase 1 supports only `CpuHwc + RGB8`. Phase 2 adds `D3D11Texture + BGRA8` without changing the inference result contract.

### `DetectionBatch`

`DetectionBatch` is the inference-to-targeting payload.

```cpp
struct Detection {
    float x1;
    float y1;
    float x2;
    float y2;
    float conf;
    int class_id;
};

struct DetectionBatch {
    uint64_t frame_id;
    uint64_t captured_at_ns;
    uint64_t inferred_at_ns;
    int frame_width;
    int frame_height;
    std::vector<Detection> detections;
    float preprocess_ms;
    float infer_ms;
    float decode_ms;
};
```

Phase 1 ends at `DetectionBatch`. It does not select targets, shape aim deltas, or recommend auto-fire.

### `VisionResult`

`VisionResult` is the final native-to-Python controller payload.

```cpp
struct VisionResult {
    uint64_t frame_id;
    uint64_t captured_at_ns;
    uint64_t inferred_at_ns;
    uint64_t result_at_ns;

    bool has_target;
    bool auto_fire;

    float dx;
    float dy;
    float target_x;
    float target_y;
    float screen_center_x;
    float screen_center_y;

    bool has_body_box;
    float body_x1;
    float body_y1;
    float body_x2;
    float body_y2;

    const char* target_source; // observed, reconstructed, predicted

    float wait_ms;
    float preprocess_ms;
    float infer_ms;
    float post_ms;
    float age_ms;
    float boxes_seen;
};
```

`VisionResult` appears only after Phase 3. Until then, production Python vision remains the runtime used by `gamepad_start.bat`.

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

## Phase 1 Inference Smoke

Phase 1 adds a native inference smoke path:

- accept one `512x640x3` CPU RGB `uint8` frame
- run CUDA preprocessing into the TensorRT input buffer
- run TensorRT inference with `models/best.engine`
- decode `[1,300,6]` output into `DetectionBatch`
- expose the result through pybind for tests and manual smoke checks

Phase 1 still does not capture frames or replace the production runner.

Run it with:

```powershell
.\tools\run_native_vision_infer_smoke.ps1 -BuildFirst
```

The first inference on a fresh `NativeEngine` includes TensorRT/CUDA warmup cost and is not a steady-state benchmark. Reuse `NativeEngine.infer_rgb(...)` in a loop when measuring hot-path latency.

## Phase 2 Capture Smoke

Phase 2 adds a native DXGI ROI capture smoke path:

- select an attached DXGI output, preferring the primary-like output that contains desktop origin
- create a D3D11 device and Desktop Duplication session
- copy only the centered ROI into a native `DXGI_FORMAT_B8G8R8A8_UNORM` texture
- expose metadata through pybind without returning full image pixels to Python

Run it with:

```powershell
.\tools\run_native_vision_capture_smoke.ps1 -BuildFirst
```

The capture smoke only proves that native C++ can produce `FramePacket(D3D11Texture + BGRA8)`. It is not connected to TensorRT preprocessing yet and is not used by `gamepad_start.bat`.

Desktop Duplication can return access denied when run from a restricted shell or while another protected desktop state is active. If the smoke fails with `0x80070005`, rerun it from a normal desktop PowerShell session before treating it as a code regression.

## What This Does Not Do Yet

- it does not wire native capture into TensorRT preprocessing
- it does not run controller targeting
- it does not replace `vision.runner`
- it does not promise any FPS or latency improvement yet

The next real phase is DXGI ROI capture plus GPU preprocessing inside native code. Only after that should we compare performance against the Python vision baseline.
