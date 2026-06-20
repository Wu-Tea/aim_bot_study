# DEC-2026-06-20-001: Reject CUDA Array Preprocess

Status: accepted
Date: 2026-06-20
Confirmed by: user request on 2026-06-20
Related sessions:
- 2026-06-20T09:35:20+08:00
Related files:
- `native/vision_native/src/tensorrt_engine.cpp`
- `native/vision_native/src/preprocess.cu`
- `native/vision_native/src/bgra_benchmark.cpp`
- `runs/native_perf/native_aim_perf_20260620_085804_087.jsonl`
- `runs/native_perf/native_aim_perf_20260620_093520_627.jsonl`
Supersedes: none
Superseded by: none

## Context

Native vision memory/GPU optimization work added an experimental CUDA array texture preprocessing path intended to avoid an intermediate BGRA array-to-linear copy. The path was guarded during implementation and compared against the existing BGRA copy path in live runtime logs.

## Decision

Do not keep or ship the CUDA array texture preprocessing path in the current native vision change set. Keep the existing BGRA copy path as the live runtime path.

Keep the non-array parts of the work that improve observability and stability, including preprocess mode telemetry, persistent/reserved buffers, selector scratch reuse, and BGRA benchmark infrastructure.

## Reasons

- Live A/B evidence did not show a latency win for the CUDA array texture path.
- The array path added code surface and configuration complexity without improving the hot path.
- The existing BGRA copy path remains simpler, already exercised by live runtime, and is the safer rollback/default path.

## Rejected Alternatives

- Keep `cuda_array_preprocess` as a disabled config flag: rejected because the live A/B result was worse and the extra runtime option would invite more hidden operational complexity.
- Enable CUDA array preprocessing by default: rejected because p95 GPU and end-to-end age metrics regressed.
- Keep the CUDA texture kernel only for future experiments: rejected for this change set; future experiments should start from a fresh, separately justified design.

## Evidence

- Old path live log: `native_aim_perf_20260620_085804_087.jsonl` reported `preprocess_mode=old_bgra_copy`.
- CUDA array live log: `native_aim_perf_20260620_093520_627.jsonl` reported `preprocess_mode=cuda_array_texture` with zero fallbacks, proving the experimental path did run.
- Stable updated-frame p95 comparison after dropping the first 10 seconds:
  - `preprocess_ms`: old `2.071`, array `2.259` (+9.1%).
  - `infer_ms`: old `5.749`, array `6.661` (+15.9%).
  - `gpu_total_ms`: old `6.204`, array `7.427` (+19.7%).
  - `output_wait_ms`: old `4.559`, array `5.247` (+15.1%).
  - `age_ms`: old `7.819`, array `8.417` (+7.6%).
  - `out_age_ms`: old `8.854`, array `9.690` (+9.5%).
- User explicitly requested deleting the array change and recording this decision.

## Consequences

- Live runtime has no CUDA array preprocess config switch in this change set.
- `preprocess_mode` remains useful for distinguishing `rgb_host_copy`, `old_bgra_copy`, and `none`.
- Future GPU preprocess experiments should require a new design and a live A/B acceptance threshold before introducing runtime switches.

## Review Triggers

- New profiling shows the old BGRA copy path is the dominant live bottleneck under comparable scenes.
- A future CUDA preprocess design demonstrates p95 `gpu_total_ms`, `age_ms`, and `out_age_ms` improvements in old/new/rollback live A/B.
- Runtime capture/input architecture changes remove or substantially alter the current D3D11 `cudaArray_t` handoff.
