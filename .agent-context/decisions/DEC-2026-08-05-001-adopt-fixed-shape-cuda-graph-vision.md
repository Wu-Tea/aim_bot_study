# DEC-2026-08-05-001: Adopt Fixed-Shape CUDA Graph Vision Inference

Status: accepted
Date: 2026-08-05
Confirmed by: user requested recording and committing the optimization after reviewing the controlled benchmark and real 160 FPS A/B evidence
Related sessions: offline Vision dataset A/B; live sessions `20260805T120935Z_47940_1` and `20260805T125610Z_44588_1`
Related files:

- `native/vision_native/include/vision_native/tensorrt_engine.h`
- `native/vision_native/src/tensorrt_engine.cpp`
- `native/vision_native/include/vision_native/types.h`
- `native/vision_native/src/vision_engine.cpp`
- `native/runtime_app/runtime_loop.cpp`
- `native/vision_native/src/bgra_benchmark.cpp`
- `native/vision_native/src/vision_native_module.cpp`
- `tools/benchmark_vision_dataset.py`

Supersedes: none
Superseded by: none
Related: `DEC-2026-06-20-001-reject-cuda-array-preprocess.md`

## Context

The shared RTX 4070 Super could show high GPU-engine utilization while game plus
YOLO board power remained well below its nominal limit. The pre-change hot path
repeated fixed TensorRT address binding and paid a multi-millisecond CPU enqueue
cost on each fixed-shape inference. A dedicated RTX 4060 was under consideration,
but software overhead needed to be isolated before adding hardware.

The existing DXGI CUDA-array preprocess decision remains valid: dynamic capture,
viewport selection and ROI preprocess are not captured into the Graph. Only the
fixed-address, fixed-shape TensorRT enqueue is stable enough for replay.

## Decision

- Bind the TensorRT input and output addresses once when their device allocations are created.
- Run Vision work on a high-priority non-blocking CUDA stream on the target runtime.
- Warm up TensorRT, capture only the fixed TensorRT `enqueueV3` segment into a CUDA Graph, instantiate it once and replay it for subsequent inference.
- Keep dynamic DXGI capture, CUDA-array ROI preprocess, output transfer/wait, decode, selector and controller work outside the Graph.
- Enable binding-once, high-priority stream and CUDA Graph by default in production, while retaining explicit benchmark switches for legacy A/B.
- Expose `enqueue_cpu_ms` through native result structures, runtime logs, Python bindings and dataset summaries.
- Fail fast if the default Graph cannot be initialized on the target runtime rather than silently changing the measured execution contract.

## Reasons

- Fixed allocations and fixed TensorRT shapes satisfy the stable-address contract required for Graph replay.
- Capturing only the invariant inference segment avoids coupling Graph lifetime to dynamic capture/ROI state.
- Repeated launch and address-binding overhead was directly measurable and materially larger than the optimized submission cost.
- Explicit benchmark switches preserve causal comparison and make future regressions diagnosable.
- A fail-fast startup preserves runtime identity; an unnoticed fallback could invalidate latency claims and live comparisons.

## Rejected Alternatives

### Capture the Entire Capture/Preprocess/Inference/Postprocess Pipeline

Rejected because DXGI frames, CUDA-array resources, viewport/ROI state and CPU
decode/selection are dynamic and do not share one safe fixed Graph contract.

### Retain Per-Inference Tensor Address Binding

Rejected for the fixed production allocations because it repeats invariant work
and benchmark evidence showed no output benefit.

### Use the Legacy Default Stream

Rejected for production because the target workload shares the GPU with a game;
the high-priority non-blocking stream produced the intended scheduling contract
without increasing matched GPU power. The legacy stream remains available only
as a benchmark control.

### Add a Secondary GPU Before Removing Software Overhead

Deferred rather than rejected as a hardware option. The real A/B shows that the
shared 4070 Super can materially improve Vision throughput at unchanged power;
a secondary card should now be justified by isolation and tail-latency needs,
not by unmeasured launch overhead.

## Evidence

- Offline 1,500-image A/B: wall P50/P95 `3.343/6.078 -> 1.207/2.432 ms`; enqueue CPU P50 `2.894 -> 0.083 ms`; GPU-total P50/P95 `3.135/5.856 -> 1.015/2.213 ms`.
- Accuracy and output identity: detection totals and TP/FP/FN were unchanged; 500 real images had maximum float difference `0`.
- Real 160 FPS A/B: capture-to-result P50/P95 `8.08/13.41 -> 5.42/9.34 ms`; active effective result rate `99.2 -> 127.8 Hz`; 6.25 ms capture-to-result compliance about `21.6% -> 70.0%`.
- End-to-end effect: source-present-to-ViGEm P50/P95 `13.05/20.63 -> 8.57/16.68 ms`; publish-to-controller timing remained essentially unchanged.
- Matched stable hardware window: 4070S core utilization/power `77.54%/84.94 W -> 77.26%/83.77 W`; power and thermal limiting stayed inactive.
- Runtime candidate SHA-256: `57A78F843A7CDB4AA474B7F6968B7B83C7DAFD15101B1210272800692E43AC25`; pre-change backup SHA-256 begins `69E8624C`.

## Consequences

- The installed validated binary and its full SHA-256 identify the live-tested candidate even though its embedded build-time Git identity predates the source-protection commit.
- Fixed-shape inference is substantially faster, but the live result stream is not claimed to be stable 160 Hz; active mean was about 128 Hz and tails remain.
- Benchmark and production paths now share default Graph/binding/priority behavior, with explicit switches required to reproduce the legacy baseline.
- TensorRT engine shape, device allocation lifetime, driver/runtime version and Graph-capture compatibility are part of the production contract.
- A dedicated secondary GPU remains optional for workload isolation, tail latency or larger future models rather than a prerequisite for current YOLO operation.

## Review Triggers

- TensorRT engine input/output shapes, tensor addresses or allocation lifetime change.
- CUDA/TensorRT/driver updates cause Graph initialization or replay failure.
- Dynamic batching or multiple simultaneous execution contexts are introduced.
- Exact-output comparison, live accuracy or end-to-end latency regresses.
- The target machine requires a supported non-Graph fallback instead of the current fail-fast contract.
