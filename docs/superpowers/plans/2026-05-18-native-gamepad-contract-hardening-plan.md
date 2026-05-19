# Native/Gamepad Contract Hardening Plan

Date: 2026-05-18
Scope: `native-vision`, `gamepad-controller`, and the Python boundary between them
Status: executed in the working tree; automated verification complete, live gameplay smoke still pending

## Overview

The app can be thought of as a Windows gameplay assist pipeline. Native vision finds a target from captured frames, Python turns that result into controller-facing aim/fire intent, and the gamepad controller mixes that intent with the player's physical controller input before writing to the virtual gamepad.

The current risk is not only raw model latency. The bigger user-facing risk is that target position, target age, auto-fire intent, and controller output timing are not yet one clear contract. If those pieces drift apart, the player can see stale aim, stale firing, wrong handoff behavior, or latency that cannot be diagnosed from `infer_ms` alone.

The goal of this plan is to make the default `gamepad_start.bat` path safer and easier to measure before adding more targeting heuristics.

## Execution Summary

Completed in this working tree:

- Phase 1: one controller-facing vision-state submission path, auto-fire freshness gating, and neutral virtual-gamepad shutdown.
- Phase 2: end-to-end timing fields and aggregated perf-log metrics for source age, native pipeline age, Python handoff, controller consume age, and virtual output age.
- Phase 3: native empty detection frames now clear target/fire state; Python parity tests document the intended empty-gap policy difference.
- Phase 4: `config.toml.example` and loader support explicit model path, auto-fire, and recoil fallback knobs; native/gamepad docs were refreshed.

Still pending outside automated tests:

- Manual `gamepad_start.bat` smoke in a live gameplay session.

## Current Situation

- `gamepad_start.bat` remains the user-facing launcher to judge the combined runtime.
- Native vision is the default production direction; Python vision remains useful as fallback and test/reference material.
- `ControllerTarget.observed_at` now exists, and gamepad/mouse controllers can use it instead of restamping every target as fresh.
- Auto-fire manual takeover was recently improved with a release window and suppression guard totaling 120ms.
- Review-time verification passed `71` selected native/gamepad tests:

```powershell
py -3 -B -m unittest tests.test_native_vision_runner tests.gamepad.test_gamepad_auto_fire_plugin tests.gamepad.test_gamepad_ai_aim_plugin tests.gamepad.test_gamepad_controller_host -v
```

Known gaps:

- Auto-fire does not yet have the same explicit freshness discipline as aim targets.
- Vision target update and auto-fire update are submitted separately to the controller.
- Native `age_ms` does not represent full input-to-output latency.
- Native no-updated-frame behavior and Python fallback hold/predict behavior are not one explicit policy.
- Synthetic native/Python parity still has a known occlusion failure.
- Gamepad shutdown should explicitly neutralize virtual output.
- Config and docs contain drift around adaptive gain, recoil fallback, native engine path, and target-source semantics.

## Proposed Direction

Keep the hybrid architecture for now: native vision for capture/inference/selection hot paths, Python for controller behavior, plugins, config, and tests.

The first improvement should be a clearer boundary contract rather than a larger rewrite. The controller should receive one coherent vision state containing movement, target metadata, auto-fire intent, and timing. Once that exists, timing instrumentation can show whether the next bottleneck is native capture, TensorRT inference, Python handoff, controller consume, or virtual output.

## Phase 1: Contract Safety

Outcome: the controller no longer observes mixed target/fire state, stale auto-fire is gated, and shutdown leaves the virtual gamepad neutral.

Implementation:

1. Add a controller-facing vision state API.
   - Candidate shape: `VisionState` dataclass or `BaseController.update_vision_state(...)`.
   - Include `dx`, `dy`, `target`, `auto_fire_requested`, `observed_at`, and optional native timing fields.
   - Keep legacy methods only as wrappers until callers are migrated.
2. Migrate callers.
   - `vision/native_runner.py` should submit one vision state per native result.
   - `vision/runner.py` should submit the same shape for Python fallback.
   - `GamepadController` and `MouseController` should consume the shared state consistently.
3. Add auto-fire freshness gating.
   - Carry `auto_fire_observed_at` or reuse the state's observed timestamp when auto-fire is derived from that target result.
   - Suppress auto-fire when the source state is older than the configured freshness threshold.
   - Consider a stricter threshold for fire than aim if tests or live feel show that firing needs less tolerance.
4. Neutralize on stop.
   - Ensure `GamepadController.stop()` or its shutdown path emits one final neutral virtual gamepad state.
   - Keep this idempotent so repeated stop/reset calls are safe.

Tests:

```powershell
py -3 -B -m unittest tests.test_native_vision_runner tests.test_vision_runner tests.gamepad.test_gamepad_auto_fire_plugin tests.gamepad.test_gamepad_controller_host tests.gamepad.test_gamepad_ai_aim_plugin -v
```

Add focused coverage for:

- stale target does not trigger auto-fire
- target and auto-fire are updated atomically from one state
- no-target or gap frames clear/suppress fire as intended
- stop sends a neutral virtual output

## Phase 2: End-To-End Timing

Outcome: latency discussions can point to measured stages, not a single ambiguous number.

Implementation:

1. Add or expose timestamps for:
   - frame/source capture time
   - native result finish time
   - Python receive time
   - controller state update time
   - gamepad loop consume time
   - virtual output send time
2. Report useful derived metrics:
   - `source_age_ms`
   - `native_pipeline_ms`
   - `python_handoff_ms`
   - `controller_consume_age_ms`
   - `output_age_ms`
3. Keep logs lightweight.
   - Aggregate p50/p95/max rather than printing every frame by default.
   - Add a debug switch if per-frame tracing is needed for one run.

Verification:

- Unit-test timestamp propagation with deterministic fake clocks.
- Run `gamepad_start.bat` and confirm diagnostics can distinguish native inference time from controller consume/output age.

## Phase 3: Native Gap And Parity Policy

Outcome: target-loss behavior is explicit, testable, and consistent with the intended production contract.

Implementation:

1. Define native no-updated-frame behavior.
   - Option A: clear target immediately.
   - Option B: short hold with explicit `source=hold`.
   - Option C: prediction with explicit `source=predicted`.
2. Update tests to match the chosen policy.
   - If Python parity remains required, fix native occlusion behavior.
   - If native becomes the production spec, turn Python parity into compatibility/golden-contract tests rather than requiring identical internals.
3. Add or update golden cases for:
   - occlusion and reacquire
   - wide-low prone/side boxes
   - cue-hold target source
   - auto-fire eligibility under target loss

Verification:

```powershell
py -3 -B -m unittest tests.test_native_vision_synthetic_parity tests.test_native_vision_targeting_bridge tests.test_vision_targeting -v
```

If native C++ changes are made:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

## Phase 4: Config And Docs Cleanup

Outcome: runtime knobs and docs match what the default path actually does.

Implementation:

1. Make high-impact runtime choices explicit.
   - native engine path
   - recoil fallback enable/amount
   - auto-fire freshness threshold
   - auto-fire manual takeover release/resume timings
2. Resolve or remove misleading examples.
   - If `[gamepad.adaptive_delta_gain]` is not wired into the default plugin chain, either wire it intentionally or mark it as non-default/experimental.
3. Refresh native docs.
   - Current target-source labels and gap behavior
   - Current timing semantics
   - Known parity status
4. Keep tuning surface small.
   - Expose settings that affect safety, latency, or live feel.
   - Avoid adding knobs for every internal heuristic.

Verification:

```powershell
py -3 -B -m unittest tests.test_config_loader tests.gamepad.test_gamepad_auto_fire_plugin tests.gamepad.test_gamepad_recoil_compensation -v
```

Adjust exact test modules to match the final files touched.

## Manual Smoke Checklist

Run this after Phase 1 and again after timing work:

1. Start from `gamepad_start.bat`.
2. Confirm virtual gamepad starts neutral.
3. ADS on a clear target and check aim assist continuity.
4. Use semi-auto weapons and verify manual RB/RT fire is not swallowed.
5. Force short target-loss or capture-gap moments and confirm auto-fire does not fire stale.
6. Hold ADS for a longer period and watch timing diagnostics for p95/max spikes.
7. Exit the runtime and confirm virtual output returns to neutral.

## Risks And Caveats

- Fire gating is feel-sensitive. Too strict can suppress valid shots; too loose can fire stale intent.
- Changing native gap behavior can improve continuity but may increase wrong-target risk if the hold/prediction source is not labeled clearly.
- End-to-end timing should come before any controller-to-C++ rewrite discussion.
- Unit tests are necessary but not enough here; live gameplay smoke remains required for hand-feel and safety.
