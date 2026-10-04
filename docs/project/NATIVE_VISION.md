> 当前 dev 合并约定：runtime、Vision 和构建走 C++；GUI 沿用 dev 的 Python/Tk 配置工作室。新 C++ GUI 未合入。本文原生助手相关旧检查不作为当前要求，界面操作见 [桌面助手](DESKTOP_ASSISTANT.md)。

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

The retained Python/Tk desktop workspace launches the native process directly. Python-hosted Vision bridges and fallback vision paths are retired; no interpreter is used inside the native Vision/control chain.

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
powershell -ExecutionPolicy Bypass -File tools\build_native_runtime.ps1
```

Direct CMake:

```powershell
cmake -S native -B native\build
cmake --build native\build --config Release --parallel 8
```

Useful binaries are emitted under
`native\build\Release`, including:

- `cod_native_runtime.exe`;
- GUI 仍由 `启动助手.vbs` 启动 Python/Tk 配置工作室；
- `vision_native_debug.exe`;
- native selector, service, resize and integration test executables.

## Verification Boundary

Current acceptance requires:

- target selector hostile/weak/cue/no-target contracts pass;
- no fresh empty result becomes a predicted target;
- result source/frame identity survives the adapter unchanged;
- fire remains direct-observed only;
- Release build and all native CTest entries pass;
- retained dev GUI configuration/workspace/IPC checks pass;
- active live throughput is measured with logging conditions held constant.

See [Current State](CURRENT_STATE.md),
[Native C++ Runtime](NATIVE_CPP_RUNTIME.md) and
[Legacy Control Stack Cleanup](LEGACY_CONTROL_STACK_CLEANUP_20260810.md).
