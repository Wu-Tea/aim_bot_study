# Native Vision Memory and GPU Optimization Design

**Date:** 2026-06-18
**Status:** Reviewed draft
**Owner:** Codex
**Review:** Incorporated local Claude Code and gpt-5.3-codex-spark feedback on 2026-06-18.

## Goal

Reduce native C++ vision hot-path latency and jitter by spending additional CPU memory and GPU memory on persistent buffers, object reuse, and a lower-copy CUDA preprocessing path.

The work must not change the model input size, target authority rules, controller behavior, tracker behavior, or recoil behavior.

## Context

The live gamepad path is now native C++ end to end:

```text
DXGI ROI capture
  -> CUDA preprocess
  -> TensorRT inference
  -> native target selector
  -> tracker
  -> controller
  -> recoil
  -> ViGEm output
```

Current live vision config uses a 480x416 engine. A TensorRT workspace A/B run on 2026-06-18 produced a better candidate engine:

```text
models/candidates/body_union_manual_core_x2_neg_e6_480x416_ws10.engine
```

Offline benchmark over 700 validation images showed:

| Engine | infer p50 | infer p95 | gpu p50 | gpu p95 | F1 |
|---|---:|---:|---:|---:|---:|
| current live | 1.463ms | 2.731ms | 1.582ms | 2.895ms | 0.515 |
| ws10 | 1.336ms | 1.900ms | 1.444ms | 2.005ms | 0.519 |

This design builds on that result without changing image dimensions.

## Non-Goals

- Do not change `crop_width` or `crop_height`.
- Do not change target selection semantics or authority tiers.
- Do not give cue-only, weak-only, predicted-only, or tracker-only targets fire authority.
- Do not change controller/recoil mixing, recoil profile playback, or tracker/controller/recoil boundaries.
- Do not introduce multi-frame GPU pipelining in this first pass. That can change frame-consumption timing and must be designed separately.
- Do not remove the existing CPU RGB inference path or the current BGRA linear-buffer fallback path.

## Current Hot-Path Observations

Current BGRA path in `TensorRTEngine::infer_bgra_array`:

```text
cudaArray_t frame_bgra
  -> cudaMemcpy2DFromArrayAsync(..., device_frame_)
  -> launch_bgra_hwc_to_chw_float(device_frame_, ..., device_input_)
  -> TensorRT enqueueV3
  -> cudaMemcpyAsync(device_output_, host_output_)
  -> cudaStreamSynchronize
  -> CPU decode
```

Current selector color path in `VisionEngine::poll_once`:

```text
selector.required_color_region(batch)
  -> host_color_frame_.resize(region_width * region_height * 4)
  -> cudaMemcpy2DFromArray(..., host_color_frame_.data())
  -> selector.select_with_frame(...)
```

Current likely sources of avoidable latency or jitter:

- Device-to-device copy from CUDA array into `device_frame_` before preprocessing.
- Per-frame vector growth in detection containers.
- Per-frame vector copies in selector-side color annotation paths.
- Per-frame resizing of `host_color_frame_` when color regions vary.
- Synchronous color copy and output copy are still serialized in the vision polling call.
- Detailed JSONL logging can add CPU jitter when enabled, though logging optimization is a separate optional phase.

## Design Overview

Use a two-stage rollout:

1. **CPU pooling and fixed-capacity reuse:** reduce per-frame allocations and CPU-side jitter while keeping the GPU path unchanged.
2. **CUDA array direct preprocess:** add a feature-flagged preprocessing path that samples from the mapped `cudaArray_t` through a CUDA texture object and writes directly to TensorRT input, skipping the intermediate `device_frame_` copy.

The old path remains available at runtime until benchmark and live logs prove the new path is stable.

## Phase 1: CPU Pooling

### Detection Container Reserve

Modify `DetectionBatch` and `VisionResult` usage so hot-path detection vectors reserve stable capacity before decode or assignment.

The TensorRT output is shaped as `(1, 300, 6)` for the current engine, so reserve 300 detections where detection vectors are populated.

Implementation expectations:

- Reserve `batch.detections` before TensorRT output decode in both `infer_rgb` and `infer_bgra_array`.
- Reserve `result.detections` before assigning or moving selector detections when practical.
- Audit `VisionTargetSelector::annotate_colors`, `select_with_frame`, and related result construction for hidden full-batch copies or repeated allocations.
- If selector-side scratch vectors are added, keep them as explicit per-selector scratch storage with a high-watermark capacity and clear them by size, not by deallocation.
- Do not cap detections below TensorRT output count unless the existing confidence threshold already does so.
- Keep detection reserve and selector scratch changes in a separate commit from CUDA array preprocessing so rollback is simple. Do not add a runtime env knob for pure `reserve`/high-watermark changes unless the implementation changes ownership or observable behavior.

### Host Color Frame Pool

Replace per-region resizing with a high-watermark buffer.

Implementation expectations:

- Keep `host_color_frame_` as a persistent vector.
- Grow it only when the requested color region exceeds current capacity.
- Do not shrink it during runtime.
- Pass the current region width, height, stride, and origin separately through `ColorFrameView`.
- Keep pixel format and color-space behavior unchanged.
- Treat `ColorFrameView` metadata as the only valid bounds for consumers. Consumers must not infer valid pixel extent from `host_color_frame_.size()` because stale tail bytes can exist beyond the current region.
- Keep `row_pitch = region_width * 4`, `width = region_width`, and `height = region_height` for the copied region. These are the copied extent, not the full frame extent.
- Preserve `origin_x`, `origin_y`, `frame_width`, and `frame_height` so existing `frame_covers` and `read_rgb` checks stay bounded to the current region.
- Add debug assertions or diagnostic counters if a color read falls outside `ColorFrameView` bounds.

The relevant view contract is:

```cpp
struct ColorFrameView {
    const std::uint8_t* data;
    int width;        // valid copied region width, not full frame width
    int height;       // valid copied region height, not full frame height
    int row_pitch;    // bytes per copied row, normally width * 4
    int origin_x;     // copied region origin in the full ROI frame
    int origin_y;     // copied region origin in the full ROI frame
    int frame_width;  // full ROI frame width
    int frame_height; // full ROI frame height
    PixelFormat format;
};
```

### Logging Allocation

Do not change logging in Phase 1 unless a benchmark shows logging is the active p95 source.

If live logs show logging jitter, add a separate logging ring-buffer design. Keep JSON schema unchanged.

## Phase 2: CUDA Array Direct Preprocess

### Rationale

The live BGRA path already maps a D3D11 ROI texture as a CUDA graphics resource and obtains a `cudaArray_t`. The current implementation copies that array into a linear device buffer, then preprocesses from the linear buffer.

Direct array preprocessing removes this middle copy:

```text
Old:
cudaArray_t -> device_frame_ -> device_input_

New:
cudaArray_t -> texture object -> device_input_
```

### API Additions

Add a new launch API in `native/vision_native/include/vision_native/preprocess.h`:

```cpp
void launch_bgra_array_to_chw_float(
    cudaArray_t frame_bgra,
    int width,
    int height,
    int input_width,
    int input_height,
    float* output_chw,
    cudaStream_t stream);
```

Keep existing APIs:

```cpp
void launch_rgb_hwc_to_chw_float(...);
void launch_bgra_hwc_to_chw_float(...);
```

### Kernel Behavior

The new kernel must initially match the old BGRA preprocessing behavior:

- Same resize mapping.
- Same channel order.
- Same normalization.
- Same coordinate origin.
- Same output tensor layout.

Use a CUDA texture object created from the mapped `cudaArray_t`.

Initial rollout is limited to the live no-scale case:

```text
frame width  == TensorRT input width
frame height == TensorRT input height
```

For this case, match `bgra_hwc_to_chw_float_direct_kernel` exactly:

- Texture coordinates are integer pixel coordinates.
- Output channel order is R, G, B from BGRA source bytes: `bgra.z`, `bgra.y`, `bgra.x`.
- Normalization is exactly `float(byte_value) / 255.0f`.
- `normalizedCoords = 0`.
- `filterMode = cudaFilterModePoint`.
- `readMode = cudaReadModeElementType`.
- Address mode is clamp on both axes.

If source size differs from TensorRT input size, the first implementation must fall back to the old linear-buffer path unless a separate parity test proves a texture-based scaled path matches `sample_hwc_channel_bilinear` within the agreed tolerance. Do not silently switch scaled frames to hardware linear filtering.

Implementation constraints:

- Validate the mapped array format with `cudaArrayGetInfo` before creating the texture object. Expected format is four unsigned 8-bit channels compatible with BGRA8 source data.
- Wrap the texture object in a short-lived RAII helper such as `CudaTextureGuard` whose destructor calls `cudaDestroyTextureObject`.
- Destroy the texture object before `cudaGraphicsUnmapResources` is called by `VisionEngine::poll_once`.
- Do not store the texture object beyond the map/unmap scope in `VisionEngine::poll_once`.
- Do not hold the mapped D3D resource longer than the current old path.
- Use the same CUDA stream as the rest of preprocessing and TensorRT enqueue.
- After launching `launch_bgra_array_to_chw_float`, call `cudaGetLastError()` just like the current `launch_bgra_hwc_to_chw_float` path.
- Ensure all stream work that reads `frame_array` completes before the mapped resource is unmapped. The current `infer_bgra_array` ends with `cudaStreamSynchronize`; if that synchronization is removed in a future pipeline, replace it with an explicit event/synchronization dependency before unmap.
- Keep `device_frame_` for `infer_rgb` and for fallback BGRA preprocessing.

### Runtime Error Recovery

The feature-flagged CUDA array path must fail closed:

- If array descriptor validation or texture object creation fails before any kernel launch, log a warning, increment a fallback counter, and run the old BGRA linear-buffer path for that frame.
- If the new preprocess kernel launch fails before TensorRT enqueue, log a warning, increment a fallback counter, and run the old BGRA linear-buffer path for that frame if the CUDA stream is still usable.
- If synchronization or TensorRT enqueue reports an error after GPU work has been submitted, do not hide the error as a normal detection miss. Disable the CUDA array path for the remaining process and surface the error through existing runtime diagnostics so the live run can be stopped or restarted.
- Fallback must not grant target or fire authority by itself. A fallback frame goes through the same selector and authority logic as any old-path frame.

### Runtime Flag

Add a runtime flag for A/B testing:

```text
VISION_CUDA_ARRAY_PREPROCESS=1
```

Default behavior for rollout:

- Initially default to old path.
- Enable new path only when the environment variable is set.
- After dataset benchmark and live logs prove stability, flip the default only in a separate explicit rollout commit.

This flag must not affect CPU RGB inference.

This flag controls only Phase 2. CPU pooling from Phase 1 is independent and should be rolled back by reverting the isolated pooling commit unless a future implementation introduces ownership changes that require a separate flag.

Invalid values should be logged at startup and treated as disabled.

### Telemetry

Add a preprocess mode marker to runtime and benchmark outputs:

```text
preprocess_mode = "old_bgra_copy" | "cuda_array_texture" | "rgb_host_copy"
preprocess_fallbacks = <count>
```

Record `preprocess_mode` in:

- `[Vision][CPP]` startup or per-frame diagnostics when practical.
- Aim perf JSONL rows.
- Native benchmark JSON summaries for the new BGRA preprocess benchmark.

This marker is required for post-hoc analysis of live logs. A timing regression cannot be debugged if the log does not show which path produced each frame.

### Interference Risk

The new path should not increase interference with game rendering because it still maps the same copied ROI texture, not the game backbuffer. It removes one CUDA-side copy after mapping.

The risk to watch is not texture sampling itself; it is:

- Longer `cudaGraphicsMapResources` / `cudaGraphicsUnmapResources` time.
- Longer preprocess kernel time.
- Extra synchronization.
- Holding the mapped resource longer than the old path.

The implementation must preserve current map/unmap scope and avoid new stream synchronizations.

## Phase 3: Optional Logging Pool

Only do this if live perf logs show CPU-side logging jitter.

Design:

- Reuse a per-thread string buffer or fixed-size log event struct.
- Write high-frequency log entries into a memory ring.
- Flush on a background thread every fixed byte threshold or time interval.
- Flush synchronously on shutdown.
- Preserve JSONL fields and file naming.

This phase is intentionally separate from vision preprocessing so it can be reverted independently.

## Validation Plan

### Build Validation

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_core
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target cod_native_runtime
```

### Preprocess Parity Test

Do not use `tools\benchmark_vision_dataset.py` as the CUDA array A/B test until it is extended. Its current native path calls `engine.infer_rgb(...)`, so it does not exercise `TensorRTEngine::infer_bgra_array` or the `VISION_CUDA_ARRAY_PREPROCESS` flag.

Add a focused native parity test target, for example:

```text
vision_native_preprocess_tests.exe
```

The test target must:

- Build deterministic BGRA fixtures: gradients, checkerboards, solid colors, thin high-contrast lines, and random seeded frames.
- Upload each fixture to both a linear device buffer and a `cudaArray_t`.
- Run the old BGRA linear preprocess and the new CUDA array texture preprocess into separate device input tensors.
- Copy both tensors back to host and compare all channels.
- Verify current live no-scale input `480x416 -> 480x416` has exact output parity or a max absolute error of at most `1.0f / 255.0f` if CUDA texture element conversion introduces a documented one-ULP difference.
- Verify scaled inputs fall back to old path unless a separate scaled parity implementation is added.
- Assert `preprocess_mode` is reported as expected for both old and new paths.

This test is the first gate for CUDA array direct preprocess. Dataset F1 is not sensitive enough to catch subtle pixel mapping errors near detection thresholds.

### BGRA Array Benchmark

Add or extend a benchmark that explicitly exercises `infer_bgra_array` using a CUDA array source. Acceptable approaches:

- A native executable that creates a `cudaArray_t` from fixture images and calls `TensorRTEngine::infer_bgra_array`.
- A Python-accessible test hook that uploads host BGRA fixtures into a CUDA array and calls the same native `infer_bgra_array` path.
- A live DXGI benchmark mode using `VisionEngine::poll_once`, if deterministic enough for timing comparison.

The benchmark output must include:

- `preprocess_mode`.
- `preprocess_fallbacks`.
- `preprocess_ms` p50/p95/p99.
- `gpu_total_ms` p50/p95/p99.
- `infer_ms` p50/p95/p99.
- Detection count distribution.

Run old BGRA copy path and new CUDA array texture path:

```powershell
$env:VISION_CUDA_ARRAY_PREPROCESS='0'
native\vision_native\build\Release\vision_native_bgra_benchmark.exe --model models\candidates\body_union_manual_core_x2_neg_e6_480x416_ws10.engine --fixtures runs\native_memory_opt\fixtures --output-json runs\native_memory_opt\bench_bgra_old.json

$env:VISION_CUDA_ARRAY_PREPROCESS='1'
native\vision_native\build\Release\vision_native_bgra_benchmark.exe --model models\candidates\body_union_manual_core_x2_neg_e6_480x416_ws10.engine --fixtures runs\native_memory_opt\fixtures --output-json runs\native_memory_opt\bench_bgra_array.json
```

Expected:

- Detection counts are unchanged for deterministic fixtures.
- `preprocess_fallbacks == 0` for the no-scale live case.
- `preprocess_ms` or `gpu_total_ms` p95 improves or stays flat.
- No new CUDA errors.

### RGB Dataset Benchmark

Keep the existing dataset benchmark as a separate engine and selector sanity check, not as proof of CUDA array preprocessing:

```powershell
python tools\benchmark_vision_dataset.py --model models\candidates\body_union_manual_core_x2_neg_e6_480x416_ws10.engine --datasets models\train D:\datasets\roboflow_candidates --split valid --max-images 500 --crop-width 480 --crop-height 416 --warmup 20 --output-json runs/native_memory_opt/bench_rgb_dataset.json
```

Compare:

- Precision, recall, F1.
- Detection counts.
- `infer_ms` p50/p95.
- `gpu_total_ms` p50/p95.

Expected:

- CPU pooling changes must not shift F1 or detection counts beyond normal run-to-run variance.
- CUDA array preprocessing changes are not validated by this benchmark unless the benchmark is explicitly extended to call `infer_bgra_array`.

### Live Runtime Validation

Run live with detailed perf log and ws10 engine:

```text
scripts\launch\gamepad_start.bat
```

Capture at least one aim-state log for old path and one for new path.

Compare:

- `cuda_map_ms`
- `cuda_unmap_ms`
- `preprocess_ms`
- `gpu_total_ms`
- `output_wait_ms`
- `age_ms`
- target count and source/tier distribution
- `preprocess_mode`
- `preprocess_fallbacks`
- aim-authority and fire-authority rates

Acceptance:

- `age_ms` p95 must not regress.
- `gpu_total_ms` p95 should improve or stay flat.
- No increase in `cuda_map_ms` / `cuda_unmap_ms`.
- No material drift in `target_source` / `target_tier` distribution under comparable scenes.
- No material drift in `aim_authority == true` or `fire_authority == true` rates under comparable scenes.
- Tracker-only, cue-only, weak-only, and projected-only frames must not gain fire authority in either mode.
- No controller/recoil behavior changes.

Run a rollback drill across process restarts:

```text
1. Start runtime with VISION_CUDA_ARRAY_PREPROCESS=0 and record a short log.
2. Restart runtime with VISION_CUDA_ARRAY_PREPROCESS=1 and record a short log.
3. Restart runtime with VISION_CUDA_ARRAY_PREPROCESS=0 and record a short log.
```

The final off run must report `preprocess_mode="old_bgra_copy"` and metrics consistent with the first off run. This catches mode stickiness and logging ambiguity.

### Native Contract

Run after any C++ changes:

```powershell
scripts\verify\native_pipeline_contract.bat
```

The contract must pass before live testing.

## Rollback Plan

- CPU pooling changes should be behavior-preserving. Keep detection reserve, selector scratch pooling, and host color frame high-watermark changes in small commits so any regression can be reverted without touching CUDA preprocessing.
- CUDA array direct preprocess remains feature-flagged. Set:

```powershell
$env:VISION_CUDA_ARRAY_PREPROCESS='0'
```

to force the old path.

- Keep `device_frame_` and existing BGRA linear-buffer preprocess until the new path has passed live validation.
- Keep `preprocess_mode` in logs while the feature exists so live captures can prove which path ran.
- If `preprocess_fallbacks` is nonzero in live logs, treat the new path as not production-ready and keep the flag disabled by default.

## Implementation Sequence

1. Commit baseline notes and benchmark artifacts if useful.
2. Implement detection vector reserve and host color frame high-watermark pool.
3. Audit selector-side allocation/copy paths and add scratch pooling only where it removes real per-frame allocation.
4. Run RGB dataset benchmark and native pipeline contract.
5. Add `preprocess_mode` / `preprocess_fallbacks` telemetry.
6. Implement native preprocess parity tests for old BGRA linear path vs CUDA array texture path.
7. Implement feature-flagged CUDA array direct preprocess for the no-scale live case only.
8. Run preprocess parity tests.
9. Implement or extend a BGRA array benchmark that calls `infer_bgra_array`.
10. Run old/new BGRA benchmark A/B.
11. Run native pipeline contract.
12. Run live perf log A/B and rollback drill.
13. Decide whether to make CUDA array preprocess default in a separate rollout commit.

## Review Questions

Reviewers should focus on:

- Whether direct `cudaArray_t` texture sampling can exactly match old BGRA preprocessing.
- Whether texture object lifetime and D3D/CUDA map lifetime are safe.
- Whether any proposed pooling risks stale detections or stale color pixels.
- Whether the feature flag and fallback are sufficient for live rollback.
- Whether validation metrics are strong enough to catch rendering interference or aim-authority regressions.
