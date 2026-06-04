# Roboflow Visible-Body Data Loop

This note records the disk-backed Roboflow data path for the current detector baseline.

## Goal

Use Roboflow Universe exports as offline training material for the current one-class person detector, but keep the runtime model and native TensorRT path unchanged until a candidate model wins validation and live smoke checks.

The target label semantic is `visible-body`:

- keep visible player/body/enemy/target/person boxes
- drop head-only labels
- drop UI, weapon, objective, minimap, barricade, teammate/friendly-only, and other non-body labels
- remap all kept boxes to one YOLO class: `0 person`

## Manual Download Layout

Download Roboflow datasets manually as YOLO exports and unpack them under a D-drive dataset folder, for example:

```text
D:\datasets\roboflow_candidates\
  deltaforcefiremode\
    data.yaml
    train\
    valid\
    test\
  takov\
    data.yaml
    train\
    valid\
    test\
  warzone_detection\
    data.yaml
    train\
    valid\
    test\
```

Do not unpack large datasets under `C:\Users\...` or the system temp directory.

## Prepare Clean YOLO Dataset

Run from the project root:

```powershell
New-Item -ItemType Directory -Force .tmp,artifacts\ultralytics,artifacts\mpl | Out-Null
$env:TEMP="D:\work\AI\yolo-study-001\.tmp"
$env:TMP="D:\work\AI\yolo-study-001\.tmp"
$env:YOLO_CONFIG_DIR="D:\work\AI\yolo-study-001\artifacts\ultralytics"
$env:MPLCONFIGDIR="D:\work\AI\yolo-study-001\artifacts\mpl"

python tools\prepare_roboflow_visible_body_dataset.py `
  --source deltaforce=D:\datasets\roboflow_candidates\deltaforcefiremode `
  --source takov=D:\datasets\roboflow_candidates\takov `
  --source warzone=D:\datasets\roboflow_candidates\warzone_detection `
  --output-root training_data\assembled\roboflow_visible_body_v1 `
  --force
```

The prepare script uses hardlinks by default when Windows allows it. This avoids copying image bytes within the same drive.

## Train With Disk Loading

Use `--cache false`. Do not use `--cache ram`.

```powershell
python tools\train_person_detector.py `
  --model models\train\best.pt `
  --data training_data\assembled\roboflow_visible_body_v1\dataset.yaml `
  --epochs 4 `
  --imgsz 640 `
  --batch 16 `
  --device 0 `
  --workers 0 `
  --cache false `
  --warmup-epochs 0.0 `
  --close-mosaic 0 `
  --mosaic 0.0 `
  --erasing 0.0 `
  --auto-augment none `
  --scale 0.2 `
  --translate 0.05 `
  --optimizer AdamW `
  --lr0 0.0001 `
  --lrf 0.2 `
  --project runs\person_train `
  --name roboflow_visible_body_v1_e4 `
  --exist-ok
```

## Baseline Numbers

The 2026-05-24 detector baseline is `models/train/best.pt`.

Original mixed Roboflow validation from `cod_polish_640_lowlr_e4_20260524-061012_train_01`:

```text
precision = 0.77082
recall    = 0.72837
mAP50     = 0.77372
mAP50-95  = 0.44946
```

Clean visible-body dataset prepared from the existing local Roboflow exports:

```text
images seen                 = 10843
images written              = 4840
images dropped as duplicate = 4892
images dropped empty        = 1111
boxes seen                  = 9523
boxes written               = 5265
boxes dropped by class      = 4234
boxes dropped by geometry   = 24
```

Baseline `models/train/best.pt` on that clean visible-body val split:

```text
precision = 0.590
recall    = 0.666
mAP50     = 0.612
mAP50-95  = 0.365
```

Four-epoch clean visible-body fine-tune from the same baseline:

```text
precision = 0.86555
recall    = 0.77865
mAP50     = 0.85491
mAP50-95  = 0.50744
cache     = false
workers   = 0
```

Candidate weights:

```text
runs\detect\runs\person_train\roboflow_visible_body_clean_640_e4_20260604\weights\best.pt
```

## Manual Roboflow Candidate Run - 2026-06-04

Manual exports were unpacked under:

```text
D:\datasets\roboflow_candidates\
  DeltaForceFireMode.v15i.yolo26
  R6 Vision Real.v4-v4.yolo26
  Takov.v16i.yolo26
  warzone detection.v8i.yolo26
```

Two disk-backed assembled datasets were prepared:

```text
training_data\assembled\roboflow_manual_core_visible_body_v1
training_data\assembled\roboflow_manual_core_plus_warzone_geo_v1
```

The core dataset used DeltaForce enemy, Takov target, and the small usable R6 enemy slice after class filtering. The plus-warzone dataset added Warzone with explicit class-id keeping plus stricter geometry filters because its class names were not reliable.

Preparation summaries:

```text
roboflow_manual_core_visible_body_v1
images seen                 = 37743
images written              = 7197
images dropped as duplicate = 21324
images dropped empty        = 9222
boxes seen                  = 13813
boxes written               = 8725
boxes dropped by class      = 5077
boxes dropped by geometry   = 11

roboflow_manual_core_plus_warzone_geo_v1
images seen                 = 43637
images written              = 9975
images dropped as duplicate = 23111
images dropped empty        = 10551
boxes seen                  = 21291
boxes written               = 12877
boxes dropped by class      = 5115
boxes dropped by geometry   = 3299
```

Training was run from `models\train\best.pt` with disk loading:

```text
cache   = false
workers = 0
imgsz   = 640
batch   = 16
epochs  = 4
```

Validation comparison:

| Model | Validation dataset | Precision | Recall | mAP50 | mAP50-95 |
| --- | --- | ---: | ---: | ---: | ---: |
| 2026-05-24 baseline | manual core | 0.543 | 0.532 | 0.499 | 0.243 |
| manual core e4 | manual core | 0.880 | 0.819 | 0.900 | 0.620 |
| plus-warzone e4 | manual core | 0.822 | 0.707 | 0.812 | 0.545 |
| 2026-05-24 baseline | manual core + warzone | 0.628 | 0.595 | 0.600 | 0.327 |
| manual core e4 | manual core + warzone | 0.848 | 0.748 | 0.813 | 0.551 |
| plus-warzone e4 | manual core + warzone | 0.845 | 0.760 | 0.865 | 0.582 |
| 2026-05-24 baseline | old clean visible-body | 0.590 | 0.666 | 0.612 | 0.365 |
| manual core e4 | old clean visible-body | 0.832 | 0.719 | 0.800 | 0.413 |
| plus-warzone e4 | old clean visible-body | 0.735 | 0.674 | 0.742 | 0.402 |

Candidate weights:

```text
runs\detect\runs\person_train\manual_core_visible_body_640_e4_20260604\weights\best.pt
runs\detect\runs\person_train\manual_core_plus_warzone_geo_640_e4_20260604\weights\best.pt
```

Current read: the manual core model is the stronger first promotion candidate because it wins on the clean manual validation split and retains more of the old clean visible-body domain. The plus-warzone model is useful evidence that Warzone adds coverage, but the current export is noisy enough that it should stay as a separate experiment unless live footage shows core-only missing Warzone-like poses.

## Baseline Source Split Check - 2026-06-04

The original baseline source datasets still contain mixed labels such as `Body`, `Head`, `enemy`, `MW2_body`, and `body`. These validations used `single_cls=True` to match the baseline training/evaluation convention, so the numbers below are useful for old-distribution retention but should not be read as clean visible-body quality.

Dataset split sizes:

```text
BO7-V1.v10-v6.yolo26              valid = 288, test = 138
COD MW Warzone.v2i.yolo26         valid = 600, test = 0
Cod WZ.v2i.yolo26                 valid = 694, test = 47
```

| Model | Dataset | Split | Precision | Recall | mAP50 | mAP50-95 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-05-24 baseline | BO7 | valid | 0.699 | 0.662 | 0.683 | 0.331 |
| manual core e4 | BO7 | valid | 0.591 | 0.342 | 0.397 | 0.137 |
| plus-warzone e4 | BO7 | valid | 0.463 | 0.353 | 0.355 | 0.110 |
| 2026-05-24 baseline | BO7 | test | 0.702 | 0.621 | 0.705 | 0.333 |
| manual core e4 | BO7 | test | 0.515 | 0.386 | 0.443 | 0.141 |
| plus-warzone e4 | BO7 | test | 0.464 | 0.447 | 0.405 | 0.130 |
| 2026-05-24 baseline | COD MW Warzone | valid | 0.797 | 0.767 | 0.816 | 0.489 |
| manual core e4 | COD MW Warzone | valid | 0.818 | 0.474 | 0.594 | 0.358 |
| plus-warzone e4 | COD MW Warzone | valid | 0.761 | 0.698 | 0.778 | 0.496 |
| 2026-05-24 baseline | Cod WZ | valid | 0.768 | 0.732 | 0.773 | 0.467 |
| manual core e4 | Cod WZ | valid | 0.805 | 0.493 | 0.593 | 0.348 |
| plus-warzone e4 | Cod WZ | valid | 0.759 | 0.656 | 0.737 | 0.460 |
| 2026-05-24 baseline | Cod WZ | test | 0.750 | 0.615 | 0.684 | 0.378 |
| manual core e4 | Cod WZ | test | 0.845 | 0.635 | 0.719 | 0.372 |
| plus-warzone e4 | Cod WZ | test | 0.649 | 0.675 | 0.686 | 0.342 |

Current read: the manual core candidate is good on clean visible-body validation but loses old mixed-label recall on BO7/COD valid splits. The plus-warzone candidate recovers more old Warzone/Cod WZ recall, but still does not cleanly beat the 2026-05-24 baseline on the original valid splits. Keep the baseline engine until live overlay confirms the cleaner body-box model improves practical aim without losing too many short/head-biased old-source detections.

## Conservative Replay Strategy - 2026-06-04

After the manual-core and plus-warzone experiments showed old-distribution regression, three conservative fine-tunes were run from `models\train\best.pt`:

```text
freeze        = 10
lr0           = 0.00005
lrf           = 0.2
epochs        = 4
imgsz         = 640
batch         = 16
cache         = false
workers       = 0
mosaic        = 0.0
auto_augment  = none
scale         = 0.15
translate     = 0.04
```

Candidate weights:

```text
runs\detect\runs\person_train\bo7_only_freeze10_lowlr_640_e4_20260604\weights\best.pt
runs\detect\runs\person_train\old_combined_replay_freeze10_lowlr_640_e4_20260604\weights\best.pt
runs\detect\runs\person_train\old_plus_manual_core_replay_freeze10_lowlr_640_e4_20260604\weights\best.pt
```

The replay+manual-core YAML only references existing D-drive image folders and does not copy image bytes:

```text
training_data\assembled\replay_mix_20260604\old_plus_manual_core_replay.yaml
```

Strategy matrix summary:

| Model | Dataset | Split | Precision | Recall | mAP50 | mAP50-95 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-05-24 baseline | old combined | valid | 0.768 | 0.732 | 0.774 | 0.450 |
| BO7-only freeze10 | old combined | valid | 0.755 | 0.691 | 0.753 | 0.398 |
| old-combined replay freeze10 | old combined | valid | 0.790 | 0.736 | 0.787 | 0.469 |
| old + manual-core replay freeze10 | old combined | valid | 0.760 | 0.711 | 0.774 | 0.449 |
| 2026-05-24 baseline | old combined | test | 0.675 | 0.627 | 0.687 | 0.333 |
| BO7-only freeze10 | old combined | test | 0.699 | 0.639 | 0.675 | 0.351 |
| old-combined replay freeze10 | old combined | test | 0.644 | 0.646 | 0.693 | 0.330 |
| old + manual-core replay freeze10 | old combined | test | 0.742 | 0.563 | 0.671 | 0.298 |
| 2026-05-24 baseline | BO7 | valid | 0.699 | 0.662 | 0.683 | 0.331 |
| BO7-only freeze10 | BO7 | valid | 0.726 | 0.703 | 0.725 | 0.370 |
| old-combined replay freeze10 | BO7 | valid | 0.694 | 0.674 | 0.688 | 0.330 |
| old + manual-core replay freeze10 | BO7 | valid | 0.650 | 0.628 | 0.636 | 0.282 |
| 2026-05-24 baseline | COD MW Warzone | valid | 0.797 | 0.767 | 0.816 | 0.489 |
| old-combined replay freeze10 | COD MW Warzone | valid | 0.822 | 0.785 | 0.829 | 0.513 |
| old + manual-core replay freeze10 | COD MW Warzone | valid | 0.801 | 0.746 | 0.819 | 0.501 |
| 2026-05-24 baseline | Cod WZ | valid | 0.768 | 0.732 | 0.773 | 0.467 |
| old-combined replay freeze10 | Cod WZ | valid | 0.796 | 0.740 | 0.789 | 0.488 |
| old + manual-core replay freeze10 | Cod WZ | valid | 0.769 | 0.722 | 0.789 | 0.479 |
| 2026-05-24 baseline | manual core clean | valid | 0.543 | 0.532 | 0.499 | 0.243 |
| old-combined replay freeze10 | manual core clean | valid | 0.541 | 0.554 | 0.512 | 0.259 |
| old + manual-core replay freeze10 | manual core clean | valid | 0.785 | 0.665 | 0.761 | 0.478 |
| 2026-05-24 baseline | old clean visible-body | valid | 0.590 | 0.666 | 0.612 | 0.365 |
| old-combined replay freeze10 | old clean visible-body | valid | 0.585 | 0.703 | 0.623 | 0.383 |
| old + manual-core replay freeze10 | old clean visible-body | valid | 0.715 | 0.713 | 0.768 | 0.450 |

Current read: `old_combined_replay_freeze10_lowlr_640_e4_20260604` is the safest mainline candidate because it improves the old combined validation score and improves the COD MW Warzone / Cod WZ old-source validations without introducing the larger BO7 regression seen in the clean-body mix. `bo7_only_freeze10_lowlr_640_e4_20260604` proves single-domain replay helps BO7, but it is not a general replacement. `old_plus_manual_core_replay_freeze10_lowlr_640_e4_20260604` is the clean-body branch: much better visible-body validation, but still risky for old BO7-style labels.

## Old Source Head-Masked Eval - 2026-06-04

To check whether clean-body models were being penalized by old `Head/head` labels, a body-only evaluation dataset was created from the three original baseline source datasets. It keeps body/enemy-style labels and drops `Head/head`; images that become empty after filtering are excluded from this scoring set.

```text
training_data\assembled\old_sources_body_only_eval_20260604

images seen                 = 10843
images written              = 9610
images dropped empty        = 1233
boxes seen                  = 19060
boxes written               = 10433
boxes dropped by class      = 8580
boxes dropped by geometry   = 47

val images                  = 1523
test images                 = 165
```

Head-masked old-source results:

| Model | Split | Precision | Recall | mAP50 | mAP50-95 |
| --- | --- | ---: | ---: | ---: | ---: |
| 2026-05-24 baseline | val | 0.576 | 0.681 | 0.618 | 0.378 |
| manual core e4 | val | 0.861 | 0.741 | 0.830 | 0.459 |
| plus-warzone e4 | val | 0.742 | 0.713 | 0.776 | 0.457 |
| old-combined replay freeze10 | val | 0.576 | 0.716 | 0.634 | 0.401 |
| old + manual-core replay freeze10 | val | 0.755 | 0.690 | 0.783 | 0.478 |
| 2026-05-24 baseline | test | 0.659 | 0.583 | 0.603 | 0.300 |
| manual core e4 | test | 0.795 | 0.622 | 0.733 | 0.256 |
| plus-warzone e4 | test | 0.639 | 0.561 | 0.627 | 0.216 |
| old-combined replay freeze10 | test | 0.654 | 0.617 | 0.612 | 0.296 |
| old + manual-core replay freeze10 | test | 0.763 | 0.672 | 0.715 | 0.316 |

Current read: the raw old-source validation was indeed penalizing clean-body candidates for not matching `Head/head` labels. Under head-masked body-only scoring, `manual_core_visible_body_640_e4_20260604` is much stronger than the 2026-05-24 baseline on old-source val and mAP50 test. This supports live-testing manual core e4 as a body-lock candidate, while still keeping the raw old-source matrix for detecting regressions on head/short-peek behavior.

## Body-Only Union Replay - 2026-06-04

After confirming that old `Head/head` labels were depressing only-body candidates, a second body-focused replay dataset was built. It keeps the old sources as body-only labels, preserves images that become empty as hard negatives, and mixes in the manual-core visible-body set twice so the model does not collapse back to the old head-biased distribution.

Training dataset:

```text
training_data\assembled\body_union_20260604\old_body_neg_manual_core_x2.yaml
```

The old-source negative-preserving dataset was prepared as:

```text
training_data\assembled\old_sources_body_only_with_negatives_20260604

images seen                 = 10843
images written              = 10843
boxes seen                  = 19060
boxes written               = 10433
boxes dropped by class      = 8580
boxes dropped by geometry   = 47
```

Training was run from `manual_core_visible_body_640_e4_20260604\weights\best.pt`:

```text
freeze        = 10
lr0           = 0.00002
lrf           = 0.2
epochs        = 6
imgsz         = 640
batch         = 16
cache         = false
workers       = 0
mosaic        = 0.0
auto_augment  = none
scale         = 0.10
translate     = 0.03
```

Candidate weights:

```text
runs\detect\runs\person_train\body_union_manual_core_x2_neg_freeze10_lowlr_640_e6_20260604\weights\best.pt
```

Full validation matrix:

```text
runs\person_val\body_union_candidate_matrix_20260604\summary.csv
```

Selected body-only comparison:

| Model | Dataset | Split | Precision | Recall | mAP50 | mAP50-95 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-05-24 baseline | old body-only | val | 0.576 | 0.681 | 0.618 | 0.378 |
| manual core e4 | old body-only | val | 0.861 | 0.741 | 0.830 | 0.459 |
| body-union x2 neg e6 | old body-only | val | 0.834 | 0.790 | 0.854 | 0.498 |
| 2026-05-24 baseline | old body-only | test | 0.659 | 0.583 | 0.603 | 0.300 |
| manual core e4 | old body-only | test | 0.795 | 0.622 | 0.733 | 0.256 |
| body-union x2 neg e6 | old body-only | test | 0.760 | 0.700 | 0.782 | 0.299 |
| 2026-05-24 baseline | manual core clean | val | 0.543 | 0.532 | 0.499 | 0.243 |
| manual core e4 | manual core clean | val | 0.880 | 0.818 | 0.900 | 0.620 |
| body-union x2 neg e6 | manual core clean | val | 0.881 | 0.809 | 0.899 | 0.612 |
| 2026-05-24 baseline | manual core clean | test | 0.570 | 0.503 | 0.505 | 0.238 |
| manual core e4 | manual core clean | test | 0.864 | 0.831 | 0.896 | 0.606 |
| body-union x2 neg e6 | manual core clean | test | 0.871 | 0.822 | 0.896 | 0.600 |
| 2026-05-24 baseline | body union | val | 0.558 | 0.586 | 0.545 | 0.296 |
| manual core e4 | body union | val | 0.865 | 0.786 | 0.871 | 0.551 |
| body-union x2 neg e6 | body union | val | 0.863 | 0.800 | 0.880 | 0.564 |
| 2026-05-24 baseline | body union | test | 0.570 | 0.521 | 0.514 | 0.244 |
| manual core e4 | body union | test | 0.847 | 0.817 | 0.878 | 0.561 |
| body-union x2 neg e6 | body union | test | 0.858 | 0.812 | 0.883 | 0.565 |

Current read: `body_union_manual_core_x2_neg_freeze10_lowlr_640_e6_20260604` is the strongest body-only candidate so far. Compared with manual core e4, it trades a little old body-only precision for materially better recall and mAP50/mAP50-95 on the old body-only split, while keeping manual-core validation nearly flat. For the no-head target direction, this is a better live-smoke candidate than both the raw old-combined replay model and the direct plus-warzone model.

## Promotion Rule

Do not replace `models/best.engine` from validation metrics alone.

Promote a detector only after:

- clean validation beats the baseline
- original mixed validation does not reveal obvious regressions
- debug overlay shows visible-body boxes, not head/UI/weapon boxes
- native/gamepad live smoke does not become sticky or over-eager
- auto-fire remains gated by strong observed `fire_authority`
