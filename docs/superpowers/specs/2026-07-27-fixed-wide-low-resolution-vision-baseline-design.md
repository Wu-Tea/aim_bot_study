# Fixed Wide Low-Resolution Vision Baseline Design

Date: 2026-07-27
Status: V0 complete; same-weight `wide320` rejected, fixed-scale follow-up selected
Candidate: fixed `640x512` physical crop resized to a `320x256` TensorRT input
Fallback: predictive dynamic viewport M0

## 1. What this is

Production currently captures one centered physical `480x416` rectangle and
feeds a matching `480x416` TensorRT engine. Close targets can leave that small
physical field before the detector and controller can retain them.

This candidate keeps a centered, fixed viewport, but separates physical capture
size from detector input size:

```text
centered physical 640x512 capture
    -> uniform 2x downscale
    -> 320x256 TensorRT
    -> existing detector/selector/tracker/controller
```

The candidate is evaluated first as an always-running unified baseline, not as
a near-only second engine. It therefore avoids viewport motion, engine
switching, target-state handoff and a second inference pass.

## 2. Why it precedes dynamic viewport work

The `320x256` input contains 81,920 pixels, versus 199,680 for `480x416`.
That is 41.03% of the current input area. The physical `640x512` crop covers
1.64 times the area of the current crop while preserving a uniform 2x X/Y
mapping.

This can solve a useful part of the close-range retention problem with a
static transform. It may also reduce inference time, GPU occupancy and energy
per frame. It can regress small or distant targets because each physical target
occupies fewer detector-input pixels.

The predictive dynamic viewport remains the fallback when this static candidate
cannot preserve enough small/far accuracy or does not retain close targets far
enough. Its M0 work does not begin until V0 reaches a recorded decision.

The recorded V0 decision and evidence are in
`docs/project/FIXED_WIDE_VISION_V0_ACCEPTANCE_20260727.md`.

## 3. V0 question

V0 answers:

> Does a fixed `640x512 -> 320x256` pipeline provide enough close-target and
> edge-coverage benefit, at materially lower per-frame cost, without an
> unacceptable common-visible detection regression?

V0 is a diagnostic offline comparison. It cannot authorize production because
the available YOLO validation images are not causal full-screen gameplay
sequences and the dataset path does not include DXGI capture.

## 4. Candidate matrix

| Candidate | Physical crop | Tensor input | Isolates |
|---|---:|---:|---|
| `fixed480` | `480x416` | `480x416` | production baseline |
| `wide480` | `600x520` | `480x416` | wider field without smaller Tensor |
| `fixed320` | `320x256` | `320x256` | smaller Tensor without wider field |
| `wide320` | `640x512` | `320x256` | complete candidate |

`wide480` uses an exact 1.25 scale and `wide320` uses an exact 2.0 scale, so
neither introduces X/Y aspect distortion.

The first `320x256` engine is exported from the same `best.pt` as the current
engine. Retraining at `320x256` is a later experiment only if the exported
engine establishes useful headroom.

## 5. Dataset validity

A requested crop larger than its source image is not a valid candidate run.
The benchmark must support a strict mode that skips and records undersized
images instead of silently clamping the crop.

The first V0 comparison uses only datasets whose source frames contain every
candidate crop being compared. Existing `320x320` processed datasets cannot
support `wide320`; they may be used for detector-resolution diagnostics but not
for physical-field comparisons.

Every evaluated image keeps:

- stable dataset/split/image identity;
- original source dimensions;
- requested and applied crop;
- original ground-truth boxes;
- crop-local clipped boxes;
- each target's visible fraction;
- detector-input target dimensions and size bucket;
- match result and IoU;
- per-frame stage timings.

This lets separate candidate runs be joined by stable image and target keys.

## 6. Accuracy views

Overall F1 is necessary but not sufficient. V0 reports:

- all crop-visible targets;
- common-visible targets present in both baseline and candidate;
- newly covered targets visible only to the wider candidate;
- targets by detector-input size bucket;
- targets by visible-fraction/clipping bucket;
- false positives per image and per newly covered region;
- matched IoU and confidence distributions.

The initial size buckets use detector-input box area:

- `tiny`: area below `16^2` pixels;
- `small`: `[16^2, 32^2)`;
- `medium`: `[32^2, 96^2)`;
- `large`: `[96^2, 160^2)`;
- `very_large`: at least `160^2`.

The initial visibility buckets are:

- `severe_clip`: visible fraction below 0.50;
- `partial`: `[0.50, 0.90)`;
- `mostly_visible`: `[0.90, 0.999)`;
- `full`: at least 0.999.

The raw continuous values remain in per-frame evidence so bucket boundaries can
be changed without rerunning inference.

## 7. Performance views

Each inference already produces preprocess, inference, GPU-total, output-wait
and decode timings. V0 retains their per-frame values and reports
`avg/p50/p90/p95/p99/max`.

It additionally records:

- wall-clock time per frame;
- process CPU time per frame;
- optional process resident memory samples;
- deadline misses for configured frame budgets;
- sustained images per second;
- GPU utilization and memory;
- power, temperature and session energy;
- images, true positives and F1 per unit time/energy.

`nvidia-smi` samples are session-level resource evidence, not exact per-frame
attribution. CUDA/native stage timings are the authoritative per-frame GPU
latency evidence.

The offline dataset benchmark does not include capture cost. A later native
BGRA/live test must measure the complete:

```text
DXGI capture -> source read -> downscale -> TensorRT -> output/decode
```

This is required because `640x512` reads 1.64 times as many source pixels as
the production `480x416` crop even though its Tensor input is 58.97% smaller.

## 8. Provenance

Every retained artifact records:

- candidate identifier;
- engine path, byte size and SHA-256;
- inspected engine tensor shapes;
- requested crop and detector input;
- confidence, IoU and class policy;
- dataset roots, split and source identity;
- benchmark revision and dirty-worktree state;
- warmup, image limit, GPU index and monitoring interval;
- timestamp and artifact schema version.

Artifacts without this identity are diagnostic only and cannot promote a
candidate.

## 9. V0 gates

The initial gates are:

1. Engine inspection confirms input `[1,3,256,320]` and production-compatible
   output `[1,300,6]`.
2. No non-finite output, coordinate-contract error or silent crop clamp occurs.
3. `wide320` paired common-visible recall and its per-run overall F1 are each
   no more than 0.03 below `fixed480`.
4. Tiny/small regressions are reported separately and do not disappear into an
   overall average.
5. Large, very-large or clipped-target coverage/recall shows a clear benefit.
6. Inference and GPU-total p50 improve by at least 25%; p95/p99 remain stable
   and repeatable.
7. Resource monitoring shows no unexplained memory, power or thermal
   regression.

Passing V0 authorizes `320x256` training and a full-screen recorded-video
comparison. It does not authorize the live production switch.

## 10. Staged decision

```text
benchmark contract + tests
    -> export same-weight 320x256 engine
    -> four-candidate V0 on eligible static datasets
    -> if promising: 320x256 training
    -> full-screen recorded-video and native BGRA/capture benchmark
    -> live default-off trial
```

If `wide320` passes accuracy, coverage and end-to-end performance gates, it
becomes the simpler production candidate. If it fails because small/far
detection loses too much accuracy, proceed to the predictive dynamic viewport
M0 rather than hiding the regression with a second uncontrolled inference path.

## 11. Non-goals

V0 does not:

- change the live capture rectangle or production engine;
- switch engines based on target size;
- add dynamic ROI movement or zoom;
- retrain weights before the export-only comparison;
- claim that processed square images reproduce live close-range motion;
- grant target, aim or fire authority;
- compare runs whose eligible image populations differ without paired evidence.
