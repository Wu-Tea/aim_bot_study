# Fixed-Wide Vision V0 Acceptance

Date: 2026-07-27
Status: `wide320` export-only candidate rejected; extended size/aspect sweep
identifies `480x384` and `512x352` follow-up candidates; no production change

## Outcome

The fixed `640x512 -> 320x256` candidate did not satisfy either of its primary
V0 gates:

- paired common-visible recall fell by 18.24 percentage points;
- repeated native GPU p50 improved by only 3.21% at the median, versus the
  required 25%, while median p95 regressed 16.48%.

The failure is specifically associated with reducing detector input
resolution. A fixed wider physical crop feeding the existing `480x416` engine
retained substantially more accuracy. The strongest near-coverage diagnostic
was `630x546 -> 480x416` (1.3125x): common-visible recall fell only 1.03 points,
overall F1 fell 1.14 points, and 24 of 44 newly visible targets were detected.

No runtime, capture, selector, tracker or controller behavior was changed.

An extended same-weight TensorRT sweep later the same day confirmed that the
original `320x256` proposal was too aggressive, not that resized capture is
generally unsuitable. In particular, `640x440 -> 512x352` improved paired
common-visible recall by 1.03 points, detected 23 of 38 newly visible targets,
and produced the best overall F1 in the sweep. Its paired native GPU p50 cost
was 5.24%, so it is a near-coverage candidate rather than a compute-saving
candidate.

## Candidate identity

Production baseline:

```text
engine: models/best.engine
SHA-256: 5566D50D444A3EB917F55A9288DF9E35AC62ECC6A52B280CDF72147647AE39C0
input:  [1,3,416,480]
output: [1,300,6]
```

Export-only candidate:

```text
weights: models/best.pt
engine: models/best_320x256.engine
SHA-256: C6DBC482C661BF76787376C06B7ADE5CF4C714B94003A686720E5AB16358D3B9
input:  [1,3,256,320]
output: [1,300,6]
precision: FP16
workspace: 4 GiB
TensorRT: 10.15.1.29
```

The candidate was exported through an isolated temporary directory. The
production engine hash was unchanged after export.

## Benchmark contract

The accuracy diagnostic used:

- `DeltaForceFireMode.v15i.yolo26`, first 500 validation images;
- `Takov.v16i.yolo26`, first 500 validation images;
- target classes `enemy,target`;
- confidence 0.25;
- IoU 0.50;
- class-unaware prediction matching, because source datasets have incompatible
  class IDs;
- strict requested-crop validation;
- identical source images joined by dataset/split/image identity;
- original target index retained across candidates.

R6 `640x412` and Warzone `320x320` sources were recorded as undersized and
excluded rather than silently clamped.

These processed static datasets are sufficient for a falsification and scale
sweep. They are not proof of live near-target continuity.

## Accuracy result

| Candidate | Physical crop | Tensor | GT | Precision | Recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| `fixed480` | `480x416` | `480x416` | 680 | 0.6654 | 0.7809 | 0.7185 |
| `wide320` | `640x512` | `320x256` | 723 | 0.6834 | 0.5851 | 0.6304 |
| fixed 1.125x | `540x468` | `480x416` | 698 | 0.6917 | 0.7779 | 0.7323 |
| fixed 1.25x | `600x520` | `480x416` | 720 | 0.6654 | 0.7569 | 0.7083 |
| fixed 1.3125x | `630x546` | `480x416` | 724 | 0.6634 | 0.7569 | 0.7071 |

The paired view removes the changing target population:

| Candidate | Common targets | Common recall delta | Newly visible | Newly visible TP |
|---|---:|---:|---:|---:|
| `wide320` | 680 | -0.1824 | 43 | 16 |
| fixed 1.125x | 680 | +0.0147 | 18 | 2 |
| fixed 1.25x | 680 | 0.0000 | 40 | 14 |
| fixed 1.3125x | 680 | -0.0103 | 44 | 24 |

The `wide320` common-visible loss by candidate Tensor-size bucket was:

| Bucket | Targets | Baseline recall | Candidate recall | Delta |
|---|---:|---:|---:|---:|
| tiny | 77 | 0.7143 | 0.1299 | -0.5844 |
| small | 210 | 0.7238 | 0.5190 | -0.2048 |
| medium | 313 | 0.8371 | 0.7157 | -0.1214 |
| large | 62 | 0.7742 | 0.7903 | +0.0161 |
| very large | 18 | 0.7778 | 0.8333 | +0.0556 |

This is the expected near/far trade: large targets survive the lower-resolution
Tensor, but tiny and small targets do not.

The fixed 1.3125x existing-engine candidate was much more balanced:

| Bucket | Targets | Recall delta |
|---|---:|---:|
| tiny | 22 | -0.1818 |
| small | 123 | -0.0650 |
| medium | 361 | -0.0222 |
| large | 111 | +0.0901 |
| very large | 63 | +0.0476 |

## Performance result

Five 1,000-iteration native BGRA runs compared:

```text
fixed480: 480x416 BGRA -> 480x416 engine
wide320:  640x512 BGRA -> 320x256 engine
```

Paired `wide320` GPU-total deltas were:

| Repeat | p50 | p95 |
|---:|---:|---:|
| 1 | -4.63% | -13.05% |
| 2 | +1.07% | +16.48% |
| 3 | -3.21% | +37.27% |
| 4 | +31.46% | +53.39% |
| 5 | -4.82% | -0.04% |
| median | -3.21% | +16.48% |

Lower Tensor pixel count therefore did not produce a material latency win on
this YOLO26n end-to-end engine and RTX 4070 SUPER. The likely interpretation is
that fixed kernel launch, end-to-end/top-K/output work and GPU clock behavior
dominate at this small batch/model size. This is an inference from the timing
shape, not a kernel-profiled causal proof.

The dataset run also failed to show a speed benefit: `wide320` wall p50 was
2.996 ms versus 2.609 ms for `fixed480`. Dataset timing varied with GPU state
and run order, so the repeated native result is the primary latency evidence.

Session-level `nvidia-smi` samples remain diagnostic rather than per-frame
attribution. They do not overturn the native per-frame result.

## Decision

Reject the same-weight `wide320` candidate and do not promote it to production.
Do not begin `320x256` retraining solely to obtain compute savings: training
may recover some accuracy, but this engine already failed to establish the
required runtime headroom.

Advance the following to recorded full-screen video:

1. `fixed480` baseline;
2. fixed 1.25x: `600x520 -> 480x416`;
3. fixed 1.3125x: `630x546 -> 480x416`.

The 1.25x candidate is the balanced control. The 1.3125x candidate is the
near-coverage preference because it detected 24 newly visible targets while
remaining inside the 3-point common-visible and overall-F1 gates.

The next evidence must measure:

- close approach and target growth;
- edge retention and target clipping;
- ordinary far/small target continuity;
- full DXGI capture-to-decode latency and deadline misses;
- GPU memory, utilization, power and energy;
- controller-visible detection continuity, without changing aim policy.

If neither fixed scale retains close targets far enough, resume predictive
dynamic viewport M0. If fixed 1.25x or 1.3125x passes video and live gates, it
remains simpler than dynamic ROI and should be preferred.

## Reproduction

The benchmark implementation and raw artifacts retain exact per-frame evidence
and engine provenance. Representative commands:

```powershell
python tools/benchmark_vision_dataset.py `
  --model models/best.engine `
  --datasets D:/datasets/roboflow_candidates `
  --split valid --max-images 500 `
  --target-classes enemy,target `
  --crop-width 480 --crop-height 416 `
  --strict-crop-size --candidate-id fixed480-quick500-r2 `
  --warmup 50 --gpu-monitor --gpu-monitor-interval-ms 100 `
  --frame-jsonl artifacts/vision-wide-v0/quick500-r2/fixed480.frames.jsonl `
  --output-json artifacts/vision-wide-v0/quick500-r2/fixed480.summary.json

python tools/compare_vision_benchmark_candidates.py `
  --baseline artifacts/vision-wide-v0/quick500-r2/fixed480.frames.jsonl `
  --candidate artifacts/vision-wide-v0/quick500-r2/wide320.frames.jsonl `
  --output-json artifacts/vision-wide-v0/quick500-r2/fixed480-vs-wide320.json
```

Raw evidence root:

```text
artifacts/vision-wide-v0/
```

## Addendum: extended size and aspect sweep

The follow-up deliberately varied both Tensor size and aspect ratio rather than
only scaling the original shape. Every engine was exported from the same
`models/best.pt` weights with FP16 TensorRT and tested against the same 1,000
eligible images. Physical crops used a uniform scale in both axes, so geometry
remained isotropic.

| Tensor | Physical crop | Scale | Precision | Recall | F1 | Common recall delta | New TP / visible |
|---|---:|---:|---:|---:|---:|---:|---:|
| `480x416` baseline | `480x416` | 1.0000x | 0.665 | 0.781 | 0.719 | — | — |
| `512x384` | `640x480` | 1.2500x | 0.656 | 0.766 | 0.707 | -0.59 pp | 26 / 42 |
| `480x384` | `640x512` | 1.3333x | 0.688 | 0.758 | 0.721 | -0.88 pp | 23 / 43 |
| `480x352` | `630x462` | 1.3125x | 0.672 | 0.750 | 0.709 | -1.91 pp | 22 / 40 |
| `512x352` | `640x440` | 1.2500x | 0.673 | 0.781 | 0.723 | +1.03 pp | 23 / 38 |
| `448x384` | `616x528` | 1.3750x | 0.653 | 0.719 | 0.685 | -4.12 pp | 17 / 43 |
| `448x352` | `616x484` | 1.3750x | 0.661 | 0.716 | 0.688 | -4.56 pp | 17 / 42 |
| `416x320` | `624x480` | 1.5000x | 0.676 | 0.693 | 0.684 | -7.06 pp | 17 / 42 |
| `352x288` | `616x504` | 1.7500x | 0.675 | 0.628 | 0.650 | -13.24 pp | 13 / 43 |
| `320x256` | `640x512` | 2.0000x | 0.683 | 0.585 | 0.630 | -18.24 pp | 16 / 43 |

The comparison between `480x352` and `448x384` is especially informative:
the former has fewer Tensor pixels but retains 3.5 more common-recall points
and detects five more newly visible targets. For this dataset and model,
horizontal sampling is more valuable than adding vertical rows. Total pixel
count alone is therefore not a sufficient size-selection rule.

Three-round interleaved native medians were:

| Tensor | GPU p50 | Delta | GPU p95 | Delta |
|---|---:|---:|---:|---:|
| `480x416` baseline | 2.446 ms | — | 3.593 ms | — |
| `512x384` | 2.578 ms | +5.4% | 4.034 ms | +12.3% |
| `480x384` | 2.473 ms | +1.1% | 3.780 ms | +5.2% |
| `480x352` | 2.439 ms | -0.2% | 3.498 ms | -2.6% |
| `448x384` | 2.408 ms | -1.5% | 3.503 ms | -2.5% |
| `448x352` | 2.290 ms | -6.4% | 3.544 ms | -1.4% |
| `416x320` | 2.496 ms | +2.1% | 3.633 ms | +1.1% |
| `352x288` | 2.517 ms | +2.9% | 3.691 ms | +2.7% |
| `320x256` | 2.358 ms | -3.6% | 3.355 ms | -6.6% |

`512x352` initially showed order-sensitive timing, so it received a separate
four-round alternating baseline/candidate run. Median GPU-total latency was
`2.510 -> 2.641 ms` at p50 (+5.24%) and `3.946 -> 3.948 ms` at p95 (+0.07%).
These paired numbers supersede its less stable sequential result.

### Performance/value marks

The marks below keep the acceptance gate separate from the ranking. A
candidate first needs common-visible recall loss no worse than 3 percentage
points. Candidates that fail that gate are not called good value merely
because they run faster.

For gate-passing candidates, the diagnostic value index is:

```text
quality = 50% * (F1 / baseline F1) + 50% * (total TP / baseline total TP)
value   = 100 * quality / (GPU p50 / baseline GPU p50)
```

This is a compact comparison aid, not a replacement for the raw metrics.
Baseline value is 100. `512x352` uses its alternating paired p50 ratio because
its sequential timing was order-sensitive.

| Mark | Candidate | Performance | Quality index | Value index | Interpretation |
|---|---|---:|---:|---:|---|
| `★ BALANCED` | `480x384 / 640x512` | p50 +1.1% | 101.8 | **100.6** | Best overall balance |
| `⚡ LATENCY` | `480x352 / 630x462` | p50 -0.2% | 100.2 | **100.4** | Near-zero latency cost |
| `◎ NEAR` | `512x352 / 640x440` | paired p50 +5.2% | **103.1** | 98.0 | Best detection/coverage result |
| `○ COVERAGE` | `512x384 / 640x480` | p50 +5.4% | 101.2 | 96.1 | More new TP, weaker F1/p95 |
| `BASE` | `480x416 / 480x416` | 2.446 ms | 100.0 | 100.0 | Production reference |
| `× FAIL` | `448x384` and smaller | varies | — | — | Common recall loss exceeds gate |

The ranking therefore depends on the operating goal: `480x384` is the
price/performance choice, `480x352` is the latency-preserving choice, and
`512x352` is the close-range coverage choice. There is no single candidate
that wins all three axes.

The updated shortlist is:

1. `480x384 / 640x512`: balanced static-wide candidate, essentially baseline
   F1 and approximately baseline p50.
2. `512x352 / 640x440`: strongest static near/edge candidate, with the best
   paired common recall and overall F1, at a modest p50 cost.
3. `480x352 / 630x462`: latency-neutral conservative candidate, but with a
   larger accuracy loss than the two candidates above.

The static validation material still does not reproduce the intended
close-range failure. It contains 231 `large` or `very_large` targets among the
680 baseline-visible targets, but it has no temporal approach sequence: no
target can grow between frames, move toward a crop edge, or demonstrate
detection continuity while clipped. Consequently this sweep can reject bad
resolutions and rank plausible candidates, but it cannot accept near-target
continuity. The next benchmark should use recorded full-screen approach
sequences and score detection continuity as the target grows, clips against an
edge, and approaches the current crop boundary.

Extended evidence root:

```text
artifacts/vision-size-matrix/
```
