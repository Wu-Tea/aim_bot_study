# Gamepad Body Lock Input Arbitration Design

**Goal:** Replace the current "manual stick + AI stick" body-lock blend with an input arbitration layer that decides which parts of the player's right-stick input should be preserved, damped, or suppressed so the reticle keeps following the tracked target even when manual input is noisy or briefly wrong.

**Primary outcome:** `Body Lock` should stop behaving like a raw sum of two controllers and instead behave like a tracking controller with sanitized player intent. Helpful manual input should still feel present, but harmful input should no longer be able to knock the reticle off the target as easily.

**Scope:**
- Cover the `gamepad` controller only.
- Change the default state-machine path in `controllers/gamepad/ai_aim.py`.
- Apply the arbitration layer only while `mode == "body_lock"` in this phase.
- Keep the existing `Manual / ADS Snap / Body Lock` top-level state model.
- Extend the manual-mix benchmark so it can measure whether arbitration preserves lock under harmful input.

**Non-goals:**
- No `vision` model changes.
- No new live-game telemetry capture in this phase.
- No removal of the legacy sub-plugin path.
- No ADS behavior change in this phase, although the arbitration helpers should be structured so `ADS Snap` can reuse them later.
- No machine-learned player model.

## Problem

The current default `Body Lock` path mixes input like this:

- compute an AI correction vector toward the upper-body lock point
- smooth that correction into `ai_stick_x/y`
- add the result directly to `manual_right_x/y`

That works when the player and the controller agree, but the new manual-mix benchmark exposed the weak spot: once the player briefly counter-steers, over-corrects, or jitters orthogonally, the controller is not really arbitrating anything. It is just summing two vectors and hoping the net output still tracks.

The result is a control model that is:

- good at steady tracking when manual input is aligned
- fragile when manual input is briefly harmful
- too dependent on "yield vs. not yield" tuning, instead of explicitly deciding which inputs should count

The product goal for this phase is not "make AI yield more." It is:

- keep the reticle following the tracked target even when the player's input is imperfect
- preserve the feeling of participation when the player's input is helping
- suppress the parts of manual input that are actively damaging target tracking

## Current Constraints

The existing `Body Lock` implementation already gives us the pieces we need:

- stable target matching through body-box IoU or center tolerance
- an upper-body lock point instead of full-body center
- short-horizon motion lead
- a compact activation window that limits lock engagement to a central region
- manual-mix benchmark coverage with deterministic opposing bursts and wobble

The weak point is specifically the final output composition. Today the controller does not evaluate:

- whether manual input is aligned with the desired correction vector
- whether orthogonal manual input is useful or just noise
- whether current target continuity is strong enough to justify aggressively suppressing bad input

## Approaches Considered

### 1. Add a stronger "yield" gate on top of the current blend

Pros:
- smallest code change
- easy to target the current reverse-move slowdown-bubble complaints

Cons:
- still frames the problem as "AI vs. player"
- does not explicitly distinguish helpful, harmful, and orthogonal input
- unlikely to generalize cleanly to ADS later

### 2. Hard-override player input whenever `Body Lock` confidence is high

Pros:
- strongest guarantee that the reticle keeps following the target
- simple mental model

Cons:
- feels too much like input hijacking
- throws away useful aligned manual input
- likely to feel bad when confidence is wrong or the player intentionally wants to break lock

### 3. Add a vector-based arbitration layer with dynamic confidence

Pros:
- preserves aligned manual intent
- suppresses harmful components instead of blindly zeroing the whole stick
- gives us one reusable rule system that can later be applied to ADS
- matches the benchmark problem more directly

Cons:
- more moving parts than a simple gate
- needs new tests and new benchmark metrics

## Chosen Design

Choose **Approach 3**.

`Body Lock` will gain an **input arbitration layer** between the raw manual stick input and the final composed output. The arbitration layer will:

1. compute the desired correction vector toward the current body-lock target
2. compute a `lock_confidence` score in the range `0.0 .. 1.0`
3. decompose the player's manual input into:
   - aligned-toward-target component
   - opposing component
   - orthogonal component
4. preserve or mildly amplify aligned input
5. damp orthogonal input more aggressively near the lock point
6. strongly suppress opposing input when `lock_confidence` is high
7. fall back toward raw manual control when `lock_confidence` is low or the target continuity is weak

The important behavioral change is this:

- `Body Lock` no longer treats the full manual stick vector as sacred
- it treats manual input as intent that must be filtered through the currently tracked target

## Architecture

The design is split into four bounded pieces.

### 1. Lock confidence estimator

Location:

- `controllers/gamepad/ai_aim.py`

Responsibilities:

- summarize how safe it is to trust the current body-lock target
- produce one bounded scalar `lock_confidence`
- reset quickly when ADS ends or the tracked target changes

The first version will derive `lock_confidence` from these signals:

- `target continuity`
  - increases with consecutive matched target frames
- `body-lock validity`
  - target still has a body box
  - reticle center is still inside the body box plus tolerance
- `activation proximity`
  - stronger when the lock delta is comfortably inside the activation box
- `motion stability`
  - stronger once the plugin has enough motion history to trust lead and continuity

This is intentionally simple and local. It does not need a new public state machine.

### 2. Manual-vector decomposition

Location:

- `controllers/gamepad/ai_aim.py`

Responsibilities:

- interpret the player's raw right-stick input relative to the desired correction vector
- separate helpful input from harmful input

For the active `Body Lock` target, let:

- `d = (desired_ai_x, desired_ai_y)` in screen-space correction intent
- `m = (manual_right_x, manual_right_y)` in stick space after sign normalization consistent with current output composition

The arbitration layer will build a normalized desired direction `u` from `d`, then decompose `m` into:

- `m_parallel`
  - the component of `m` along `u`
- `m_perpendicular`
  - the remainder orthogonal to `u`

`m_parallel` is then split into:

- `m_helpful`
  - same direction as `u`
- `m_harmful`
  - opposite direction from `u`

This gives the controller a concrete basis for deciding what to keep and what to suppress.

### 3. Arbitration profile and sanitized manual output

Location:

- `controllers/gamepad/ai_aim.py`

Responsibilities:

- transform raw manual input into `sanitized_manual_x/y`
- compose `sanitized_manual + ai_stick` for final output

The first version will use a profile-driven rule set rather than additional public modes.

#### Helpful aligned input

When the player pushes in the same direction as the desired correction:

- keep most of that component
- optionally allow a mild preservation bonus when error is still moderate or large
- reduce preservation near the exact lock point so micro-noise does not dominate settling

This keeps the controller feeling collaborative instead of fully automatic.

#### Harmful opposing input

When the player pushes against the desired correction:

- if `lock_confidence` is high, heavily suppress that component
- if `lock_confidence` is medium, partially suppress it
- if `lock_confidence` is low, release suppression and allow more manual freedom

The suppression decision will also get stronger when:

- the error is already small
- the target remains stably matched
- the frame is still well inside the body-lock activation box

This is the core mechanism that should prevent wrong-direction bursts from breaking the lock.

#### Orthogonal input

Orthogonal input is neither clearly helpful nor clearly harmful. In practice it is often where jitter and unintended wobble live.

The first version will:

- preserve more orthogonal input when the error is large or confidence is low
- damp orthogonal input when the reticle is already near the lock point
- damp vertical orthogonal energy slightly more aggressively than horizontal energy near the upper-body point

This should reduce "shake the lock off target" behavior without making large-error recapture feel overconstrained.

### 4. Benchmark-facing arbitration metrics

Locations:

- `tests/gamepad/manual_mix_metrics.py`
- `tests/gamepad/test_gamepad_manual_mix_metrics.py`
- `docs/project/GAMEPAD_MANUAL_MIX_BENCHMARKS.md`

Responsibilities:

- measure whether the arbitration logic actually protects tracking under bad input
- keep the current manual-mix suite reproducible

The current manual-mix suite already records:

- `conflict_frames_ratio`
- `wrong_input_recovery_frames`
- `manual_yield_score`

For this phase, the optimization target shifts away from `manual_yield_score`. The suite should add or promote metrics that answer the real product question:

- `harmful_input_suppression_ratio`
  - how much opposing manual input was removed before the final output
- `aligned_input_preservation_ratio`
  - how much helpful manual input survived arbitration
- `opposing_burst_hold_error_px`
  - mean tracking error during annotated opposing-burst windows
- `lock_survival_rate`
  - fraction of opposing bursts that do not knock the controller out of effective body-lock tracking

`manual_yield_score` can stay as a secondary diagnostic, but it should no longer be treated as the headline optimization metric.

## Detailed Behavior

### When arbitration is active

Arbitration is active only when:

- ADS is active
- the plugin selected `mode == "body_lock"`
- there is a valid tracked target with a body box

Outside of `Body Lock`, manual input remains unchanged in this phase.

### When arbitration is bypassed

Arbitration must bypass cleanly and return raw manual input when:

- there is no valid target
- the desired correction vector is too small to define a meaningful direction
- the tracked target changed and confidence has not rebuilt yet
- ADS has ended

This prevents undefined vector math and avoids carrying lock-specific restrictions into manual play.

### Confidence reset rules

The confidence estimator resets when:

- ADS ends
- target continuity breaks
- target body box disappears
- the controller leaves `Body Lock`

This keeps aggressive suppression tied to a currently trusted lock, not stale history.

### Composition order

Within `Body Lock`, the output path becomes:

1. compute desired body-lock delta
2. compute desired AI stick vector and smoothing as today
3. compute `lock_confidence`
4. sanitize raw manual input into `sanitized_manual`
5. compose `final_output = sanitized_manual + ai_stick`
6. clamp to stick limits

This preserves the current high-level controller structure while changing the one place that is causing mixed-input instability.

## Configuration

The first implementation should add explicit config controls to `AIAimConfig` for arbitration rather than hiding the behavior in hard-coded constants.

The initial config group should cover:

- minimum confidence needed for strong harmful-input suppression
- maximum opposing-input suppression ratio
- maximum orthogonal-input suppression ratio
- helpful-input preservation floor
- near-lock error band where orthogonal damping increases
- optional vertical orthogonal damping bias

These parameters should live with the rest of the gamepad AI aim config and be exposed through the normal config loader path.

## Testing Strategy

### Unit tests

Add focused tests in `tests/gamepad/test_gamepad_ai_aim_plugin.py` for:

- high-confidence opposing input is suppressed enough that net output still moves toward the tracked target
- aligned manual input remains materially preserved during `Body Lock`
- orthogonal manual input is damped more near the lock point than far from it
- low-confidence or freshly reacquired targets fall back toward raw manual input instead of over-suppressing
- leaving `Body Lock` resets arbitration state
- ADS behavior remains unchanged outside `Body Lock`

### Manual-mix metric tests

Add or update tests in `tests/gamepad/test_gamepad_manual_mix_metrics.py` for:

- arbitration-specific metrics serialize and aggregate correctly
- opposing-burst windows produce measurable suppression statistics
- deterministic seeds still reproduce the same metrics

### Benchmark validation

After implementation, the manual-mix suite should be rerun to compare against the current committed baseline.

Success should look like:

- lower `opposing_burst_hold_error_px`
- better `lock_survival_rate`
- lower `wrong_input_recovery_frames`
- improved or stable aggregate error metrics
- `aligned_input_preservation_ratio` staying high enough that helpful player input still meaningfully contributes

## Risks and Mitigations

### Risk: input feels too constrained

If suppression is too strong, `Body Lock` could feel like it is stealing control.

Mitigation:

- preserve aligned input explicitly
- release suppression when confidence is low
- keep arbitration scoped to `Body Lock` only in phase 1

### Risk: false confidence on the wrong target

If target continuity is wrong, strong suppression could keep the player attached to the wrong target.

Mitigation:

- reset confidence aggressively on target discontinuity
- keep confidence dependent on valid body-box continuity and activation proximity
- do not extend arbitration into ADS until the body-lock version proves stable

### Risk: benchmark improves while live feel worsens

Mitigation:

- keep the manual-mix suite focused on harmful-vs-helpful input separation
- continue validating against live-game feel, especially for target switches and intentional disengage

## Rollout Notes

This phase changes only `Body Lock`, but the design intentionally separates:

- confidence estimation
- vector decomposition
- arbitration policy

If the approach works, the same arbitration helpers can later be reused by ADS to decide when snap assistance should preserve, limit, or suppress manual input during the short snap window.
