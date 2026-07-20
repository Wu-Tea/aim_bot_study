# yolo-study-001

Windows-focused YOLO aim-assist study project. The default live gamepad path is
now a full native C++ runtime:

- `scripts\launch\gamepad_start.bat` defaults to `GAMEPAD_RUNTIME=native`.
- The native path starts `native\vision_native\build\Release\cod_native_runtime.exe`.
- C++ owns live gamepad capture, TensorRT vision, target selection, controller
  state, `ai_aim`, auto-fire, aim-assist dynamics, recoil playback, and ViGEm
  output.
- Python remains available mainly for fallback, training/export, recoil tooling,
  debug utilities, and tests.

## Current status

- `scripts\launch\gamepad_start.bat` is the normal gamepad entry point and
  calls `scripts\launch\gamepad_native_cpp_start.bat` by default.
- Set `GAMEPAD_RUNTIME=python` only when you explicitly need the older Python
  gamepad fallback.
- `main.py` remains the launcher for Python fallback modes, mouse mode,
  keyboard/mouse-to-gamepad mode, and tooling-oriented debug paths.
- `scripts\launch\debug\gamepad_debug.bat` and `scripts\launch\debug\gamepad_native_debug.bat` expose older Python-hosted native-vision debug entry points.
- `scripts\launch\mouse_start.bat` and `scripts\launch\debug\mouse_native_debug.bat` provide the native mouse-output path.
- Native vision and the production native gamepad runtime live under
  `native/vision_native/`, `native/runtime_app/`, and `native/controller_native/`.
- The mouse path continues to evolve and is less settled than the main gamepad path.

## Quick start

### Python environment

```powershell
py -3.11 -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

Notes:

- The repo is Windows-oriented.
- Runtime reads the local project-root `config.toml` directly. The file is intentionally gitignored so machine-specific tuning stays private; when it is absent, code defaults are used.
- `requirements.txt` covers Python packages only. CUDA, TensorRT, Visual Studio C++ tools, and pybind11-backed native build requirements are separate.

### Native C++ runtime prerequisites

The native C++ gamepad runtime requires a local Windows CUDA + TensorRT
toolchain. The current build script defaults are:

- `CUDA_PATH = C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1`
- `TensorRT_ROOT = D:\env\TensorRT-10.15.1.29`
- `Python = D:\env\python\python.exe`

Expected tools/components:

- Visual Studio 2022 C++ build tools
- CUDA Toolkit 13.1
- TensorRT 10.15.1.29 Windows SDK
- `pybind11` installed in the Python environment used for the native build

Build the native module and runtime executable with:

```powershell
.\tools\build_native_vision.ps1
```

Useful smoke and debug scripts:

```powershell
.\scripts\verify\native_pipeline_contract.bat
.\tools\check_native_cpp_gamepad_runtime.ps1 -BuildFirst
.\tools\run_native_vision_smoke.ps1 -BuildFirst
.\tools\run_native_vision_infer_smoke.ps1 -BuildFirst
.\tools\run_native_vision_capture_smoke.ps1 -BuildFirst
.\tools\run_native_vision_debug.ps1 -BuildFirst
```

Use `scripts\verify\native_pipeline_contract.bat` before live acceptance when
changing the native `vision -> tracker -> controller -> recoil` path. It checks
known recoil/controller coupling regressions, runs native controller tests,
runs a one-tick runtime smoke, verifies `tracker_motion=component_aware_final`,
and runs a short native gamepad benchmark smoke. The older
`tools\check_native_cpp_gamepad_runtime.ps1` remains useful for broader runtime
launcher/scaffold checks and now calls the core pipeline contract unless
`-SkipPipelineContract` is supplied.

## Startup scripts

These scripts are the fastest way to use the project without remembering CLI flags.
Launcher implementations live under `scripts/launch/`; legacy/manual helpers live under `scripts/legacy/`.
The old root `.bat` compatibility shims have been removed.

| Script | Purpose | Current behavior |
| --- | --- | --- |
| `scripts\launch\gamepad_start.bat` | Main gamepad runtime | Defaults to full native C++ gamepad runtime; set `GAMEPAD_RUNTIME=python` for the old Python fallback |
| `scripts\launch\gamepad_native_cpp_start.bat` | Direct native C++ gamepad runtime | Starts `cod_native_runtime.exe`, prompts for recoil profile selection and `RB` / `RT` override |
| `scripts\launch\debug\gamepad_debug.bat` | Python-hosted gamepad debug runtime | Prompts for `RB` / `RT` and native vs Python vision backend, enables `--vision-debug --vision-debug-save`, defaults `VISION_CAPTURE_FPS=140` |
| `scripts\launch\debug\gamepad_native_debug.bat` | Python-hosted native-vision debug | Forces native vision through the Python controller bridge with `--vision-debug`, defaults `VISION_CAPTURE_FPS=140` |
| `scripts\launch\mouse_start.bat` | Main native mouse runtime | Uses `--controller-mode mouse`, defaults to native backend, enables perf log, defaults `VISION_CAPTURE_FPS=140` |
| `scripts\launch\debug\mouse_native_debug.bat` | Native mouse debug runtime | Mouse path with `--vision-debug --vision-debug-save`, defaults `VISION_CAPTURE_FPS=140` |
| `scripts\launch\recoil_app_start.bat` | Standalone recoil app | Recognition, recording, and status debugging entry point |
| `scripts\legacy\recoil_toolkit.bat` | Legacy recoil helper | Older manual recoil workflows; prefer the recoil app for current testing |

Equivalent direct CLI examples:

```powershell
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log
$env:GAMEPAD_RUNTIME = "python"
py -3.11 main.py --controller-mode gamepad --vision-backend native --perf-log
py -3.11 main.py --controller-mode gamepad --vision-backend python --vision-debug
py -3.11 main.py --controller-mode mouse --vision-backend native --vision-debug --vision-debug-save --perf-log
```

## TensorRT and model notes

- Default native runtime engine: `models/best.engine`
- Default Python fallback model: `models/best.pt`
- Training / export helper: `tools/export_trt.py`
- Native module and runtime output location: `native/vision_native/build/Release`

The Python fallback loader in `vision/native_runner.py` automatically tries to:

- add the native build directory to `sys.path`
- prepend TensorRT and CUDA `bin` folders to `PATH`
- load `vision_native_cpp` from `native/vision_native/build/Release`

If the native module is missing, the expected recovery path is:

1. Build with `.\tools\build_native_vision.ps1`
2. Retry `scripts\launch\gamepad_start.bat`
3. Set `GAMEPAD_RUNTIME=python` or `--vision-backend python` only if you need the fallback path

## Repository structure

```text
config/                  Config loader and example tuning surfaces
controllers/             Controller hosts and per-mode plugin logic
controllers/gamepad/     Gamepad plugin stack and support modules
controllers/mouse/       Mouse plugin stack and support modules
docs/project/            Project-facing architecture, benchmark, and status docs
docs/methods/            Reusable cross-project engineering methods and start-here guides
docs/superpowers/        Historical specs and implementation plans
models/                  YOLO `.pt`, `.onnx`, and TensorRT `.engine` artifacts
native/vision_native/    C++ / CUDA / TensorRT native vision runtime
native/runtime_app/      Full native C++ gamepad runtime executable
native/controller_native/ Native C++ gamepad controller, recoil, input, and ViGEm output
scripts/launch/          Full launcher script implementations
scripts/legacy/          Older/manual launcher helpers kept for compatibility
tests/                   Python test suite
tools/                   Build, export, benchmark, training, and smoke scripts
training/                Dataset helpers for detector training
vision/                  Python vision backend, debug tools, and native bridge
controllers/factory.py   Controller factory
controller.py            Compatibility shim exporting ControllerFactory
main.py                  Unified CLI entry point
```

## Runtime architecture

At a high level, the default gamepad runtime is:

1. `scripts\launch\gamepad_start.bat` selects `GAMEPAD_RUNTIME=native`.
2. `scripts\launch\gamepad_native_cpp_start.bat` starts `cod_native_runtime.exe`.
3. `native/runtime_app` reads config and physical gamepad input.
4. `native/vision_native` captures the ROI and runs TensorRT + target selection.
5. `native/controller_native` applies `ai_aim`, auto-fire, aim-assist dynamics,
   recoil, and final ViGEm output.

The Python architecture still exists for fallback and non-gamepad modes:

1. `main.py` parses CLI / env overrides.
2. `controllers/factory.py` builds a Python controller host.
3. Vision runs through `vision/runner.py` or `vision/native_runner.py`.
4. Python controllers own input reading, AI/manual mixing, auto-fire actuation,
   and final device output.

Important code entry points:

- Default native runtime: `native/runtime_app/main.cpp`
- Native runtime loop: `native/runtime_app/runtime_loop.cpp`
- Native gamepad controller: `native/controller_native/native_gamepad_controller.cpp`
- Native vision engine: `native/vision_native/src/vision_engine.cpp`
- Python fallback backend: `vision/runner.py`
- Python native-vision bridge: `vision/native_runner.py`
- Controller base contract: `controllers/base_controller.py`
- Python fallback gamepad host: `controllers/gamepad_controller.py`
- Mouse host: `controllers/mouse_controller.py`

## Documentation map

Read these first:

1. `docs/project/README.md`
2. `docs/project/WORKLOG.md`
3. `docs/project/NATIVE_CPP_RUNTIME.md`
4. `docs/project/NATIVE_VISION.md`
5. `docs/project/CONTROLLER_OVERVIEW.md`
6. `docs/project/VISION_OVERVIEW.md`

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
