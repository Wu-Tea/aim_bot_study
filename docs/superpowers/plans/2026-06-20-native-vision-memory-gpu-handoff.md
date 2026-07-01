# Native Vision Memory/GPU Optimization Handoff

Date: 2026-06-20
Workspace: `D:\work\AI\yolo-study-001`
Primary spec: `docs/superpowers/specs/2026-06-18-native-vision-memory-gpu-optimization-design.md`

## Goal

Finish and verify the native C++ vision memory/GPU optimization work.

The optimization is intended to reduce live vision latency and jitter by using persistent buffers and an optional CUDA array texture preprocessing path. It must not change model input size, target authority rules, controller feel, tracker behavior, or recoil behavior.

## Current State Summary

The current workspace contains uncommitted implementation work for:

- CPU-side reserve/high-watermark pooling.
- `preprocess_mode` / `preprocess_fallbacks` telemetry.
- Feature-flagged CUDA array direct preprocessing.
- Native preprocess parity test target.
- Native BGRA benchmark target.

The work is partially implemented and partially verified. It is not ready to be treated as fully accepted or enabled by default.

## Current Dirty Worktree

As of the latest Codex inspection, these files are modified:

```text
M native/controller_native/controller_behavior_tests.cpp
M native/runtime_app/aim_perf_file_logger.cpp
M native/runtime_app/runtime_loop.cpp
M native/vision_native/CMakeLists.txt
M native/vision_native/include/vision_native/preprocess.h
M native/vision_native/include/vision_native/tensorrt_engine.h
M native/vision_native/include/vision_native/types.h
M native/vision_native/src/preprocess.cu
M native/vision_native/src/target_selector.cpp
M native/vision_native/src/tensorrt_engine.cpp
M native/vision_native/src/vision_engine.cpp
?? native/vision_native/src/bgra_benchmark.cpp
?? native/vision_native/src/preprocess_tests.cpp
?? .agent-context/claude-runs/
```

Do not blindly commit `.agent-context/claude-runs/`. Some entries there are incomplete local-executor artifacts.

## Important Constraints

- Do not change `crop_width` or `crop_height`.
- Do not change controller/recoil mixing or feel.
- Do not change tracker/controller/recoil boundaries.
- Do not change target selection authority semantics.
- Do not give cue-only, weak-only, tracker-only, or predicted-only targets fire authority.
- Keep CUDA array preprocessing default-off until live A/B proves it is stable and useful.
- Preserve the old BGRA linear-buffer path as rollback.

Current config is already aligned to the intended live size:

```text
crop_width = 480
crop_height = 416
model_path = "models/candidates/body_union_manual_core_x2_neg_e6_480x416_ws10.engine"
```

## Implemented Items

### Telemetry

Implemented:

- `PreprocessMode` enum in `native/vision_native/include/vision_native/types.h`.
- `preprocess_mode_name(...)`.
- `preprocess_mode` and `preprocess_fallbacks` fields on `DetectionBatch` and `VisionResult`.
- JSONL output fields in `native/runtime_app/aim_perf_file_logger.cpp`.
- Console `[Vision][CPP]` fields in `native/runtime_app/runtime_loop.cpp`.
- Controller test coverage for aim perf JSON fields.

Expected values:

```text
none
rgb_host_copy
old_bgra_copy
cuda_array_texture
```

### CPU Pooling

Implemented:

- `batch.detections.reserve(output_rows_)` before TensorRT output decode in both RGB and BGRA paths.
- `host_color_frame_` in `VisionEngine::poll_once` now grows only when needed instead of resizing exactly every frame.
- Extra debug assertions were added around `ColorFrameView` reads.

Partial / not complete:

- Selector-side scratch pooling was not fully implemented.
- `VisionTargetSelector::annotate_colors` / `select_with_frame` still need a focused allocation/copy audit.
- Do not claim Phase 1 pooling is complete until selector-side copies and hidden allocations are checked.

### CUDA Array Direct Preprocess

Implemented:

- New API:

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

- CUDA texture object path in `native/vision_native/src/preprocess.cu`.
- Feature flag:

```text
VISION_CUDA_ARRAY_PREPROCESS=1
```

- Default remains old path.
- Fallback logic exists for invalid CUDA array descriptor, scaled input, launch failure before TensorRT enqueue, and post-submit runtime disable.
- `preprocess_fallbacks` counter is reported.

Current limitation:

- CUDA array path only supports no-scale input.
- If `width != input_width` or `height != input_height`, it falls back to `old_bgra_copy`.
- That is intentional for this phase.

## Verification Already Run

Codex ran these on 2026-06-19 / 2026-06-20 local time:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_core
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target cod_native_runtime
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_preprocess_tests
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_bgra_benchmark

native\vision_native\build\Release\vision_native_preprocess_tests.exe
scripts\verify\native_pipeline_contract.bat
```

Observed result:

```text
vision_native_preprocess_tests.exe: PASS
scripts\verify\native_pipeline_contract.bat: PASS
```

The build targets also completed successfully.

## Benchmark Evidence

Acceptance benchmark artifacts:

```text
runs/native_memory_opt/bench_bgra_old_20260619_acceptance.json
runs/native_memory_opt/bench_bgra_array_20260619_acceptance.json
```

Old path result:

```text
preprocess_mode = old_bgra_copy
preprocess_fallbacks = 0
preprocess_ms p50/p95/p99 = 0.040 / 0.233 / 0.625 ms
infer_ms      p50/p95/p99 = 3.304 / 5.465 / 15.475 ms
gpu_total_ms  p50/p95/p99 = 3.381 / 5.579 / 15.534 ms
```

CUDA array path result:

```text
preprocess_mode = cuda_array_texture
preprocess_fallbacks = 0
preprocess_ms p50/p95/p99 = 0.049 / 0.261 / 0.687 ms
infer_ms      p50/p95/p99 = 3.198 / 7.651 / 9.939 ms
gpu_total_ms  p50/p95/p99 = 3.279 / 7.719 / 9.990 ms
```

Interpretation:

- The CUDA array path can run.
- Fallback count is 0 in the benchmark.
- This benchmark does not prove the CUDA array path is faster. In that run, `gpu_total_ms` p95 was worse for the array path.
- Repeat old/new benchmark for more runs before drawing a performance conclusion.

## Live Log Evidence

Latest effective live log inspected:

```text
runs/native_perf/native_aim_perf_20260619_224444_385.jsonl
```

Result:

```text
preprocess_mode = old_bgra_copy for all steady rows
preprocess_fallbacks = 0
```

Interpretation:

- The latest live runtime did not run the CUDA array path.
- This is not a fallback case. The flag was likely not set in the process environment.
- No live `cuda_array_texture` A/B evidence exists yet.

## Remaining Work

### 1. Prove CUDA Array Path in Live Runtime

Run live with:

```powershell
$env:VISION_CUDA_ARRAY_PREPROCESS='1'
scripts\launch\gamepad_start.bat
```

Then inspect the new JSONL:

```text
preprocess_mode should be cuda_array_texture
preprocess_fallbacks should be 0
```

If the log still shows `old_bgra_copy`, inspect launch scripts and environment propagation into `cod_native_runtime.exe`.

### 2. Run Live Old / Array / Rollback A/B

Run three separate process launches:

```text
1. VISION_CUDA_ARRAY_PREPROCESS=0, record old live log.
2. VISION_CUDA_ARRAY_PREPROCESS=1, record array live log.
3. VISION_CUDA_ARRAY_PREPROCESS=0, restart and record rollback old log.
```

Compare:

- `preprocess_mode`
- `preprocess_fallbacks`
- `capture_ms`
- `copy_ms` / `capture_transfer_ms`
- `cuda_map_ms`
- `preprocess_ms`
- `infer_ms`
- `gpu_total_ms`
- `output_wait_ms`
- `age_ms`
- `out_age_ms`
- `ctrl_loop_ms`
- `target`
- `source`
- `tier`
- `aim_authority`
- `fire_authority`

Acceptance:

- Array live run must actually report `cuda_array_texture`.
- `preprocess_fallbacks == 0`.
- `age_ms` p95 must not regress.
- `gpu_total_ms` p95 should improve or stay flat.
- No authority/source/tier distribution drift under comparable scenes.
- Rollback run must return to `old_bgra_copy`.

### 3. Repeat BGRA Benchmark

Repeat 3-5 runs with more iterations:

```powershell
$env:VISION_CUDA_ARRAY_PREPROCESS='0'
native\vision_native\build\Release\vision_native_bgra_benchmark.exe --model models\candidates\body_union_manual_core_x2_neg_e6_480x416_ws10.engine --output-json runs\native_memory_opt\bench_bgra_old_<run>.json --iterations 1000 --warmup 50

$env:VISION_CUDA_ARRAY_PREPROCESS='1'
native\vision_native\build\Release\vision_native_bgra_benchmark.exe --model models\candidates\body_union_manual_core_x2_neg_e6_480x416_ws10.engine --output-json runs\native_memory_opt\bench_bgra_array_<run>.json --iterations 1000 --warmup 50
```

If array p95 remains worse, keep feature flag default-off and do not enable by default.

### 4. Complete Selector-Side Pooling Audit

Inspect and, where useful, improve:

- `VisionTargetSelector::annotate_colors`
- `VisionTargetSelector::select_with_frame`
- result construction / vector copies
- hidden per-frame allocations

Potential implementation direction:

- Add selector-owned scratch vectors only if they remove real allocations.
- Use high-watermark storage.
- Clear by size; do not shrink during hot path.
- Preserve output behavior and authority rules exactly.

### 5. Add Scaled Fallback Test

Current preprocess parity test covers no-scale `480x416 -> 480x416`.

Missing test:

```text
width != input_width or height != input_height should not use cuda_array_texture.
It should fall back to old_bgra_copy and increment preprocess_fallbacks.
```

This may be easiest at `TensorRTEngine::infer_bgra_array` / benchmark level rather than the low-level kernel API, because the low-level API intentionally throws on scaled inputs.

### 6. Decide Commit Boundary

Recommended commits:

1. CPU reserve/high-watermark pooling + telemetry + tests/benchmark targets.
2. Feature-flagged CUDA array direct preprocess.

Do not include:

- transient perf logs unless explicitly wanted as artifacts.
- incomplete `.agent-context/claude-runs/` records.
- generated benchmark JSON unless the project intentionally tracks benchmark artifacts.

## Commands To Run Before Commit

Run these fresh before claiming completion:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_core
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target cod_native_runtime
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_preprocess_tests
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\vision_native\build --config Release --target vision_native_bgra_benchmark

native\vision_native\build\Release\vision_native_preprocess_tests.exe
scripts\verify\native_pipeline_contract.bat
```

After live testing, add a short report with:

- files changed
- commands run
- benchmark JSON paths
- live JSONL paths
- old vs array p50/p95/p99
- whether array path should remain default-off or be promoted later

## Acceptance Decision

Current decision:

```text
Do not default-enable CUDA array preprocessing yet.
```

Reason:

- It can run in the synthetic BGRA benchmark.
- It has not been observed in live runtime.
- The one available benchmark run had worse array p95 than old path.

Safe near-term outcome:

- Commit the feature flag, telemetry, tests, and benchmark if they remain clean.
- Keep runtime default on `old_bgra_copy`.
- Use `VISION_CUDA_ARRAY_PREPROCESS=1` only for explicit A/B testing until live logs prove it helps.
