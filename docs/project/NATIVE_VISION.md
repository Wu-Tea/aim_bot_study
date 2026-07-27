# Native Vision

Last updated: 2026-06-11

## Current Status

Native vision is now one component inside the default full native C++ gamepad
runtime. The normal gamepad path is:

```text
scripts\launch\gamepad_start.bat
-> scripts\launch\gamepad_native_cpp_start.bat
-> native\vision_native\build\Release\cod_native_runtime.exe
```

The older Python-hosted native vision bridge remains available for debug,
fallback, and comparison work, but it is not the normal live gamepad path.

The scaffold proves these things:

- the Windows C++ toolchain can build inside this repo
- C++ TensorRT can load `models/best.engine`
- Python can call into the native extension through pybind11 for fallback/debug paths
- C++ can accept one CPU RGB frame, preprocess it on CUDA, run TensorRT, and return `DetectionBatch`
- C++ can capture a centered desktop ROI into a native `D3D11Texture` `FramePacket`
- C++ now exposes a native `VisionEngine` / `VisionResult` boundary
- `VisionEngine` can map the live ROI texture into CUDA and run TensorRT without returning frames to Python
- C++ now exposes a stateful native target selector for synthetic parity tests and live engine integration
- native color classification now runs inside the C++ selector path for both pybind tests and the live debug engine
- native empty detection frames now clear the selected target instead of predicting through the gap
- native target sources currently exposed to Python are `observed`, `cue_hold`, or an empty string when no target is active
- native auto-fire is suppressed on no-target and cue-hold gap frames, then resumes only after a fresh observed target is confirmed inside the selected-target `fire_zone`
- native aim enhancement now applies lead prediction, catchup boost, and near-target damping after selection
- synthetic Python-vs-native tests now compare shared lock/color behavior and explicitly document the native-vs-Python difference on empty detection gaps
- a standalone `vision_native_debug` executable can run the live native loop and print result/perf fields
- the debug loop now reports real `preprocess_ms`, `infer_ms`, and `boxes_seen` values from native inference
- Python can still run `NativeVisionEngine` through `--vision-backend native` for fallback/debug paths
- `scripts\launch\debug\gamepad_native_debug.bat` starts the C++ vision + Python controller bridge with a synthetic debug window
- `cod_native_runtime.exe` consumes `VisionEngine` directly in process and passes `VisionResult` to the native gamepad controller

The default `scripts\launch\gamepad_start.bat` path now uses full native C++ gamepad runtime. Runtime defaults come from the local project-root `config.toml` when that file exists. Python vision remains available only through the Python fallback path, for example `GAMEPAD_RUNTIME=python` plus `--vision-backend python` or equivalent debug scripts.

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
- native capture is now the production default, so long-play stability and recovery behavior matter more than before

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

- live gameplay logs should now be read from native C++ runtime output first
- Python vision remains a fallback/comparison path, not the default runtime oracle

### 3. Target Selector

Status: core Phase 3B targeting is implemented for the native debug path; rollout validation is still pending.

What is already native:

- a stateful `NativeTargetSelector` exposed through pybind
- two-frame pickup confirmation for first lock
- posture-aware aim point derived from the selected body box: standing/upper-body, crouched, and wide-low prone/side boxes use different vertical target ratios
- geometry/confidence gating
- multi-candidate scoring based on crosshair distance, confidence, area heuristics, and tracking bonus
- two-frame switch confirmation before replacing the active target
- friendly/enemy color classification
- partial-occlusion handling that keeps the visible observed box rather than reconstructing a hidden box
- immediate target clear on empty detection frames in the production native selector
- reacquire after an empty gap requires a fresh confirmation frame before native target output resumes
- auto-fire recommendation from selected-target `fire_zone`, with fire suppressed during no-target and cue-hold gap frames
- aim enhancement through native lead prediction, catchup boost, and near-target damping
- live `VisionEngine` integration, so debug output already reflects the native selector instead of a highest-confidence placeholder

What still needs validation:

- recorded-scene validation against the Python runtime, treating empty-gap prediction as an intentional policy difference unless reopened
- performance comparison against the Python production path

Bottom line:

- **capture:** basically in place
- **recognition:** basically in place
- **selector:** lock/switch/color/cue-hold/enhancement/auto-fire behavior is implemented in native code, with production-path rollout/perf validation still pending

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

    const char* target_source; // observed, cue_hold, or "" when no target is active

    float wait_ms;
    float preprocess_ms;
    float infer_ms;
    float post_ms;
    float age_ms;
    float boxes_seen;
};
```

`VisionResult` appears only after Phase 3. In the current Phase 3B checkpoint it carries real native timing, box-count fields, native target selection, auto-fire recommendation, and enhanced `dx` / `dy`. Python consumes it through `vision.native_runner.process_native_vision`; controller code stays in Python.

The native result timing fields are not the whole controller-output latency by themselves. The Python bridge now also reports derived diagnostics for source age, native pipeline age, Python handoff, controller consume age, and final virtual-output age when `VISION_PERF_LOG=1`.

The native runtime engine path is read from `model_path` in `[runtime.vision]`. Lower-level `VisionEngine` callers that do not pass an explicit engine path still fall back to `VISION_MODEL_PATH` and then the built-in default.

### Capture/Tensor size contract

The native runtime supports a physical capture ROI that is larger than the
TensorRT input. CUDA preprocessing resizes capture pixels to the engine input,
and decoded boxes are scaled back to capture coordinates before selection,
color readback, tracking and controller use.

`[runtime.vision]` now makes this relationship explicit:

```toml
capture_width = 480
capture_height = 416
tensor_width = 480
tensor_height = 416
require_isotropic_resize = true
model_path = "models/best.engine"
```

`tensor_width` and `tensor_height` must match the actual static TensorRT engine
input. The runtime validates the engine after loading and fails startup on a
mismatch. With `require_isotropic_resize = true` (the default), capture and
Tensor aspect ratios must also match, preventing different X/Y scale factors
from silently distorting detection coordinates.

Validated exploration combinations are:

| Purpose | Capture | Tensor | Engine |
|---|---:|---:|---|
| production reference | `480x416` | `480x416` | `models/best.engine` |
| balanced wide | `640x512` | `480x384` | `models/best_480x384.engine` |
| near coverage | `640x440` | `512x352` | `models/best_512x352.engine` |
| latency neutral | `630x462` | `480x352` | `models/best_480x352.engine` |

Change the capture pair, Tensor pair and engine path together. Startup emits a
`[VisionGeometry][CPP]` line with actual capture/Tensor sizes and scale. When
telemetry or performance logging is enabled, `session.json` schema v2 records
the same size contract, model path and isotropic-resize policy.

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

The capture smoke only proves that native C++ can produce `FramePacket(D3D11Texture + BGRA8)`. The production native runtime already feeds that ROI texture into TensorRT through `VisionEngine`.

Desktop Duplication can return access denied when run from a restricted shell or while another protected desktop state is active. If the smoke fails with `0x80070005`, rerun it from a normal desktop PowerShell session before treating it as a code regression.

## Phase 3 Foundation Target

The first native Phase 3 checkpoint was not a direct production switch. It established three explicit deliverables:

- `VisionResult` as the final native-to-Python payload contract
- `VisionEngine` as the long-lived native runtime boundary
- `vision_native_debug` as a standalone native verification program

The debug program is part of the migration plan, not an optional extra. It exists to validate native capture, inference, targeting, and perf accounting before and after `scripts\launch\gamepad_start.bat` defaults to the native backend.

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
- at that checkpoint Python still owned the production runtime and remained the behavior oracle; this is no longer true for the default gamepad path

Phase 3A is now complete. The next active checkpoint is Phase 3B.

## Phase 3B Native Target Selector Core

Phase 3B begins the migration of Python targeting state into native code.

The current native slice includes:

- a stateful `NativeTargetSelector` pybind entry point for synthetic parity tests
- two-frame pickup confirmation for first lock
- posture-aware target point generation from the selected body box
- multi-candidate scoring based on crosshair distance, confidence, area heuristics, and tracking bonus
- friendly/enemy color classification for the color band above the body box
- two-frame switch confirmation before replacing the active target
- partial-occlusion reconstruction and two-frame short-horizon prediction
- `target_source` parity for `observed`, `reconstructed`, and `predicted`
- native auto-fire gate matching selected-target `fire_zone` and release grace behavior
- native aim enhancement pipeline matching the current lead/catchup/damping model
- live `VisionEngine` integration, so the debug executable now uses the native selector instead of a highest-confidence placeholder

Current limitation:

- default production `scripts\launch\gamepad_start.bat` now uses the full native C++ gamepad runtime
- synthetic parity is covered by `tests/test_native_vision_synthetic_parity.py`, but recorded gameplay parity and performance validation have not been completed yet
- the current live-engine implementation downloads the full `640x512` BGRA ROI to host memory once detections exist, then runs CPU HSV classification; this is acceptable for parity work but is not the final low-latency form

## Synthetic Parity Harness

The current parity harness lives in:

```powershell
py -3 -m unittest tests.test_native_vision_synthetic_parity -v
```

It runs the same synthetic detection sequences through:

- Python `TargetSelector` + `CrosshairPersonHitDetector` + `AimEnhancementPipeline`
- native `NativeTargetSelector` + `NativeAimEnhancer`

The harness currently covers:

- center lock with auto-fire release grace and enhancement
- friendly filtering plus enemy target pickup
- short occlusion prediction and reacquire behavior

This is not a replacement for recorded gameplay validation. It is the deterministic guardrail that should fail first when native selector, enhancement, or auto-fire behavior drifts away from the Python behavior oracle.

## Phase 4 Gate

Phase 4 must not start until the native debug harness can prove all of the following in a repeatable way:

- native live capture, inference, and post-processing all run in one executable
- the program prints or records `VisionResult` and perf timing fields
- the native output is comparable against the Python baseline for the same scenarios
- cold-start and steady-state behavior are both understood

That checkpoint has been superseded by the full native C++ gamepad runtime. `scripts\launch\gamepad_start.bat` now starts `cod_native_runtime.exe` by default, while keeping the Python path available as a fallback.

## Phase 3 Foundation Debug

Run the current standalone native debug program with:

```powershell
.\tools\run_native_vision_debug.ps1 -BuildFirst
```

The current output proves the `VisionEngine -> VisionResult` boundary, live ROI capture loop, real native capture-to-inference timing, and the current native targeting/enhancement/auto-fire slice. Default gamepad startup now consumes this path inside `cod_native_runtime.exe`.

## Python Controller Bridge Fallback

Run the C++ vision + Python controller bridge only when you specifically need
the fallback/debug path:

```powershell
.\scripts\launch\debug\gamepad_native_debug.bat
```

The normal debug script also exposes the same native path and defaults to it:

```powershell
.\scripts\launch\debug\gamepad_debug.bat
```

`scripts\launch\gamepad_start.bat`, `scripts\launch\debug\gamepad_debug.bat`, and `scripts\launch\debug\gamepad_native_debug.bat` default to `VISION_CAPTURE_FPS=140` unless the environment variable is already set.

The gamepad scripts also default `VISION_QUIT_KEY=0`. This disables the old keyboard quit hotkey, which could otherwise stop the process during gameplay if the game or user input touched the same key.

or directly:

```powershell
py -3.11 main.py --controller-mode gamepad --vision-backend native --vision-debug
```

This fallback path keeps controller code in Python and replaces only the vision loop:

- Python creates the existing gamepad controller
- `vision.native_runner` loads `vision_native_cpp` from `native/vision_native/build/Release`
- `NativeVisionEngine.poll_once()` runs native ROI capture, TensorRT inference, target selection, enhancement, and auto-fire recommendation
- Python forwards `dx`, `dy`, `ControllerTarget`, and delayed auto-fire state to the existing controller interface
- perf logging uses native `wait_ms`, `infer_ms`, `post_ms`, `age_ms`, and `boxes_seen`

The `--vision-debug` window is currently a synthetic native-result canvas. It shows target point, body box, source, `dx/dy`, auto-fire state, and native timings. It does not display the real ROI image yet because the current native contract intentionally avoids copying frame pixels back to Python.

## What This Does Not Do Yet

- it does not yet prove full Python `TargetSelector` parity on recorded gameplay cases
- it does not remove the Python `vision.runner` fallback
- it is not the primary source of truth for live gamepad runtime behavior; use `docs/project/NATIVE_CPP_RUNTIME.md` and C++ runtime logs first
- it does not yet prove long-session stability across all gameplay scenes

The next real phase is to validate this native path against recorded or live gameplay scenarios, then decide whether the current host-side color sampling needs to be replaced with a smaller ROI-copy or GPU-side path. Native is now the default production startup, so any accuracy or stability regression should be treated as a production-path issue with Python available as a fallback.
