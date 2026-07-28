# Dynamic Viewport Benchmark Acceptance

## What must be proven

Dynamic viewport has two separate claims:

1. A larger viewport can recover a close target that is growing out of, or is
   clipped by, the precision viewport.
2. The runtime controller changes viewport early enough and does not cause
   coordinate jumps, target switches, or oscillation.

Dataset recall alone cannot prove both claims. The offline benchmark therefore
uses two paired tests, followed by recorded or live sequence validation.

## Paired viewport sweep

`tools/benchmark_vision_viewport_sweep.py` runs the same image and the same
ground-truth target identity through:

- precision: 360x312;
- normal: 480x416;
- rescue: 600x520;
- fixed TensorRT input: 480x416.

It reports three cohorts:

- `fully_visible_all`: target is fully visible in every viewport, isolating
  scale effects;
- `precision_clipped`: the precision viewport clips or excludes the target,
  measuring the main near/edge recovery case;
- `all_rescue_visible`: total target availability inside the rescue viewport.

Command used:

```powershell
D:\env\python\python.exe tools/benchmark_vision_viewport_sweep.py `
  --model models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine `
  --datasets D:/datasets/roboflow_candidates `
  --split valid --max-images 500 --target-classes enemy,target `
  --conf 0.25 --iou 0.5 --warmup 20 `
  --output-json .tmp/dynamic-viewport-paired-sweep.json
```

Result on 614 images and 720 rescue-visible targets:

| Cohort | Precision | Normal | Rescue |
|---|---:|---:|---:|
| All rescue-visible | 68.33% | 73.89% | 75.69% |
| Fully visible in all | 85.84% | 82.74% | 78.32% |
| Precision clipped/outside | 38.81% | 58.96% | 71.27% |

Precision-to-rescue recovered 98 targets and regressed 45. Within the
fully-visible cohort it recovered only 8 and regressed 42. The gain therefore
comes primarily from restoring scene coverage, not from making every target
smaller.

This evidence rejects unconditional zoom-out after a miss. Rescue should be
opened only from committed-target growth, boundary pressure, or immediate
post-boundary loss.

## Target-centered scale sweep

`tools/benchmark_vision_target_scale_sweep.py` keeps each target identity
centered, changes only scene scale, and discards samples where the target would
be clipped. This isolates the detector's size envelope from viewport coverage.

The scale is relative to a 480x416 context:

- 0.8 is rescue-like;
- 1.0 is normal-like;
- 1.333 is precision-like;
- 1.6 through 4.0 simulate continued approach without switching.

On the 137 targets present at every tested scale:

| Scale | Recall |
|---:|---:|
| 0.8 | 71.53% |
| 1.0 | 77.37% |
| 1.333 | 89.78% |
| 1.6 | 84.67% |
| 2.0 | 85.40% |
| 2.5 | 88.32% |
| 3.0 | 83.21% |
| 4.0 | 78.10% |

The model does not show a hard failure merely because a fully visible target
becomes large. The stronger failure mode in the current dataset is clipping or
loss of surrounding context. This supports boundary-aware switching rather
than an area-only threshold.

## Remaining sequence gate

These paired image tests prove that a rescue viewport has useful observations
available. They do not prove that the production state machine switches at the
right frame.

Final acceptance requires a recorded close-range sequence or live
output-disabled run with:

- viewport sequence and size per frame;
- committed target identity;
- body box and visible fraction;
- first boundary-pressure frame;
- first Normal and Rescue frame;
- complete-miss duration;
- coordinate delta on each viewport switch;
- number of viewport reversals in a 500 ms window.

Pass criteria:

- Rescue begins before the first complete miss, or reduces the longest
  consecutive miss against fixed 360x312 and fixed 480x416;
- committed identity switches do not increase;
- viewport-switch coordinate jump is at most one detector-frame residual;
- no more than one shrink transition occurs inside the configured dwell;
- far-target recall is unchanged while no near/boundary evidence exists.

Until this sequence gate passes, `dynamic_viewport_enabled` remains false by
default.
