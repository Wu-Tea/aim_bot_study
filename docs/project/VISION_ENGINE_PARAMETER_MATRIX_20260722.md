# Vision Engine Parameter Matrix — 2026-07-22

Status: historical fixed-ROI evidence. The current production path uses
dynamic ROI with a `480x384` inference tensor; this document does not define
the current runtime baseline.

## Purpose

Compare TensorRT export parameters for the production `480x416`, batch-1 vision path without changing weights, crop geometry, confidence thresholds, post-processing, or the production engine. The existing `models/best.engine` remains the baseline.

## Fixed Contract

- Source weights: `models/best.pt`
- GPU: NVIDIA GeForce RTX 4070 SUPER, TensorRT 10.15.1.29
- Input/output: `[1,3,416,480] -> [1,300,6]`
- Export: static shape, batch 1, `simplify=true`, `dynamic=false`, `nms=false`
- Dataset: 3,288 validation images, 1,999 selected ground-truth objects
- Match thresholds: confidence 0.25, IoU 0.50
- Native benchmark: BGRA `480x416`, 50-100 warmups, 1,000-2,000 measured iterations

Candidates changed only precision and TensorRT workspace:

| Candidate | Precision | Workspace | Engine size | Build time |
|---|---:|---:|---:|---:|
| Existing production | FP16 | historical/unknown | 8,530,379 B | historical |
| FP16 ws1 | FP16 | 1 GiB | 8,501,675 B | about 259 s |
| FP16 ws4 | FP16 | 4 GiB | 8,125,499 B | about 203 s |
| FP16 ws10 | FP16 | 10 GiB | 8,422,419 B | about 390 s |
| FP32 ws4 | FP32 | 4 GiB | 11,890,140 B | about 89 s |

## Dataset Result

Accuracy is deterministic for each engine. Timing and energy are sensitive to GPU clocks and background load, so the table uses the later controlled run where available and is supplemented by paired native runs below.

| Candidate | Precision | Recall | F1 | F1 change | Infer p50 | Infer p95 |
|---|---:|---:|---:|---:|---:|---:|
| Existing production | 0.5622 | 0.7999 | 0.66033 | baseline | 2.934 ms | 5.440 ms |
| FP16 ws1 | 0.5636 | 0.8004 | 0.66143 | +0.166% | 2.609 ms | 4.710 ms |
| FP16 ws4 | 0.5615 | 0.8019 | 0.66049 | +0.023% | 2.659 ms | 5.014 ms |
| FP16 ws10 | 0.5618 | 0.7999 | 0.66006 | -0.041% | 2.654 ms | 3.151 ms |
| FP32 ws4 | 0.5611 | 0.7989 | 0.65924 | -0.166% | 3.155 ms | 5.061 ms |

The small FP16 accuracy differences are tactic/numerical effects, not a changed detector. FP32 did not recover accuracy and was slower, so it is dominated for this runtime.

## Paired Native Result

Separate-process dataset timing produced one anomalously fast baseline run (`1.866 ms` p50). It was rejected as a promotion basis because it did not reproduce. Interleaved native BGRA runs were added to compare engines under closer GPU conditions.

- FP16 ws1: typical paired p50 improvement about 8%; typical p95 improvement about 5-7%. Two runs had worse p99, so tail stability is not conclusively better.
- FP16 ws4: p50 improved in all three final pairs by 8.1-9.0%. p95 changes were `-10.8%`, `+13.9%`, and `-1.0%`; mean p99 was about 9% lower. This is the most balanced candidate, but its p95 advantage is not proven.
- FP16 ws10: p50 improved by about 4.4% across three final pairs. Its apparent 31.8% p95 win in the first two runs disappeared under pairing; paired mean p95 improved only about 0.8%, with noisy p99.
- FP32 ws4: the initial two-run native mean was 16.4% slower at p50 and 14.6% slower at p95.

## Loss / Gain Interpretation

For the preferred FP16 ws4 candidate relative to production:

- Accuracy loss: none demonstrated; F1 changed by `+0.00015` absolute (`+0.023%` relative).
- Median latency: consistently about `8-9%` lower in final paired native runs.
- Tail latency: inconclusive at p95; mean p99 improved, but individual p95 runs moved in both directions.
- Model storage: about `4.7%` smaller.
- Build cost: about 3.4 minutes on this machine.
- Energy: current whole-run samples are too clock-state-dependent to claim a reliable gain. Do not use the single-run joule values as a promotion gate.

## Decision and Promotion

FP16 with `workspace=4` was selected because it provides the most repeatable p50 gain, preserves F1, and avoids the excessive build cost of workspace 10.

After explicit user approval, it was promoted to `models/best.engine` on 2026-07-22. The previous production engine is preserved as the read-only rollback artifact `models/2026-07-22/best_pre_fp16_ws4.engine`.

- Previous/backup SHA-256: `5CFA9F334542739FF2BD36C58911F482B57A52D33D6AAD2F760286AE1101AC21`
- Promoted production SHA-256: `5566D50D444A3EB917F55A9288DF9E35AC62ECC6A52B280CDF72147647AE39C0`
- Post-promotion native contract: `[1,3,416,480] -> [1,300,6]`
- Post-promotion 500-iteration smoke: infer p50 `1.896 ms`, infer p95 `2.778 ms`, GPU-total p95 `2.839 ms`

One real-session A/B check remains useful for detection continuity and frame-time spikes. Rollback requires copying the preserved backup over `models/best.engine`; do not overwrite the backup itself.

Do not select FP32. Do not claim that increasing workspace monotonically improves runtime; TensorRT tactic selection and live GPU state dominate that assumption here.

Raw local evidence is under `runs/vision_engine_matrix_20260722/` and is intentionally not a production dependency.
