# Gamepad Overview

Last updated: 2026-06-11

## Goal

The current default gamepad path is a full native C++ runtime that:

- reads a physical gamepad
- mirrors it to a virtual Xbox 360 controller
- mixes controller-local AI assistance into the output
- keeps the vision boundary compact

This is the most mature assisted-play path in the repository today. The older
Python `GamepadController` path remains available as a fallback and historical
reference, but it is not the normal live runtime.

## Main Files

- `native/runtime_app/main.cpp`
- `native/runtime_app/runtime_loop.cpp`
- `native/runtime_app/perf_logger.cpp`
- `native/controller_native/native_gamepad_controller.cpp`
- `native/controller_native/ai_aim.cpp`
- `native/controller_native/auto_fire` behavior inside `native_gamepad_controller.cpp`
- `native/controller_native/aim_assist_dynamics.cpp`
- `native/controller_native/recoil_compensation.cpp`
- `native/controller_native/target_tracker.cpp`
- `native/controller_native/xinput_reader.cpp`
- `native/controller_native/sdl_gamepad_reader.cpp`
- `native/controller_native/virtual_gamepad.cpp`
- `native/vision_native/src/vision_engine.cpp`

Python fallback and reference files:

- `controllers/gamepad_controller.py`
- `controllers/gamepad/state.py`
- `controllers/gamepad/plugin.py`
- `controllers/gamepad/ai_aim.py`
- `controllers/gamepad/auto_fire.py`
- `controllers/gamepad/aim_assist_dynamics.py`
- `controllers/gamepad/recoil_compensation.py`
- `controllers/gamepad/diagnostics.py`

Support and legacy modules still present in the folder:

- `adaptive_delta_gain.py`
- `horizontal_assist.py`
- `manual_intent_guard.py`
- `overshoot_guard.py`
- `legacy_ai_aim.py`

Those support files still matter for experiments, old benchmark context, and implementation history, but they are not part of the default C++ runtime.

## Runtime Flow

Default C++ path:

1. `scripts\launch\gamepad_start.bat` defaults to `GAMEPAD_RUNTIME=native`.
2. `scripts\launch\gamepad_native_cpp_start.bat` starts `cod_native_runtime.exe`.
3. `RuntimeLoop` reads physical gamepad input and polls `VisionEngine` while aiming.
4. `VisionEngine` emits `VisionResult` with target delta, target metadata, authority fields, auto-fire intent, and timing fields.
5. `NativeGamepadController` builds output from physical input plus the latest vision result.
6. Native `ai_aim`, auto-fire, aim-assist dynamics, recoil, and diagnostics mutate the output.
7. The final output is written through ViGEm.

Python fallback path:

1. Set `GAMEPAD_RUNTIME=python`.
2. `main.py` asks `ControllerFactory` for `gamepad`.
3. `GamepadController` opens the physical pad through `pygame` and the virtual pad through `vgamepad`.
4. Vision sends `ControllerVisionState` into the Python plugin chain.

## Host Responsibilities

`NativeGamepadController` is the default host, not the place for feature sprawl.

Current native host responsibilities are:

- initialize native input readers
- initialize ViGEm output
- read sticks, triggers, buttons, and D-pad
- track aiming state from the left trigger
- store the latest vision delta and target metadata
- store auto-fire intent with the same source timestamp discipline as target aim
- build native output state from physical passthrough values
- run native controller stages
- write the final virtual-controller state

The host intentionally preserves manual stick values before plugins run. It does not silently deadzone away small right-stick inputs at the host layer.

## Shared Data Structures

Default C++ state is defined under `native/controller_native/`:

- `PhysicalGamepadState`
- `GamepadOutputState`
- `NativeControllerVisionState`
- `NativeControllerStageTrace`

Python fallback state is defined in `controllers/gamepad/state.py`:

- `GamepadFrame`
  - immutable snapshot of one controller frame
  - includes manual input, aiming state, latest vision signal, auto-fire request, and optional `ControllerTarget`
- `GamepadOutput`
  - mutable output buffer written by plugins before final device output

`ControllerTarget` currently carries:

- aim point
- screen center
- optional `body_box`

This extra target metadata is what allows controller-side `Body Lock` behavior without moving final actuation logic into vision.

## Current Plugin Chain

The default native controller order is:

1. native AI aim
2. native auto-fire readiness/gate
3. native aim-assist dynamics
4. native recoil compensation
5. ViGEm output

Native tracker/controller/recoil contract:

- vision/tracker state feeds controller assistance before recoil
- recoil is the final feed-forward playback stage
- recoil does not consume target dx/dy, tracker state, target freshness, or controller correction errors
- tracker receives component-aware final camera motion for ego projection while `manual`, `assist`, `dynamics`, `recoil`, and `final` output components remain separately attributed
- run `scripts\verify\native_pipeline_contract.bat` after native controller/tracker/recoil changes

The Python fallback plugin chain created in `GamepadController.__init__` is:

1. `AIAimPlugin`
2. `AutoFirePlugin`
3. `AimAssistDynamicsPlugin`
4. `RecoilCompensationPlugin`

Optional diagnostics:

- `DownwardPullDiagnostics` can record plugin traces when enabled through env vars

## Current AI Aim Behavior

Native AI aim is the default live implementation. `AIAimPlugin` is the Python
fallback implementation and tuning reference. Both follow the same practical
mode model.

Primary modes:

- `manual`
  - passthrough baseline when there is no active assistance window
- `ads_snap`
  - short first-ADS reposition window
- `body_lock`
  - sticky upper-body correction when the crosshair is already close and the target body box supports a lock

Important details:

- `ads_snap_window_ms` defaults to `100`
- body lock only engages when the crosshair is already inside the target body box plus tolerance
- body lock uses upper-body aim instead of generic box center
- body lock has controller-side motion compensation after continuity is established
- body lock also has a near-lock lateral motion assist: when the upper-body lock point is moving quickly and the current X error is inside the release/deadzone window, it applies a short velocity lead and preserves a stronger horizontal tail so side-running targets do not get a zero-output frame right as they cross the reticle
- manual input is not simply overwritten
  - same-direction help can be preserved
  - harmful opposing input can be suppressed
  - orthogonal wobble can be damped near lock
- near zero-crossing, axis guards intentionally hold correction briefly to reduce oscillation

Current tuning comes from:

- code defaults in `native/controller_native/runtime_config.h`
- matching Python fallback defaults in `controllers/gamepad/ai_aim.py`
- optional overrides from `config.toml` under `[gamepad.ai_aim]`

Useful near-lock lateral knobs:

- `body_lock_lateral_motion_min_speed_px_per_sec`
- `body_lock_lateral_motion_lead_seconds`
- `body_lock_lateral_motion_lead_window_px`
- `body_lock_lateral_motion_lead_max_px`
- `body_lock_lateral_motion_tail_scale`

## AutoFire

Native auto-fire converts vision fire intent into actual controller output in
the default runtime. `AutoFirePlugin` is the Python fallback implementation.

Current behavior:

- `aim_only = True` by default
- auto-fire intent is ignored when its source frame is stale
- manual RB/RT input during auto-fire gets a short release/resume guard so user input is not swallowed
- final output can be either:
  - `RB`
  - `RT`

Current source of that choice:

- CLI: `--auto-fire-output`
- `scripts\launch\gamepad_start.bat` prompt
- `config.toml` default under `[runtime.gamepad]`

Freshness and manual takeover timing are configured under `[gamepad.auto_fire]`.

## Recoil Compensation

Native recoil compensation now supports two modes in the default runtime:

- legacy fixed-pull fallback when no recoil-profile provider is wired in
- profile-driven curve playback when the host can resolve an active recoil profile

The profile-driven path:

- reads the current active profile from the native recognizer state path and profile directory
- advances the curve only while `auto_fire_active` or manual `RT/RB` fire input is active
- treats stored recoil samples as collector screen-response curves; when a matching game/aim/stance calibration exists, per-sample recoil deltas use that measured mapping, otherwise playback uses the older uncalibrated pixel-to-stick mapping for trial use
- scales profile playback with `[gamepad.recoil].profile_amount`
- applies signed extra horizontal profile strength with `[gamepad.recoil].profile_x_amount`; X uses per-sample horizontal changes instead of amplifying the cumulative X curve, and negative values invert horizontal correction for direction checks
- advances profile playback with `[gamepad.recoil].profile_lead_ms` to compensate gamepad/runtime/game input timing delay
- uses `[gamepad.recoil].feedback_amount` only as the fixed fallback down-pull when no matching profile is available
- resets playback when firing stops or the active weapon profile changes or disappears

The current native host keeps the integration conservative:

- if `RECOIL_PROFILE_DIR` and `RECOIL_RECOGNIZER_STATE_PATH` are both available, the host enables profile-driven recoil
- if those paths are not configured, the host keeps the fixed fallback configured under `[gamepad.recoil]` (default `feedback_amount = 0.20`)
- recoil playback is independent from target/tracker/controller correction state; tune controller assistance and recoil profile playback separately
- low `confidence` is kept as a diagnostic for magazine-curve profiles, but it does not block runtime use by itself
- magazine profile quality findings such as low support, recovery tail, or horizontal disagreement are audit/status diagnostics only; if the weapon id, stance, and aim mode match, runtime trial playback uses the profile
- missing calibration no longer blocks magazine-curve trial playback, but logs/status will mark that ready mode as uncalibrated
- lower `[gamepad.recoil].profile_amount` if the uncalibrated profile trial is too strong vertically; tune `[gamepad.recoil].profile_x_amount` in small positive or negative steps if impacts still jump left/right; adjust `[gamepad.recoil].profile_lead_ms` if compensation feels late or early, then replace trial mapping with measured calibration when available

The current native host now supports a deliberately narrow recoil-recognition shortcut for direct use:

- if `RECOIL_SWITCH_RECOGNITION_MODE=y_button_text`, `RECOIL_GAME`, `RECOIL_SIGNATURE_DIR`, and `RECOIL_RECOGNIZER_STATE_PATH` are present, the controller listens for `Y` rising edges and performs a short text-only OCR burst against the configured weapon-name ROI
- the controller does not trust a previous slot result on `Y`; each switch starts a fresh OCR capture, while a monotonic `switch_epoch` prevents stale OCR results from winning
- the controller still does not run continuous recognition; it only refreshes weapon identity when you switch with `Y`

## Recoil App Workflow

The new primary recoil path is `recoil_app`, which now supports two runtime modes:

- `record`
  - `Y` switch recognition
  - auto-create weapon identities from OCR names
  - `RT/RB` burst-driven recoil learning
  - recoil plot generation
- `recoil`
  - `Y` switch recognition
  - in-memory cached profile lookup
  - `[gamepad.recoil].profile_amount` / `profile_x_amount` / `profile_lead_ms` profile playback, with `[gamepad.recoil].feedback_amount` used only for fixed fallback when no matching profile exists

For the main native AI-aim runtime, the C++ launcher prepares profile paths and
the native controller reads recognizer state directly. You do not need to run a
separate recoil sidecar process for normal use.

Use `docs/project/RECOIL_RECORD_REPLAY_VALIDATION.md` as the current checklist before trusting a recorded profile for live gamepad recoil compensation.

Recommended paths:

1. Standalone testing:
   - `scripts\launch\recoil_app_start.bat`
   - or `python -m recoil_app --game cod22 --mode record`
   - or `python -m recoil_app --game cod22 --mode recoil`
2. Main native AI-aim runtime:
   - set `ENABLE_RECOIL_RUNTIME=1` or choose `1. On` in the launcher
   - optionally set `RECOIL_GAME`, `RECOIL_PROFILE_DIR`, `RECOIL_CALIBRATION_DIR`, `RECOIL_WEAPON_DIR`, and `RECOIL_RECOGNIZER_STATE_PATH`
   - then start the normal gamepad runtime

Collector behavior in the current direct-use flow:

- if you pass `--canonical-weapon-id`, the collector skips live HUD weapon confirmation completely
- `--startup-delay` exists only to give you time to switch back into the game before capture starts
- firing burst boundaries come from the shared physical-gamepad reader:
  - `RT/R2` or `RB/R1` pressed => burst start
  - both released => burst end
- recoil motion is still measured from the center capture window, but burst segmentation no longer has to guess start/stop from motion alone

Runtime files and directories:

- lightweight recoil-app weapon identities live under `artifacts/recoil_app/weapons/`
- recoil profiles live under `artifacts/recoil_profiles/`
- recoil plots live under `artifacts/recoil_plots/`
- live debug state defaults to `artifacts/recoil_app/current_weapon.json`
- screenshot examples for ROI tuning may live under `artifacts/weapon_examples/`, but those are local test assets and should not be committed

Runtime behavior in this direct-use path:

- `record` mode never exposes compensation; it only learns and saves assets
- `recoil` mode is still useful for Python fallback/debug work; the default native runtime reads profile/state files directly instead of importing the Python recoil app
- standalone `scripts\launch\recoil_app_start.bat` does not create or update a virtual gamepad; it is for recognition, recording, and status debugging
- the main native gamepad runtime selects profiles from the configured profile directory and recognizer state path
- repeated full-magazine recordings are kept as raw episodes under `artifacts/recoil_profiles/_episodes/`
- the fitted profile exposed to runtime is a single `*-current.json` file per weapon, stance, and aim mode
- recoil plots are written after each successful recording as final-profile trajectory images: `*.recoil.png` for measured recoil, `*.anti_recoil.png` for the inverse compensation path, and `*.timeline.png` for recoil and anti-recoil on one time axis
- successful recordings log a `[Recoil] plot_written ...` line with the generated plot paths
- profile replay can be inspected without live gamepad output through `tools\dry_run_recoil_playback.py`, which prints the same plugin-derived `right_x`/`right_y` stick curve that runtime would apply
- the recoil app writes the latest `current_weapon` JSON itself after successful `Y` recognition for observability
- loaded weapon identities and profile records may stay indexed in memory, but `Y` switching always re-recognizes the current HUD weapon before selecting a profile
- recoil may stay on fallback or no profile immediately after a switch until the new OCR capture confirms the current weapon
- OCR defaults to CUDA through RapidOCR and `scripts\launch\recoil_app_start.bat` sets `PYTHONNOUSERSITE=1` so a user-site CPU `onnxruntime` package does not shadow the project GPU package
- set `RECOIL_OCR_PROVIDER=dml` for DirectML or `RECOIL_OCR_PROVIDER=cpu` for explicit CPU OCR; missing GPU providers no longer silently fall back to CPU unless `RECOIL_OCR_ALLOW_CPU_FALLBACK=1`
- switch and learning capture prefer DXGI and only fall back to `PIL.ImageGrab`

## Startup And Scripts

Current gamepad entry points:

- `scripts\launch\gamepad_start.bat`
  - main gamepad runtime
  - defaults to `GAMEPAD_RUNTIME=native`
  - calls `scripts\launch\gamepad_native_cpp_start.bat`
  - set `GAMEPAD_RUNTIME=python` for the old Python fallback
- `scripts\launch\gamepad_native_cpp_start.bat`
  - direct full native C++ gamepad runtime
  - prompts for auto-fire output: `RB` or `RT`
  - prompts for recoil profile selection
  - runs `native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --perf-log`
- `scripts\launch\debug\gamepad_debug.bat`
  - Python-hosted gamepad debug runtime
  - prompts for auto-fire output and backend choice
  - enables `--vision-debug --vision-debug-save`
  - defaults `VISION_CAPTURE_FPS=140`
- `scripts\launch\debug\gamepad_native_debug.bat`
  - Python-hosted native-vision debug runtime
  - enables `--vision-debug`
  - defaults `VISION_CAPTURE_FPS=140`
- `scripts\verify\native_pipeline_contract.bat`
  - pre-acceptance native pipeline contract check
  - use before live acceptance when changing native tracker/controller/recoil behavior

The default production gamepad path is now full native C++:

- native C++ for the hot vision loop
- native C++ for controller input/output
- native C++ for `ai_aim`, auto-fire, aim-assist dynamics, and recoil playback
- Python only for fallback, tooling, and debug/reference paths

`scripts\launch\gamepad_start.bat` and `scripts\launch\gamepad_native_cpp_start.bat` now have a recoil-runtime path:

- launch `scripts\launch\gamepad_start.bat` and choose `1. On` at the recoil-runtime prompt, or press Enter to use the default `On`
- for non-interactive startup, set `ENABLE_RECOIL_RUNTIME=1`
- optionally set:
  - `RECOIL_GAME`
  - `RECOIL_PROFILE_DIR`
  - `RECOIL_SIGNATURE_DIR` / `RECOIL_WEAPON_DIR`
  - `RECOIL_STATE_FILE`
  - `RECOIL_CALIBRATION_DIR`
  - `RECOIL_RECOGNIZER_STATE_PATH`
  - `RECOIL_NATIVE_RECOGNIZER`
- then launch `scripts\launch\gamepad_start.bat` normally
- choose `2. Off` or set `ENABLE_RECOIL_RUNTIME=0` for the plain gamepad runtime
- when this path is enabled, the native launcher sets profile, calibration, weapon, and recognizer-state paths for `cod_native_runtime.exe`
- the default identity directory is `artifacts/recoil_app/weapons`, matching identities created by `scripts\launch\recoil_app_start.bat`

`scripts\legacy\recoil_toolkit.bat` is a legacy/debug helper for older manual workflows. Prefer `scripts\launch\recoil_app_start.bat` for recording/status work and `ENABLE_RECOIL_RUNTIME=1` for main native gamepad integration.

## Legacy And Support Notes

The repository still contains older gamepad-side support modules for:

- adaptive gain
- horizontal assist
- manual-intent arbitration helpers
- overshoot control
- older legacy AI-aim structure

These are still useful reference material, benchmark context, or experimental tooling. They should not be described as the default runtime path unless the controller host is explicitly changed to instantiate them.

## Current Boundary With Vision

The gamepad controller expects vision to decide:

- what target to trust
- what target delta to send
- when auto-fire should be requested

The gamepad layer decides:

- how to mix AI with live manual stick input
- how to map fire output
- how to shape stick actuation on the virtual pad

That split is intentional. The latest native migration keeps the same conceptual boundary, but the default gamepad implementation of both sides now runs in C++.
