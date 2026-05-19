# AI Abstention And Recoil Recognition Reliability Draft

Date: 2026-05-19
Status: Draft only; do not implement until the user confirms the behavior.
Scope: native vision, gamepad controller assist arbitration, `recoil_app_start`, weapon identity, recoil profile readiness

## Application Overview

The normal `gamepad_start.bat` flow has three moving parts:

- native vision detects targets, selects an aim point, and emits optional auto-fire intent
- the gamepad controller mixes physical stick input with AI aim modes, auto-fire, and recoil compensation
- `recoil_app_start` tries to recognize the active weapon, learn or load a recoil profile, and expose it to the gamepad recoil plugin

The new user feedback is about trust boundaries. Some scenes should not be solved by pushing AI harder. In a head-glitch / scalp-only angle, the visible target is so small that automatic lock or auto-fire can become more harmful than manual control. In recoil, poor weapon-name OCR or poor recoil-curve extraction can apply the wrong profile with too much confidence.

## User Feedback Captured

1. In scalp-only positions, consider letting AI stop intervening and leave the shot fully manual because neither the user nor the AI can reliably lock the tiny visible head from the marker.
2. `recoil_app_start` is already being built, but current OCR weapon-name recognition and recoil-curve recognition both feel weak.

## Current Situation

Native/gamepad target handling:

- `ControllerVisionState` now gives the controller one atomic state containing target, target source, auto-fire intent, and timing metadata.
- `ControllerTarget` carries `body_box`, `target_source`, and observed time, but it has no explicit target-quality or assist-policy field.
- The target selector aims at an upper-body point derived from the detected box. There is no first-class `head_only`, `scalp_only`, or `abstain` target state.
- `controllers/gamepad/ai_aim.py` has `manual`, `ads_snap`, and `body_lock` behavior. It gates on freshness and `body_box`, but not on target quality.
- `controllers/gamepad/auto_fire.py` gates on freshness and aiming, but not on target quality.

Recoil app and sidecar:

- `recoil_app/runtime.py` can run in `record` and `recoil` modes and can create weapon identities from Y-switch text capture.
- `vision/weapon_identity/text.py` performs OCR candidate extraction and normalization.
- `vision/weapon_identity/resolver.py` has confidence thresholds, carry-forward behavior, and degraded state, but the confidence model is mostly heuristic.
- The switch-name path can still be too trusting when `_select_best_name(...)` picks a plausible OCR string and assigns a high confidence current weapon.
- `vision/recoil_collection/extraction.py` computes burst retention, variance, and a profile confidence, but `runtime/recoil_sidecar/service.py` treats a matched profile as ready based mainly on identity readiness. Low profile confidence is exposed but not yet a hard readiness gate.

## Proposed Direction

### A. Add AI Abstention For Scalp-Only Targets

The goal is to make "AI should not help here" an explicit runtime outcome.

Introduce a target assist policy that can classify a selected target as one of:

- `normal_body`: regular assist and auto-fire are allowed
- `small_body`: allow weaker aim assist, suppress auto-fire by default
- `head_only_or_ambiguous`: full manual passthrough; suppress AI aim and auto-fire
- `cue_only`: diagnostics only unless a real body box confirms
- `stale_or_hold`: no auto-fire; optional short visual/debug hold only

Implementation should start at the controller boundary rather than the detector. The new `ControllerVisionState` contract is already the right place to carry metadata, and the controller is where manual-vs-AI arbitration happens.

Likely implementation shape:

- Add explicit quality metadata to `ControllerTarget`, such as `assist_policy`, `target_quality`, or `assist_allowed`.
- Add a small classifier near target conversion in `vision/native_runner.py` and `vision/runner.py`, using box height/area/aspect, target source, and whether the body box is plausibly a body rather than a tiny scalp cue.
- In `AIAimPlugin`, treat `head_only_or_ambiguous` as `manual` and reset body-lock/ADS-snap transient state.
- In auto-fire submission, suppress `auto_fire_requested` when quality is not fire-safe.
- Add debug/perf counters for abstention reason so live tests can show whether the gate is too aggressive.

Default draft recommendation:

- First version should fully disable AI aim and auto-fire for `head_only_or_ambiguous`.
- It may still show debug overlay information so the user can understand why the AI stood down.
- After live smoke, optionally restore a very weak slowdown-only mode if full abstention feels too conservative.

### B. Make Recoil Recognition Degrade Before It Misapplies

The goal is to make wrong OCR or bad curves fail closed.

Weapon identity should have separate states:

- `confirmed`: enough evidence to switch active weapon and use profiles
- `suggested`: display/log candidate, but do not switch active profile automatically
- `degraded_carry_forward`: keep previous weapon but mark it unsafe for new profile learning
- `unknown`: no active weapon confidence

Recoil profile readiness should also be explicit:

- `ready`: profile confidence and capture quality pass thresholds
- `profile_poor`: profile exists but should not drive compensation
- `missing`: no matching profile
- `fallback_allowed`: fixed fallback may run only if explicitly enabled

Likely implementation shape:

- For Y-switch OCR, require repeated agreement across delayed captures or a match against an existing identity before writing or switching state.
- Stop auto-creating new identities from a single OCR read unless a manual confirmation path is active.
- Keep exact debug crops for bad OCR when enabled, rather than running heavy OCR sweeps.
- Add a profile readiness gate in `RecoilSidecarService.publish_active_profile(...)` based on profile confidence, burst count, variance, and duration.
- Make the gamepad profile provider ignore `profile_poor` exactly like `unknown`.
- In record mode, save poor profiles with diagnostics, but do not mark them ready for recoil mode.

Default draft recommendation:

- OCR should be advisory unless it agrees across multiple frames or matches an existing known weapon.
- A bad recoil curve should never silently become the active runtime profile.
- Fallback fixed pull should remain opt-in and obvious in logs.

## Concrete Implementation Plan After Confirmation

1. Add target-quality metadata to controller target state.
2. Add scalp/head-only target classifier and config defaults.
3. Gate gamepad AI aim and auto-fire based on target quality.
4. Add tests for normal body targets, tiny/head-only targets, cue-only targets, and stale/hold targets.
5. Add recoil identity states or readiness fields without breaking existing JSON readers.
6. Add OCR voting/confirmation tests for noisy names and existing identity matches.
7. Add profile-quality readiness tests for low confidence, low burst count, high variance, and valid profiles.
8. Update docs and `.agent-context/` after live smoke results.

## Verification Plan

Target abstention:

- synthetic native/Python target tests prove tiny ambiguous boxes produce no AI aim and no auto-fire
- gamepad plugin tests prove manual stick output passes through unchanged during abstention
- regression tests prove normal body lock and ADS snap still work
- live smoke should check scalp angle, normal close target, target loss, and manual fire takeover

Recoil:

- OCR fixture tests prove noisy ammo/UI text does not switch weapons
- repeated-vote tests prove stable existing names can confirm
- sidecar tests prove low-confidence profiles do not publish `ready`
- gamepad recoil tests prove `profile_poor`/`unknown` produce no profile-driven compensation
- manual record/recoil smoke should inspect logs for `confirmed`, `suggested`, `profile_poor`, and `ready`

## Open Decision

For scalp-only targets, choose the first behavior:

- full manual passthrough: disable AI aim and auto-fire
- suppress only body-lock/auto-fire: keep a weak slowdown or diagnostics mode
- adaptive: start manual-only, but allow assist again after several stable frames
