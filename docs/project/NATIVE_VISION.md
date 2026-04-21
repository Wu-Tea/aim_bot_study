# Native Vision Scaffold

Last updated: 2026-04-21

## Current Status

Native vision is currently a Phase 3B scaffold, not the production gamepad path.

The scaffold proves these things:

- the Windows C++ toolchain can build inside this repo
- C++ TensorRT can load `models/best.engine`
- Python can later call into a native extension through pybind11
- C++ can accept one CPU RGB frame, preprocess it on CUDA, run TensorRT, and return `DetectionBatch`
- C++ can capture a centered desktop ROI into a native `D3D11Texture` `FramePacket`
- C++ now exposes a native `VisionEngine` / `VisionResult` boundary
- `VisionEngine` can map the live ROI texture into CUDA and run TensorRT without returning frames to Python
- C++ now exposes a stateful native target selector for synthetic parity tests and live engine integration
- native color classification now runs inside the C++ selector path for both pybind tests and the live debug engine
- native occlusion compensation now carries `observed`, `reconstructed`, and short-horizon `predicted` target sources
- native auto-fire gating now follows selected-target `fire_zone` plus release grace semantics
- native aim enhancement now applies lead prediction, catchup boost, and near-target damping after selection
- a standalone `vision_native_debug` executable can run the live native loop and print result/perf fields
- the debug loop now reports real `preprocess_ms`, `infer_ms`, and `boxes_seen` values from native inference

Production still uses the Python `vision` package. The native scaffold is only used through the explicit tools in `tools/`.

## Pipeline Status

Current native progress is easiest to understand in the real runtime order:

### 1. Capture / Screenshot

Status: substantially done for the native path.

What is already native:

- Desktop Duplication setup and output selection
- centered ROI capture instead of full-screen-then-crop
- copy into a small native `D3D11Texture`
- live capture smoke validation through `run_native_vision_capture_smoke.ps1`

What this means in practice:

- the native path is no longer blocked on screenshot plumbing
- Python does not need the ROI pixels back for the native debug path
- the capture stage is already suitable for further native parity work

Remaining risk at this stage:

- DXGI desktop access can still fail in restricted contexts such as `0x80070005`
- production startup still uses the Python runtime, so native capture is not yet the default gamepad path

### 2. Detection / Recognition

Status: substantially done for the native path.

What is already native:

- `models/best.engine` loading in C++ TensorRT
- BGRA ROI texture registration through CUDA D3D11 interop
- GPU-side BGRA -> normalized CHW preprocessing
- native TensorRT enqueue and decode into `DetectionBatch`
- real `preprocess_ms`, `infer_ms`, `decode_ms`, and `boxes_seen` reporting

What this means in practice:

- screenshot and detection are already connected end-to-end in native code
- the current native debug loop is doing real capture -> infer work, not placeholder timing
- the main unresolved work is no longer detector plumbing

Remaining risk at this stage:

- no claim yet that native detection is faster in real gameplay than the Python production path
- production runner still uses Python vision as the behavior oracle and fallback

### 3. Target Selector

Status: core Phase 3B parity is implemented for the native debug path; rollout validation is still pending.

What is already native:

- a stateful `NativeTargetSelector` exposed through pybind
- two-frame pickup confirmation for first lock
- upper-chest aim point derived from the selected body box
- geometry/confidence gating
- multi-candidate scoring based on crosshair distance, confidence, area heuristics, and tracking bonus
- two-frame switch confirmation before replacing the active target
- friendly/enemy color classification
- partial-occlusion upper-body reconstruction from recent stable height
- short occlusion prediction for up to two empty detection frames
- immediate return to `observed` when a real detection is reacquired after prediction
- auto-fire recommendation from selected-target `fire_zone` with four-frame release grace
- aim enhancement through native lead prediction, catchup boost, and near-target damping
- live `VisionEngine` integration, so debug output already reflects the native selector instead of a highest-confidence placeholder

What is still not native:

- production startup and controller handoff through `gamepad_start.bat`
- recorded-scene one-to-one parity validation against the Python runtime
- performance comparison against the Python production path

Bottom line:

- **capture:** basically in place
- **recognition:** basically in place
- **selector:** lock/switch/color/occlusion/enhancement/auto-fire parity is now implemented in native code, with rollout/perf validation still pending

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

Phase 1 supports only `CpuHwc + RGB8`. Phase 2 adds `D3D11Texture + BGRA8` without changing the inference result contract. Phase 3A consumes that GPU texture directly through CUDA D3D11 interop instead of copying the frame back into Python.

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

`VisionResult` appears only after Phase 3. In the current Phase 3B checkpoint it carries real native timing, box-count fields, native target selection, auto-fire recommendation, and enhanced `dx` / `dy`. Production Python vision still remains the runtime used by `gamepad_start.bat`.

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

The capture smoke only proves that native C++ can produce `FramePacket(D3D11Texture + BGRA8)`. Phase 3A is the first checkpoint that feeds that ROI texture directly into native TensorRT preprocessing, but it is still not used by `gamepad_start.bat`.

Desktop Duplication can return access denied when run from a restricted shell or while another protected desktop state is active. If the smoke fails with `0x80070005`, rerun it from a normal desktop PowerShell session before treating it as a code regression.

## Phase 3 Foundation Target

The first native Phase 3 checkpoint was not a direct production switch. It established three explicit deliverables:

- `VisionResult` as the final native-to-Python payload contract
- `VisionEngine` as the long-lived native runtime boundary
- `vision_native_debug` as a standalone native verification program

The debug program is part of the migration plan, not an optional extra. It exists to validate native capture, inference, targeting, and perf accounting before `gamepad_start.bat` is allowed to default to the native backend.

That foundation milestone is now complete. The follow-on checkpoint is Phase 3A: native capture-to-inference wiring.

## Phase 3A Capture-To-Inference Bridge

Phase 3A adds the first real end-to-end native hot path:

- `DXGIOutputCapture` publishes a centered ROI as a native `D3D11Texture`
- `VisionEngine` registers that ROI texture with CUDA through `cudaGraphicsD3D11RegisterResource`
- each `poll_once()` maps the texture, reads a `cudaArray_t`, and feeds it into `TensorRTEngine`
- CUDA preprocessing converts BGRA ROI pixels into normalized CHW input for `models/best.engine`
- `VisionResult` now reports real `preprocess_ms`, `infer_ms`, `post_ms`, `age_ms`, and `boxes_seen`

Phase 3A limitation snapshot:

- target selection was still a minimal best-detection placeholder at that checkpoint
- `target_source` was only `observed`
- occlusion compensation, enhancement, and auto-fire parity were still native TODOs
- Python still owned the production runtime and remained the behavior oracle

Phase 3A is now complete. The next active checkpoint is Phase 3B.

## Phase 3B Native Target Selector Core

Phase 3B begins the migration of Python targeting state into native code.

The current native slice includes:

- a stateful `NativeTargetSelector` pybind entry point for synthetic parity tests
- two-frame pickup confirmation for first lock
- upper-chest target point generation from the selected body box
- multi-candidate scoring based on crosshair distance, confidence, area heuristics, and tracking bonus
- friendly/enemy color classification for the color band above the body box
- two-frame switch confirmation before replacing the active target
- partial-occlusion reconstruction and two-frame short-horizon prediction
- `target_source` parity for `observed`, `reconstructed`, and `predicted`
- native auto-fire gate matching selected-target `fire_zone` and release grace behavior
- native aim enhancement pipeline matching the current lead/catchup/damping model
- live `VisionEngine` integration, so the debug executable now uses the native selector instead of a highest-confidence placeholder

Current limitation:

- production `gamepad_start.bat` still uses the Python vision runtime
- recorded gameplay parity and performance validation have not been completed yet
- the current live-engine implementation downloads the full `640x512` BGRA ROI to host memory once detections exist, then runs CPU HSV classification; this is acceptable for parity work but is not the final low-latency form

## Phase 4 Gate

Phase 4 must not start until the native debug harness can prove all of the following in a repeatable way:

- native live capture, inference, and post-processing all run in one executable
- the program prints or records `VisionResult` and perf timing fields
- the native output is comparable against the Python baseline for the same scenarios
- cold-start and steady-state behavior are both understood

Only after that checkpoint should the normal Python startup path begin loading native vision by default.

## Phase 3 Foundation Debug

Run the current standalone native debug program with:

```powershell
.\tools\run_native_vision_debug.ps1 -BuildFirst
```

The current output proves the `VisionEngine -> VisionResult` boundary, live ROI capture loop, real native capture-to-inference timing, and the current native targeting/enhancement/auto-fire slice. It still does not prove production readiness because recorded-scene parity and gamepad startup integration remain pending.

## What This Does Not Do Yet

- it does not yet prove full Python `TargetSelector` parity on recorded gameplay cases
- it does not replace `vision.runner`
- it does not promise any FPS or latency improvement yet

The next real phase is to validate this native path against recorded or live gameplay scenarios, then decide whether the current host-side color sampling needs to be replaced with a smaller ROI-copy or GPU-side path. Only after that should we compare performance against the Python vision baseline in a meaningful way and consider production startup integration.
