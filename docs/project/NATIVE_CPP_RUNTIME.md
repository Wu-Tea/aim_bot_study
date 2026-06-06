# Native C++ Runtime

This document covers the gamepad live runtime that runs as a single C++
process. It keeps the existing native vision module as the vision
implementation and moves the gamepad controller-side runtime out of Python.

## What It Is

The native C++ runtime is `cod_native_runtime.exe`. It owns the gamepad live
path for:

- native vision polling through the existing `VisionEngine`
- config loading from `config.toml`
- XInput physical gamepad reads
- native gamepad controller state
- `ai_aim`
- auto-fire gate behavior
- aim assist dynamics
- recoil profile selection, calibration, despike, and playback
- downward-pull diagnostics
- perf logging
- ViGEm virtual Xbox 360 output

The default gamepad launch path now starts this native C++ runtime:

```powershell
scripts\launch\gamepad_start.bat
```

The native launcher can also be run directly:

```powershell
scripts\launch\gamepad_native_cpp_start.bat
```

The executable can also be run directly:

```powershell
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log
```

For a one-tick smoke test:

```powershell
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log --once
```

For a pre-acceptance technical readiness check:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check_native_cpp_gamepad_runtime.ps1 -BuildFirst
```

This builds the native project, runs native controller behavior tests, runs a
one-tick runtime smoke, and runs the related Python scaffold/startup regression
tests. It does not replace live gameplay acceptance.

## Scope

This runtime is gamepad-only for the current migration milestone. Mouse and
keyboard-to-gamepad modes remain on their existing Python paths.

Python remains available as the Python fallback for gameplay comparison,
bisecting regressions, training, export, plots, recoil tooling, and debugging.
Use it by setting:

```powershell
$env:GAMEPAD_RUNTIME = "python"
scripts\launch\gamepad_start.bat
```

or in `cmd.exe`:

```bat
set GAMEPAD_RUNTIME=python
scripts\launch\gamepad_start.bat
```

## Native Vision Boundary

The C++ runtime preserves native vision as the source of target information. It
polls `VisionEngine`, receives `VisionResult`, and passes that result directly
into the native gamepad controller in process.

Do not add Python vision work to this path. The runtime should not require
Python packages during gameplay.

## Build

Build the native project before launch:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected output:

```text
native\vision_native\build\Release\cod_native_runtime.exe
```

## Runtime Choices

The C++ launcher prompts for the auto-fire output:

- `RB`
- `RT`
- default from `config.toml`

It also prepares recoil runtime environment variables for the native recoil
profile reader:

- `RECOIL_PROFILE_DIR`
- `RECOIL_CALIBRATION_DIR`
- `RECOIL_RECOGNIZER_STATE_PATH`

The runtime only consumes recognizer state when that state file exists. It does
not start Python weapon OCR or Python vision.

## Exit Behavior

The runtime exits through the configured quit key from `config.toml` and also
handles console stop signals such as Ctrl+C. In both cases the loop requests a
normal shutdown and sends a neutral `GamepadOutputState` to the virtual gamepad
before the process returns.

## Acceptance Checklist

Use this checklist when accepting the C++ launcher as the normal live-play path:

- manual pass-through feels correct
- `ai_aim` acquisition and body-lock feel close enough to the Python fallback
- recoil playback preserves the tuned weapon feel
- `ai_aim` plus recoil overlap does not introduce severe jitter
- auto-fire gate matches the expected authority behavior
- perf stability is acceptable during live play
- process exit neutralizes the virtual gamepad

## Default Launcher

`scripts\launch\gamepad_start.bat` calls
`scripts\launch\gamepad_native_cpp_start.bat` by default.

Keep the Python fallback path in place for comparison and debugging. Set
`GAMEPAD_RUNTIME=python` to use the old Python gamepad runtime.
