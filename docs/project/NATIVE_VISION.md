# Native Vision

This document describes the native Vision implementation used by the default
C++ gamepad runtime. Historical migration phases and the retired native
prediction/enhancement parity layer are recoverable from Git history; they are
not current architecture.

## Current Role

`vision_native::VisionEngine` owns:

- centered DXGI region capture;
- BGRA/RGB handling and CUDA preprocessing;
- fixed-shape TensorRT inference;
- detection decode;
- native color/cue classification and target selection;
- compact `VisionResult` publication.

It does not own final stick shaping, manual/AI arbitration, target projection
between detector results, or recoil. Those belong to the controller boundary.

## Current Native Flow

```text
DXGI capture
  -> CUDA preprocess
  -> fixed-shape TensorRT inference
  -> decode detections
  -> native target selector
       -> hostile/friendly/corpse evidence
       -> observed or weak-observed target
       -> optional same-generation visible cue continuation
  -> VisionResult
```

`VisionResult` carries target geometry, source/authority fields, body-box
metadata, source identity and native timing. The selector publishes raw current
target error. The removed native `AimEnhancementPipeline` no longer applies a
second lead, catch-up or near-target damping pass after selection.

## Evidence and Authority

Current native target sources are:

- `observed`;
- `associated_weak`;
- `weak_observed`;
- `cue_hold`;
- empty/no target.

`observed` may grant fire authority when its fire-zone conditions are met.
Weak and cue evidence are aim-only. Cue continuity is tied to one confirmed
selector generation, requires current cue pixels and cannot self-train its
geometry. A cue reconstructed too far from the last reliable same-generation
point loses authority.

An empty fresh detection result clears generic target authority. The native
selector does not emit `predicted`, `projected` or blind reconstructed targets.
Unknown source strings fail closed at downstream authority boundaries.

## Freshness Contract

`frame_updated` means that `VisionEngine::poll_once` processed a new source
frame. `VisionService` exposes only its latest snapshot, and `RuntimeLoop`
admits a result only when `VisionDeliveryGate` sees a unique, increasing and
recent source capture.

Two cases must remain distinct:

- no new source frame: no new selector decision; the controller can retain the
  immutable plan associated with the last accepted source;
- new source frame with no target: release target authority unless current
  same-generation cue evidence explicitly supports aim-only continuation.

The service no longer replays the previous `VisionResult` as a synthetic fresh
sample and does not own a duplicate capture cadence.

## Timing Fields

The compact result exposes current native timing such as:

- capture acquisition/transfer;
- CUDA map/preprocess;
- TensorRT enqueue/GPU/output wait;
- decode;
- selector;
- total post/result age.

There is no `enhance_ms` field because there is no native post-selector
enhancement stage. Controller/output latency is measured separately by the
runtime performance or telemetry facilities.

## Runtime Integration

Default native gamepad path:

```text
cod_native_runtime.exe
  -> VisionEngine / VisionService
  -> VisionDeliveryGate
  -> NativeGamepadController
```

The optional Python-hosted native debug/fallback path loads
`vision_native_cpp` through `vision/native_runner.py` and maps the same compact
result into `ControllerTarget`. Its fallback authority logic uses an explicit
allowlist and does not revive projected source labels.

The pure Python Vision backend still contains older occlusion and
`AimEnhancementPipeline` behavior for fallback-specific use. That is a separate
runtime contract and must not be cited as behavior of native production.

## Configuration

The maintained native example uses:

```toml
[runtime.vision]
capture_width = 640
capture_height = 512
tensor_width = 480
tensor_height = 384
require_isotropic_resize = true
capture_fps = 160
idle_capture_fps = 20
model_path = "models/best_480x384.engine"
gpu_service_enabled = true
```

Tensor dimensions must match the selected engine. Isotropic resize is a hard
geometry boundary. Dynamic viewport remains opt-in until every tier uses a
compatible tensor/model contract.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Direct CMake:

```powershell
cmake -S native\vision_native -B native\vision_native\build
cmake --build native\vision_native\build --config Release --parallel 8
```

Useful binaries are emitted under
`native\vision_native\build\Release`, including:

- `cod_native_runtime.exe`;
- `vision_native_cpp.cp311-win_amd64.pyd`;
- `vision_native_debug.exe`;
- native selector, service, resize and integration test executables.

## Verification Boundary

Current acceptance requires:

- target selector hostile/weak/cue/no-target contracts pass;
- no fresh empty result becomes a predicted target;
- result source/frame identity survives the adapter unchanged;
- fire remains direct-observed only;
- Release build and all native CTest entries pass;
- focused Python native-runner/performance mapping tests pass;
- active live throughput is measured with logging conditions held constant.

See [Current State](CURRENT_STATE.md),
[Native C++ Runtime](NATIVE_CPP_RUNTIME.md) and
[Legacy Control Stack Cleanup](LEGACY_CONTROL_STACK_CLEANUP_20260810.md).
