# COD Person Detector Training Results

## Overview

This document records the local COD one-class detector optimization run from 2026-05-24. The detector is trained from local YOLO datasets under `models/train` and is intended for `native-vision` style target detection.

## Data

Dataset YAML:

```text
models/train/cod_combined_single_cls.yaml
```

Split summary:

```text
train: 9,076 images, 15,854 target boxes, 920 empty/background images
valid: 1,582 images, 2,890 target boxes, 28 empty/background images
test :   185 images,   316 target boxes,   8 empty/background images
```

The data contains many small targets. Median training box width is about 6.7% of image width, and the 10th percentile is below 1.9%.

## Baseline

Prior stable loop:

```text
runs/person_train/cod_safe_loop_20260524-045506_train_02/weights/best.pt
```

Validation at 640:

```text
P=0.609, R=0.521, mAP50=0.552, mAP50-95=0.257
```

Validation at 896:

```text
P=0.594, R=0.420, mAP50=0.455, mAP50-95=0.213
```

## Experiments

### Failed: 896 Low-Augment Fine-Tune

Run:

```text
cod_quality_896_e12_20260524-052019
```

Stopped after 3 epochs because validation quality degraded:

```text
epoch 1: P=0.519, R=0.329, mAP50=0.327, mAP50-95=0.139
epoch 2: P=0.523, R=0.254, mAP50=0.300, mAP50-95=0.124
epoch 3: P=0.512, R=0.300, mAP50=0.318, mAP50-95=0.130
```

Conclusion: jumping to 896 with the original auto learning-rate behavior degraded the existing model.

### Failed: 640 Default-Augment Continuation

Run:

```text
cod_continue_640_default_e8_20260524-053911
```

Stopped after 3 epochs because auto optimizer/warmup degraded the model:

```text
epoch 1: P=0.617, R=0.485, mAP50=0.514, mAP50-95=0.249
epoch 2: P=0.524, R=0.421, mAP50=0.443, mAP50-95=0.182
epoch 3: P=0.563, R=0.370, mAP50=0.398, mAP50-95=0.162
```

Conclusion: the main failure mode was not missing full-data training; it was overly aggressive continuation training from an already useful checkpoint.

### Candidate A: 640 Low-LR Fine-Tune

Run:

```text
cod_finetune_640_lowlr_e6_20260524-054955
```

Best path:

```text
runs/person_train/cod_finetune_640_lowlr_e6_20260524-054955_train_01/weights/best.pt
```

Final validation:

```text
P=0.789, R=0.702, mAP50=0.771, mAP50-95=0.450
```

This candidate has better precision than the final polish run.

### Candidate B: 640 Low-LR Polish

Run:

```text
cod_polish_640_lowlr_e4_20260524-061012
```

Recommended path:

```text
runs/person_train/cod_polish_640_lowlr_e4_20260524-061012_train_01/weights/best.pt
```

Final validation:

```text
P=0.767, R=0.731, mAP50=0.774, mAP50-95=0.449
```

Confusion matrix:

```text
true positive item detections: 1982
missed item targets: 460
background false positives: 908
```

Compared with the prior stable loop, this improves recall and substantially reduces background false positives. This is the recommended candidate when the priority is fewer missed labeled targets.

## Recommended Command

Use this recipe for future continuation from the prior stable model:

```powershell
py -3 -B tools\run_person_detector_loop.py `
  --model runs\person_train\cod_safe_loop_20260524-045506_train_02\weights\best.pt `
  --data models\train\cod_combined_single_cls.yaml `
  --loops 1 `
  --epochs-per-loop 6 `
  --train-imgsz 640 `
  --val-imgsz 640 `
  --batch 8 `
  --workers 0 `
  --cache false `
  --run-prefix cod_finetune_640_lowlr `
  --skip-baseline `
  --plots `
  --auto-augment none `
  --mosaic 0.2 `
  --erasing 0 `
  --scale 0.2 `
  --translate 0.05 `
  --warmup-epochs 0 `
  --close-mosaic 0 `
  --optimizer AdamW `
  --lr0 0.0002 `
  --lrf 0.2 `
  --cos-lr
```

## Next Steps

- Run the recommended model against real native-vision screenshots or captured gameplay clips.
- If live false positives are too noticeable, try Candidate A first because it has higher precision.
- If live missed targets are still the main issue, try Candidate B first because it has higher recall.
- The next meaningful quality jump likely needs hard-negative data cleanup for HUD, streamer overlays, loot outlines, and non-target UI objects rather than just more epochs.
