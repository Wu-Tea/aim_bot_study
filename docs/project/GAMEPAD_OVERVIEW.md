# Gamepad Overview

## Goal

The current gamepad path is split into two layers:

- Base layer: read the physical gamepad and mirror it to the virtual Xbox gamepad.
- Enhancement layer: treat aim assist, auto-fire, recoil pull-down, and similar behavior as plugins.

This keeps the main loop narrow. The host is responsible for I/O and state handoff. Behavior changes live in plugins.

## Current directory layout

- `controllers/gamepad_controller.py`
  - gamepad host loop
  - physical input read
  - virtual gamepad writeback
  - plugin invocation
- `controllers/gamepad/`
  - plugin contracts and gamepad-specific enhancement modules
- `tests/gamepad/`
  - gamepad plugin and host tests
- `gamepad_start.bat`
  - start gamepad mode
- `mouse_start.bat`
  - start mouse mode

## Runtime flow

1. `main.py` parses runtime options such as `--controller-mode` and `--auto-fire-output`.
2. `controller.py` creates `GamepadController` when `--controller-mode gamepad` is selected.
3. `vision/runner.py` drives detection and targeting.
4. Vision sends only high-level signals into the controller:
   - `controller.update(dx, dy)` for target offset
   - `controller.set_auto_fire(pressed)` for fire intent
   - `controller.reset()` when no valid target or aim state is lost
5. `controllers/gamepad_controller.py` reads the physical gamepad state, builds a `GamepadFrame`, creates a mutable `GamepadOutput`, then runs the plugin chain.
6. The final `GamepadOutput` is written to the virtual Xbox controller.

The key boundary is: the vision side does not directly decide button mapping or stick shaping. It only sends target and fire signals.

## Base responsibilities in `gamepad_controller.py`

`GamepadController` is the host, not the place for feature growth. Its current responsibilities are:

- initialize `pygame` and `vgamepad`
- read physical sticks, triggers, buttons, and D-pad
- apply physical-stick deadzone handling
- track controller-local state such as aiming and latest target revision
- package one frame of input into `GamepadFrame`
- seed `GamepadOutput` with pure passthrough values
- run plugins in order
- write the final output to the virtual pad

This matches the direction you wanted: main loop only passes signals and applies final output.

## Plugin contracts

`controllers/gamepad/state.py` defines two core data structures:

- `GamepadFrame`
  - immutable snapshot of one controller frame plus vision signals
- `GamepadOutput`
  - mutable output buffer that plugins can modify before writeback

`controllers/gamepad/plugin.py` defines the host/plugin contract:

- `apply_plugins(plugins, frame, output)`
- `reset_plugins(plugins)`

Each plugin only needs:

- `reset()`
- `apply(frame, output)`

That contract is small enough to reuse for a mouse backend later.

## Current plugins

### `AIAimPlugin`

File: `controllers/gamepad/ai_aim.py`

Responsibility:

- convert vision-space target delta into AI right-stick correction
- blend AI correction with manual right-stick input
- fade AI out when the player is already moving the stick hard
- keep wrong manual X input from shutting AI off too early when target direction is stable

It currently contains four sub-plugins, in this order:

- `ManualIntentGuardSubPlugin`
  - watches recent target-revision X direction
  - only activates when recent target X error is stable enough to trust
  - if the player keeps pulling the right stick against that stable direction, it softens manual X and removes that wrong input from the X-axis AI fade calculation
  - current scope is intentionally X axis only, so vertical manual correction and recoil feel stay independent
- `AdaptiveDeltaGainSubPlugin`
  - increases effective target delta when the system is clearly not converging
  - solves the case where the AI cap is high enough, but the input to the pixel-to-stick map is still too small to catch a moving target

- `HorizontalAssistSubPlugin`
  - predicts horizontal movement and adds feedforward / catch-up behavior
- `OvershootGuardSubPlugin`
  - reduces AI carry and desired force near convergence or after zero-crossing to avoid pulling past the target

This is the main place where movement prediction compensation, wrong-input correction, catch-up behavior, and overshoot limiting now live.

### Current AI aim blend rules

The current gamepad aim path is:

1. Read manual right-stick input from the physical pad.
2. Read target `dx/dy` from vision and scale it by baseline `ai_delta_gain`.
3. Let the AI sub-plugin chain adjust:
   - manual X trust
   - target delta gain
   - horizontal feedforward
   - near-target overshoot damping
4. Map the adjusted screen-space delta into virtual right-stick output.
5. Add the AI stick on top of the manual stick.

Important details:

- X-axis AI fade is no longer driven only by raw manual stick magnitude.
- When manual X is obviously wrong and recent target direction is stable, AI remains active on X instead of fading out with the player mistake.
- When manual X agrees with target direction, the system keeps the normal blend and fade behavior.
- Y-axis behavior is still conservative on purpose. Wrong-input correction currently does not interfere with vertical recoil or manual vertical adjustment.
- Pixel-to-stick mapping is piecewise rather than purely linear, so the mid-range error band is stronger without forcing max stick too early.

### `AutoFirePlugin`

File: `controllers/gamepad/auto_fire.py`

Responsibility:

- convert `auto_fire_requested` into actual fire output on the virtual pad
- support configurable fire mapping

Current config:

- `RB`
- `RT`

So the enhancement is named `AutoFire`, but the final fire button is a config choice rather than hard-coded to RB.

### `RecoilCompensationPlugin`

File: `controllers/gamepad/recoil_compensation.py`

Responsibility:

- add downward right-stick pull while auto-fire is active

This plugin is intentionally separate from `AutoFirePlugin`, so fire timing and recoil behavior can evolve independently.

## Supporting modules

### `manual_intent_guard.py`

- tracks a short X-axis target history
- classifies recent manual X input as aligned, opposed, or unstable
- only attenuates manual X when target direction is stable enough to trust
- supplies a corrected X value for output mixing and for AI fade

This is the main protection against user over-correction pulling the reticle off target and disabling AI at the same time.

### `adaptive_delta_gain.py`

- tracks whether target error is shrinking or stalling
- temporarily boosts effective target delta when the AI is not catching up
- cools down quickly once convergence resumes

### `horizontal_assist.py`

- tracks horizontal error velocity
- adds limited feedforward when the target is moving sideways
- builds a small catch-up bonus if the system is not converging

### `overshoot_guard.py`

- tracks near-target convergence on both axes
- detects zero-crossing close to center
- reduces desired AI force and carry when the system is likely to pull through the target

## Current tuning direction

The current gamepad stack is intentionally biased toward trusting AI more than before, but only when the system has enough evidence:

- stable recent target direction can override clearly wrong manual X input
- non-converging target error can temporarily raise effective AI pull
- horizontal prediction can help the reticle arrive earlier on sideways movement
- overshoot protection still limits pull-through near target center

So the current design goal is not "weaken AI when the player moves the stick".
It is "blend normally when the player is helping, but rescue the track when the player is clearly fighting a stable target direction".

## Relationship to vision-side targeting

The gamepad side assumes vision sends only target and fire signals, but aim feel also depends on upstream targeting rules:

- target selection now exposes a torso-oriented slow zone
- near-target damping is intended to happen only when the reticle is already converging inside that trusted torso zone

That separation matters for future mouse work:

- vision decides where the trusted target zone is
- controller plugins decide how strongly to move toward it and how to mix with manual input

## Startup and configuration

### `gamepad_start.bat`

- starts `main.py --controller-mode gamepad`
- prompts for AutoFire output:
  - `1 = RB`
  - `2 = RT`
- enables perf logging by default

### `mouse_start.bat`

- starts `main.py --controller-mode mouse`
- enables perf logging by default

CLI entry:

- `main.py --auto-fire-output RB|RT`

## Compatibility notes

- `controllers/gamepad_controller.py` stays in the controller root by design.
- Gamepad enhancement modules live under `controllers/gamepad/`.
- Gamepad tests live under `tests/gamepad/`.
- D-pad output uses per-button `press_button` / `release_button` calls, because the installed `vgamepad.VX360Gamepad` on this machine does not provide `directional_pad()`.

## What Claude should preserve for a mouse design

The mouse version should preserve the same separation:

- host layer
  - read physical mouse / keyboard state
  - receive vision signals
  - write final output
- plugin layer
  - AI aim
  - auto-fire
  - recoil compensation
  - wrong-input correction
  - future prediction / overshoot limiting / motion compensation

The reusable idea is not "virtual gamepad output". The reusable idea is:

- one immutable frame input
- one mutable output object
- a thin host
- a plugin chain for enhancements
- short-window intent classification before final AI/manual blending

## Suggested translation to a mouse backend

Claude can use this gamepad design as a template and replace the output target:

- gamepad right-stick output -> mouse delta output
- gamepad fire mapping (`RB` / `RT`) -> mouse button mapping or configurable fire action
- recoil compensation right-stick pull -> downward mouse delta
- AI fade against manual stick input -> AI fade against strong manual mouse movement
- manual-intent guard against wrong right-stick X input -> short-window manual-intent guard against wrong mouse X movement
- overshoot guard and horizontal prediction -> keep as screen-space correction modules if the math remains target-delta based

## Detailed design notes

This file is the current behavior overview.

Detailed change-by-change notes remain in:

- `docs/superpowers/specs/2026-04-13-gamepad-adaptive-delta-gain-design.md`
- `docs/superpowers/plans/2026-04-13-gamepad-manual-intent-guard.md`

Those files are useful for implementation history and tuning rationale, but this overview should be the primary handoff document for future controller work.

## Questions Claude should answer for the mouse plan

- What is the mouse equivalent of `GamepadFrame` and `GamepadOutput`?
- Which parts of AI aim remain screen-space and can be shared conceptually?
- Which mouse actions belong in the base host and which must stay plugins?
- How should auto-fire map to mouse buttons and keyboard-assisted fire cases?
- How should manual mouse movement suppress or blend with AI output?
- Does the mouse path need its own overshoot and recoil tuning, or can it reuse the same conceptual modules with different output scaling?
