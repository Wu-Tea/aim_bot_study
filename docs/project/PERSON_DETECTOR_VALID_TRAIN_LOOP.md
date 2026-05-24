# Person Detector Valid/Train Loop

This workflow is for repeatable COD one-class detector validation and conservative fine-tuning. The detector is trained from local YOLO data under `models/train` and is intended for `native-vision` style runtime use.

## Goal

The important part of this workflow is not one aggressive training command. The goal is to make every baseline validation, training pass, and post-training validation comparable while avoiding Windows multiprocessing, stdin entrypoint, RAM cache, and pagefile problems.

## Stability Rules

- Do not launch Ultralytics valid/train from `python -c`, stdin, or notebook cells.
- Use real script entrypoints: `tools/validate_person_detector.py`, `tools/train_person_detector.py`, and `tools/run_person_detector_loop.py`.
- Keep `workers=0` on Windows by default. Try higher workers only after the flow is stable.
- Keep `cache=false` by default. Do not use `--cache ram` on large datasets unless memory pressure is understood.
- Use separate run names for baseline validation, every training loop, and every trained-model validation.
- Loop logs are written under `artifacts/person_detector_loops/<run_id>/`.
- Prove the flow first with `imgsz=640` and short epochs before moving to `imgsz=896` or longer training.

## Baseline Validation

```powershell
py -3 tools\validate_person_detector.py `
  --model models\train\best.pt `
  --data models\train\cod_combined_single_cls.yaml `
  --imgsz 640 `
  --batch 16 `
  --workers 0 `
  --name baseline_train_best_cod_combined_640 `
  --plots
```

Default output:

```text
runs/person_val/baseline_train_best_cod_combined_640
```

## Conservative Training

```powershell
py -3 tools\train_person_detector.py `
  --model models\train\best.pt `
  --data models\train\cod_combined_single_cls.yaml `
  --epochs 5 `
  --imgsz 640 `
  --batch 8 `
  --workers 0 `
  --cache false `
  --name cod_combined_safe_640_e5
```

Default output:

```text
runs/person_train/cod_combined_safe_640_e5
```

## Automated Loop

Run a short loop first:

```powershell
py -3 tools\run_person_detector_loop.py `
  --model models\train\best.pt `
  --data models\train\cod_combined_single_cls.yaml `
  --loops 2 `
  --epochs-per-loop 1 `
  --train-imgsz 640 `
  --val-imgsz 640 `
  --batch 8 `
  --workers 0 `
  --cache false `
  --run-prefix cod_safe_loop `
  --plots
```

The flow is:

1. Validate the starting model.
2. Train loop 1.
3. Validate loop 1 `best.pt`.
4. Continue loop 2 from the previous `best.pt`.
5. Validate loop 2 `best.pt`.

Each step writes an independent log:

```text
artifacts/person_detector_loops/cod_safe_loop_YYYYMMDD-HHMMSS/
  01_baseline_val.log
  02_train_01.log
  03_val_01.log
  04_train_02.log
  05_val_02.log
  summary.json
```

## Full Training Candidate

After the short loop is stable, raise training strength carefully:

```powershell
py -3 tools\run_person_detector_loop.py `
  --model models\train\best.pt `
  --data models\train\cod_combined_single_cls.yaml `
  --loops 1 `
  --epochs-per-loop 30 `
  --train-imgsz 896 `
  --val-imgsz 640 `
  --batch 12 `
  --workers 0 `
  --cache false `
  --run-prefix cod_combined_from_best_v1 `
  --plots
```

If GPU and memory behavior remain stable, try:

```powershell
--workers 2
```

Do not restore `--cache ram` by default.

## Stuck-Process Checks

A healthy run should show at least one of these signs:

- Python CPU time keeps increasing.
- `runs/person_train/...` or `runs/person_val/...` receives new files.
- GPU compute activity continues.
- The loop log keeps appending output.

If a directory does not change for 10 minutes, CPU time does not increase, and the log is stuck on one line, stop the process and inspect the step log.
