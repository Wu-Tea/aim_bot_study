# Model Artifacts

This folder contains local model artifacts used by the runtime, training helpers, and older benchmark history.

## Current Runtime Defaults

| File | Status | Used by |
| --- | --- | --- |
| `best.engine` | Current native / TensorRT runtime engine | `config.toml`, `vision/runner.py`, `vision/native_runner.py`, native C++ TensorRT runtime |
| `best.pt` | Current Python fallback model | Python vision fallback and model export/debug paths |

## Training And Export Inputs

| File | Status | Used by |
| --- | --- | --- |
| `yolo26n.pt` | Training/export base model | `tools/train_person_detector.py`, `tools/export_trt.py`, `docs/project/PERSON_DETECTOR_TRAINING.md` |
| `last.pt` | Training checkpoint | Manual recovery/export workflows; not a default runtime model |

## Historical / Reference Artifacts

| File | Status | Notes |
| --- | --- | --- |
| `best.onnx` | Export artifact for the current detector family | Keep while native/TensorRT export history is useful. |
| `yolo26n.engine` | Older detector engine | Referenced by older worklog/training context. |
| `yolo26n.onnx` | Older detector ONNX | Reference/export artifact. |
| `yolo26n-pose.pt` | Older pose-model baseline | Referenced by historical performance notes. |
| `yolo26n-pose.onnx` | Older pose-model ONNX | Reference/export artifact. |
| `yolo26n-pose.engine` | Older pose-model TensorRT engine | Referenced by historical performance notes. |

## Cleanup Rule

Do not delete model files based only on age. First check:

```powershell
git grep -n "model-file-name"
```

If a model is no longer referenced by runtime, tools, docs, or tests, move it out of the repo or replace it with a documented download/export step.
