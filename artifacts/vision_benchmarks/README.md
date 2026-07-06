# Vision Benchmark Baselines

Baseline date: 2026-07-06

Model:

```text
models/candidates/body_union_manual_core_x2_neg_e6_480x416_ws10.engine
```

Runtime change:

```text
native/vision_native/src/vision_engine.cpp
kSelectorDecodeConfidenceFloor = 0.40
```

## COD Valid Confidence Comparison

Dataset group: `models/train`, split `valid`, target classes `body,enemy,mw2_body`, crop `480x416`, IoU `0.50`.

| Confidence | Images | Detections | TP | FP | FN | Precision | Recall | F1 | FP/Image | FN/Image |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.25 | 1582 | 1568 | 1273 | 295 | 342 | 0.812 | 0.788 | 0.800 | 0.186 | 0.216 |
| 0.40 | 1582 | 1334 | 1178 | 156 | 437 | 0.883 | 0.729 | 0.799 | 0.099 | 0.276 |

Interpretation: `0.40` keeps F1 effectively flat while cutting false positives per image nearly in half, so it is the preferred live starting threshold for stronger target authority.

## GPU Resource Baselines

| Benchmark | F1 | Avg GPU Util | Avg Memory | Avg Power | Energy | Images/s | TP/s | Images/kJ | TP/kJ |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| COD valid all, conf 0.25 | 0.583 | 32.9% | 4647 MB | 55.2 W | 512.7 J | 170.5 | 137.9 | 3085.7 | 2496.6 |
| COD valid body/enemy, conf 0.25 | 0.800 | 59.8% | 4910 MB | 67.7 W | 746.5 J | 143.5 | 115.5 | 2119.3 | 1705.3 |
| COD valid body/enemy, conf 0.40 | 0.799 | 59.9% | 4914 MB | 67.7 W | 738.0 J | 145.2 | 108.1 | 2143.5 | 1596.1 |
| COD test all, conf 0.25 | 0.540 | 28.3% | 4675 MB | 40.3 W | 62.6 J | 119.0 | 82.4 | 2955.9 | 2045.2 |

The authoritative raw metrics are the JSON files in this directory. Failure images and `failures.jsonl` files are local-only artifacts and are intentionally ignored by git.
