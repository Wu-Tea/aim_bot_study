# yolo-study-001

Windows-focused YOLO aim-assist study project with a hybrid runtime:

- Python still owns startup, configuration, controller orchestration, debug tooling, and most tests.
- Native C++ now owns the hot vision path for the current default gamepad runtime.
- Python vision remains available as a fallback and comparison path through `--vision-backend python`.

## Current status

- `main.py` is the single launcher for all controller modes.
- `gamepad_start.bat` currently defaults to `VISION_BACKEND=native`.
- `gamepad_debug.bat` and `gamepad_native_debug.bat` expose native-gamepad debug entry points.
- `mouse_start.bat` and `mouse_native_debug.bat` provide the native mouse-output path.
- Native vision lives in `native/vision_native/` and is bridged back into Python through `vision/native_runner.py`.
- The mouse path continues to evolve and is less settled than the main gamepad path.

## Quick start

### Python environment

```powershell
py -3.11 -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
Copy-Item config.toml.example config.toml
```

Notes:

- The repo is Windows-oriented.
- `config.toml` is intentionally gitignored so local tuning stays private.
- `requirements.txt` covers Python packages only. CUDA, TensorRT, Visual Studio C++ tools, and pybind11-backed native build requirements are separate.

### Native vision prerequisites

Native vision requires a local Windows CUDA + TensorRT toolchain. The current build script defaults are:

- `CUDA_PATH = C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1`
- `TensorRT_ROOT = D:\env\TensorRT-10.15.1.29`
- `Python = D:\env\python\python.exe`

Expected tools/components:

- Visual Studio 2022 C++ build tools
- CUDA Toolkit 13.1
- TensorRT 10.15.1.29 Windows SDK
- `pybind11` installed in the Python environment used for the native build

Build the native module with:

```powershell
.\tools\build_native_vision.ps1
```

Useful smoke and debug scripts:

```powershell
.\tools\run_native_vision_smoke.ps1 -BuildFirst
.\tools\run_native_vision_infer_smoke.ps1 -BuildFirst
.\tools\run_native_vision_capture_smoke.ps1 -BuildFirst
.\tools\run_native_vision_debug.ps1 -BuildFirst
```

## Startup scripts

These scripts are the fastest way to use the project without remembering CLI flags.

| Script | Purpose | Current behavior |
| --- | --- | --- |
| `gamepad_start.bat` | Main gamepad runtime | Uses `config.toml` / `config.toml.example` runtime defaults, lets existing `VISION_*` env vars override them, and prompts for an optional `RB` / `RT` CLI override |
| `gamepad_debug.bat` | Gamepad debug runtime | Prompts for `RB` / `RT` and native vs Python backend, enables `--vision-debug --vision-debug-save`, defaults `VISION_CAPTURE_FPS=140` |
| `gamepad_native_debug.bat` | Force native gamepad debug | Native-only debug entry with `--vision-debug`, defaults `VISION_CAPTURE_FPS=140` |
| `mouse_start.bat` | Main native mouse runtime | Uses `--controller-mode mouse`, defaults to native backend, enables perf log, defaults `VISION_CAPTURE_FPS=140` |
| `mouse_native_debug.bat` | Native mouse debug runtime | Mouse path with `--vision-debug --vision-debug-save`, defaults `VISION_CAPTURE_FPS=140` |
| `start.bat` | Older minimal launcher | Legacy helper; not the preferred entry point anymore |

Equivalent direct CLI examples:

```powershell
py -3.11 main.py --controller-mode gamepad
py -3.11 main.py --controller-mode gamepad --vision-backend native --perf-log
py -3.11 main.py --controller-mode gamepad --vision-backend python --vision-debug
py -3.11 main.py --controller-mode mouse --vision-backend native --vision-debug --vision-debug-save --perf-log
```

## TensorRT and model notes

- Default runtime engine: `models/best.engine`
- Default Python fallback model: `models/best.pt`
- Training / export helper: `tools/export_trt.py`
- Native module output location: `native/vision_native/build/Release`

The native runtime loader in `vision/native_runner.py` automatically tries to:

- add the native build directory to `sys.path`
- prepend TensorRT and CUDA `bin` folders to `PATH`
- load `vision_native_cpp` from `native/vision_native/build/Release`

If the native module is missing, the expected recovery path is:

1. Build with `.\tools\build_native_vision.ps1`
2. Retry one of the native startup scripts
3. Fall back to `--vision-backend python` if you only need the Python path

## Repository structure

```text
config/                  Config loader and example tuning surfaces
controllers/             Controller hosts and per-mode plugin logic
controllers/gamepad/     Gamepad plugin stack and support modules
controllers/mouse/       Mouse plugin stack and support modules
docs/project/            Project-facing architecture, benchmark, and status docs
docs/superpowers/        Historical specs and implementation plans
models/                  YOLO `.pt`, `.onnx`, and TensorRT `.engine` artifacts
native/vision_native/    C++ / CUDA / TensorRT native vision runtime
tests/                   Python test suite
tools/                   Build, export, benchmark, training, and smoke scripts
training/                Dataset helpers for detector training
vision/                  Python vision backend, debug tools, and native bridge
controller.py            Controller factory
main.py                  Unified CLI entry point
```

## Runtime architecture

At a high level:

1. `main.py` parses CLI / env overrides.
2. `controller.py` builds one controller host:
   - `gamepad`
   - `mouse`
   - `kbm_to_gamepad`
3. Vision runs through one backend:
   - `vision/runner.py` for the Python backend
   - `vision/native_runner.py` for the native backend
4. Vision only hands compact deltas and target metadata to the controller layer.
5. Controllers own input reading, AI/manual mixing, auto-fire actuation, and final device output.

Important code entry points:

- Python backend: `vision/runner.py`
- Native backend bridge: `vision/native_runner.py`
- Native C++ engine: `native/vision_native/src/vision_engine.cpp`
- Controller base contract: `controllers/base_controller.py`
- Gamepad host: `controllers/gamepad_controller.py`
- Mouse host: `controllers/mouse_controller.py`

## Documentation map

Read these first:

1. `docs/project/README.md`
2. `docs/project/WORKLOG.md`
3. `docs/project/NATIVE_VISION.md`
4. `docs/project/CONTROLLER_OVERVIEW.md`
5. `docs/project/VISION_OVERVIEW.md`

Useful project docs:

- `docs/project/GAMEPAD_OVERVIEW.md`
- `docs/project/MOUSE_OVERVIEW.md`
- `docs/project/GAMEPAD_BENCHMARKS.md`
- `docs/project/GAMEPAD_ADS_BENCHMARKS.md`
- `docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md`
- `docs/project/PERSON_DETECTOR_TRAINING.md`
- `docs/project/PERF_PLAN.md`
- `docs/project/TRACKING.md`

Historical design / execution history:

- `docs/superpowers/specs/`
- `docs/superpowers/plans/`

## Suggested verification commands

General CLI and startup coverage:

```powershell
python -m unittest tests.test_main_cli tests.test_startup_scripts -v
```

Native vision bridge and parity:

```powershell
python -m unittest tests.test_native_vision_runner tests.test_native_vision_synthetic_parity -v
```

Controller-focused suites:

```powershell
python -m unittest discover -s tests/gamepad -p "test_*.py" -v
python -m unittest discover -s tests/mouse -p "test_*.py" -v
```
