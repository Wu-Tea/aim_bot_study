# COD Person Detector Quality Optimization Plan

## Overview

The goal is to turn the current stable training loop into a quality-oriented detector experiment for COD gameplay screenshots. The detector is a one-class YOLO model that should identify visible human targets from local datasets under `models/train` and produce a candidate weight file for `native-vision` style runtime use.

## Current Situation

- Local data combines three YOLO datasets through `models/train/cod_combined_single_cls.yaml`.
- Train split has 9,076 images, 920 empty/background images, and 15,854 target boxes.
- Validation split has 1,582 images, 28 empty/background images, and 2,890 target boxes.
- Prior stable smoke result after two 640px one-epoch loops reached roughly `P=0.609`, `R=0.521`, `mAP50=0.552`, `mAP50-95=0.257`.
- The prior loops were not final-quality training: only 2 total epochs and the default 3-epoch warmup meant the model had barely started normal convergence.
- Label stats show many small targets. Median train box width is about 6.7% of image width, and the 10th percentile is below 1.9%.

## Proposed Direction

1. Keep Windows-safe execution:
   - `workers=0`
   - `cache=false`
   - real script entrypoints, no stdin/inline multiprocessing entrypoints
2. Make training augmentation explicit and COD-friendly:
   - disable mosaic for the first quality pass
   - disable random erasing
   - disable auto augment
   - lower scale/translate
   - shorten warmup for short fine-tunes
3. Run a higher-resolution quality pass:
   - start from `runs/person_train/cod_safe_loop_20260524-045506_train_02/weights/best.pt`
   - train at `imgsz=896`
   - validate at `imgsz=896`
   - use `batch=8`
   - run 12 epochs first
4. Compare against the prior stable result and decide:
   - stop if recall/mAP materially improve and validation images look better
   - run another pass if training is still improving
   - avoid longer training if false positives or validation metrics regress

## Concrete Commands

Primary experiment:

```powershell
py -3 -B tools\run_person_detector_loop.py `
  --model runs\person_train\cod_safe_loop_20260524-045506_train_02\weights\best.pt `
  --data models\train\cod_combined_single_cls.yaml `
  --loops 1 `
  --epochs-per-loop 12 `
  --train-imgsz 896 `
  --val-imgsz 896 `
  --batch 8 `
  --workers 0 `
  --cache false `
  --run-prefix cod_quality_896_e12 `
  --skip-baseline `
  --plots `
  --auto-augment none `
  --mosaic 0 `
  --erasing 0 `
  --scale 0.25 `
  --translate 0.05 `
  --warmup-epochs 1
```

## Verification

- Parse final `results.csv` and validation logs.
- Compare precision, recall, mAP50, and mAP50-95 against the prior 640 stable result.
- Inspect generated validation images:
  - `val_batch*_labels.jpg`
  - `val_batch*_pred.jpg`
  - `confusion_matrix.png`
  - `BoxPR_curve.png`
- Prefer a model that improves recall without exploding background false positives.

## Risks

- More epochs alone can overfit noisy labels.
- Higher resolution helps small targets but may still miss heavily occluded or tiny heads if labels are inconsistent.
- Disabling mosaic can reduce regularization; the experiment should be compared by validation results rather than assumed better.
