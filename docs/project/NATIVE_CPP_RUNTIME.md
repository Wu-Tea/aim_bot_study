# Native C++ Runtime

This document covers the gamepad live runtime that runs as a single C++
process. It keeps the existing native vision module as the vision
implementation and moves the gamepad controller-side runtime out of Python.

This is the authoritative document for the current default gamepad runtime.
When older docs mention a Python gamepad host or a hybrid C++ vision/Python
controller path, treat that as fallback or historical context unless the task
explicitly targets Python.

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

For a pre-acceptance pipeline contract check:

```powershell
scripts\verify\native_pipeline_contract.bat
```

This builds the native controller/runtime/benchmark targets, runs native
controller behavior tests, rejects known recoil/controller coupling patterns,
runs a one-tick runtime smoke, verifies
`tracker_motion=component_aware_final`, and runs a short gamepad benchmark
smoke. It does not replace live gameplay acceptance.

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

## Tracker And Authority

The default tracker backend is `fps_reference`. The runtime records
component-aware final camera motion so tracker ego projection can account for
what the player actually sees while still preserving manual/assist/dynamics/
recoil attribution separately in controller output components.

Available backend config:

```toml
[runtime.gamepad]
tracker_backend = "fps_reference"
# tracker_backend = "legacy_projection"
# tracker_backend = "kalman_experimental"
```

Fire authority remains observed-only. Weak, cue-hold, projected, or coasting
targets may help aim with reduced authority, but they must not grant fire
authority.

## Recoil Boundary

Recoil compensation is now behind `native/recoil_native/RecoilCompensationPolicy`.
The legacy recoil math remains the source of anti-recoil stick output; the new
boundary exposes that output as a separate `recoil_stick` component.

Pipeline contract:

1. Vision/tracker state feeds controller assistance first.
2. Auto-fire decides the fire output.
3. Recoil is the final feed-forward playback stage.
4. Tracker receives the final camera-motion sample for ego projection.

Recoil must not consume target dx/dy, tracker state, target freshness, or
controller correction errors. Do not reintroduce recoil target-direction yield,
pre/post recoil tracker toggles, or controller-to-recoil target feedback without
new evidence and corresponding native contract tests.

The recoil visual displacement model exists but is disabled by default and
returns zero displacement. Do not depend on it for live tracker correction until
it is calibrated with evidence.

## Replay And Benchmark Status

The native replay skeleton defines frame schema, benchmark metric summaries,
and a compile-time runner/writer entry point. Aim perf JSONL logs now include
controller stick components:

- `manual_x`, `manual_y`
- `ai_aim_x`, `ai_aim_y`
- `dynamic_x`, `dynamic_y`
- `recoil_x`, `recoil_y`
- `final_x`, `final_y`
- `tracker_sample_x`, `tracker_sample_y`
- `fire_button`

Full deterministic backend comparison is still pending. The intended comparison
targets are `legacy_projection`, `kalman_experimental`, and detector-only
baseline.

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

The current rollback anchor for the accepted pre-refactor feel is:

```text
f00c338 checkpoint: native controller tuning baseline
```

## Acceptance Checklist

Use this checklist when accepting the C++ launcher as the normal live-play path:

- `scripts\verify\native_pipeline_contract.bat` passes
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
