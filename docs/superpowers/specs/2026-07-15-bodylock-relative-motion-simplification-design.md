# BodyLock Relative-Motion and Smooth-Authority Simplification Design

## Status

Core design approved in conversation on 2026-07-15. The ADS-to-BodyLock handoff
contract was added after review feedback and awaits confirmation. This document
defines the production change to follow the evidence-only benchmark in
`2026-07-15-left-stick-relative-motion-defect-benchmark-design.md`.

## Problem and Role in the System

BodyLock is the sustained-aim part of the native gamepad controller. It uses the
selected target and body box to request right-stick assistance after target
acquisition. The player, however, also moves laterally with the left stick. That
movement changes the target's apparent screen position even when the target is
stationary. Today the left stick is passed to the virtual controller but is not
part of BodyLock's motion model.

The live recording and aligned telemetry exposed two related failures:

1. During player or target lateral motion, BodyLock often reacts too late or
   requests too little force because it only observes the resulting target-box
   displacement after a subsequent vision frame.
2. During selector/tracker/lifecycle transitions, valid sustained assistance
   can be removed abruptly. Reacquisition can then restore a large request in
   one tick, producing the old stop-start or shake behavior.

The existing code also applies BodyLock output through several partially
overlapping transformations: axis zero-cross guards, motion lead, vertical-tail
and stabilization boosts, BodyLock-local smoothing, manual takeover/escape
rules, lifecycle history resets, and the final aim-assist dynamics envelope.
Adding another compensator to that chain would make ownership less clear and
create another possible brake.

This design therefore solves the motion defect and simplifies the output path
at the same time. It does not build a full 3D world model, require weapon data,
or add another vision pass.

## Evidence and Root Causes

### Left intent is absent from the aim input

`NativeGamepadController::build_output` preserves physical left-stick output,
but `apply_ai_aim` supplies only aiming/fire state and manual right-stick input
to `NativeAiAim`. Two runs with identical vision and right-stick input therefore
produce the same AI trace even when their left-stick profiles are different.

### Motion velocity is updated from duplicate vision frames

The controller runs at 1000 Hz while live selected-target observations are
typically delivered around 80-100 Hz. `NativeAiAim::compute` currently calls the
BodyLock motion observer on every control tick. The observer prefers the current
controller time and has no fresh-sequence gate.

The same body box is consequently processed roughly 10-12 times. Duplicate
ticks produce zero velocity; the next real frame's displacement is divided by a
near-1 ms interval and produces a spike; subsequent duplicate ticks return to
zero. This sawtooth can destabilize lead and make smoothing alternate between
stale and exaggerated requests.

### ADS evidence and BodyLock continuity share one gate

The controller currently removes `aim_authority` while aiming whenever the
current observation is absent. That is the intended conservative condition
for ADS acquisition, but it also removes sustained BodyLock assistance even
when the authority/lifecycle layer still has same-track continuity. An ADS
safety rule is therefore acting as a BodyLock brake.

### Lifecycle resets and reacquisition are not bumpless

`BodylockLifecycle` immediately yields and resets assistance history when
BodyLock is unavailable. Subsequent reacquisition can return a large requested
assist immediately, especially when ADS snap smoothing is zero. The resulting
release/reacquire edge is visible to the camera rather than being absorbed by a
single output envelope.

### ADS completion drops terminal-approach protection

The current ADS completion gate counts distinct centered frames and waits for
the existing crossing-brake flag. That crossing flag, however, is armed only by
a strong manual right-stick carry through a sign change. It does not prove that
AI-driven acquisition output or measured closing velocity has settled.

Commit `1981da8` then made `AdsCarryBrakePolicy` return unchanged output whenever
`body_lock_active` is true and changed its regression expectation to allow
bounded BodyLock overshoot for moving-target continuity. The separation fixed
an ownership problem, but it also created a handoff gap: ADS can declare
completion near the target, switch to BodyLock on the next tick, and immediately
lose terminal-approach protection while the acquisition vector is still
carrying toward the target.

The fix is not to let an ADS brake regain authority over all BodyLock output.
The handoff must preserve the feed-forward component needed to follow real
relative motion while decelerating only the position-closing component that
would carry the reticle through the target.

### Output ownership is distributed

Both `NativeAiAim` and `NativeAimAssistDynamics` currently smooth or constrain
BodyLock. Manual arbitration also has multiple overlapping paths. A lifecycle
transition can reset all history even though target-motion state and delivered
stick state have different safety lifetimes. This makes the final cause of a
zero or discontinuous output difficult to reason about.

## Goals

- Use physical left-stick intent to predict player-induced relative target
  motion without a weapon/attachment database.
- Learn the effective ADS lateral-mobility response online in process memory.
- Distinguish target/player same-direction, opposite-direction, and nearly
  synchronized motion from measured relative motion.
- Update motion estimation only from fresh vision observations while continuing
  to deliver smooth 1000 Hz output.
- Preserve bounded assistance through a short, identity-safe same-track gap.
- Decay smoothly to zero after identity is no longer trustworthy and never
  follow an arbitrary remaining detector candidate.
- Restore assistance without a first-tick jump after valid reacquisition.
- Transfer ADS acquisition into BodyLock without dropping near-target
  deceleration or suppressing legitimate moving-target feed-forward.
- Reduce BodyLock to one desired-output owner and one delivered-output owner.
- Preserve manual right-stick control and keep recoil as the final independent
  feed-forward stage.

## Non-Goals

- No full 3D reconstruction, depth estimator, optical flow, background scan, or
  extra detector invocation.
- No weapon name, attachment, scope, stance, or ADS-speed table.
- No persisted learned mobility data; restarting the process clears it.
- No change to small/far-target authority in this iteration, apart from
  regression coverage that prevents it from becoming more aggressive.
- No automatic transfer of assistance to an unselected detector candidate.
- No smoothing of the player's manual stick or the recoil component.
- No new BodyLock brake/output stage.
- No restoration of the old post-hoc ADS carry brake over the combined manual
  plus BodyLock output.

## Design Principles

### Estimation is not authority

The relative-motion observer may estimate motion, gain, and confidence. It may
not write a stick value, disable authority, reset the output envelope, or choose
a target.

### One owner for desired assist, one owner for delivered assist

The production path becomes:

```text
selected target + body geometry + left intent + fresh-frame metadata
                            |
                            v
              RelativeMotionObserver
                 (estimate only, O(1))
                            |
                            v
                  BodylockPlanner
              (only desired-assist owner)
                            |
                            v
                  AssistEnvelope
             (only delivered-assist owner)
                            |
                            v
              manual right + delivered AI
                            |
                            v
                  recoil added last
```

Lifecycle and authority provide state to these components. They do not perform
stick arithmetic.

### Fresh observations update state; controller ticks interpolate state

Observer state changes only when `fresh_observation` is true and the
`vision_sequence` is newer than the last consumed sequence. The 1000 Hz loop may
project and consume the last accepted estimate, but it may not reinterpret a
duplicate target box as another measurement.

### Fail closed by confidence, not by an abrupt hidden brake

Uncertain estimates contribute progressively less feed-forward. Loss of target
identity starts one explicit envelope release. Safety is expressed by
confidence and lifecycle state, rather than several independent stages that all
can silently zero BodyLock.

## Production Architecture

### 1. RelativeMotionObserver

The observer replaces/refactors the current BodyLock motion-policy role. It is
a small fixed-state estimator, not a second tracker and not a controller output
policy.

Inputs:

- selected track id and observation id/vision sequence;
- `fresh_observation` and capture timestamp;
- body-box center and height;
- selected-target/tracker horizontal motion after the existing tracker has
  attributed camera motion;
- physical `left_x` after the ordinary input deadzone/shape;
- pre-recoil manual-plus-assist right-stick control sample for attribution;
- BodyLock authority and lifecycle state;
- firing/recoil-transient flag only as an estimator update-quality gate.

Outputs:

- normalized relative horizontal velocity;
- estimated left-stick mobility gain;
- mobility-prior confidence;
- target-drift estimate and confidence;
- projected horizontal lead in pixels;
- estimator state and rejection reason for diagnostics.

The observer never consumes recoil as evidence of left-stick mobility and never
modifies recoil behavior. Existing tracker control attribution remains the
source for camera-motion compensation. If reliable pre-recoil attribution is
not available for a sample, the observer freezes gain learning for that sample
and may still decay its prior; it does not infer a new gain from final output.

### 2. BodylockPlanner

`NativeAiAim` remains the natural integration point, but its BodyLock work is
conceptually one planner. It combines:

- current body-relative position error;
- trusted projected relative motion from the observer;
- existing horizontal/vertical strength and maximum-force bounds;
- one continuous confidence/motion scale;
- one continuous manual-priority rule;
- one soft error deadband around the body-lock point.

Internally the planner keeps the reason for the request explicit:

```text
desired_assist = position_feedback + trusted_motion_feedforward
```

That decomposition is not another output stage. It allows the ADS-to-BodyLock
handoff to reduce a closing position-feedback component without deleting the
feed-forward needed for target motion or player strafe. The planner returns one
combined `desired_assist` vector and does not retain a second output-smoothing
history.

### 3. AssistEnvelope

`NativeAimAssistDynamics` becomes the only owner of delivered AI continuity.
For BodyLock it accepts `desired_assist` plus lifecycle transition state and
applies:

- bounded per-tick approach/rate limiting;
- bounded coast from the last valid plan during an identity-safe short gap;
- monotonic release to zero after safety expiry;
- bumpless reacquisition from the currently delivered value;
- bumpless ADS-to-BodyLock transfer seeded from the last actual post-ADS-brake,
  pre-recoil AI component;
- the unified manual-priority scale supplied by the arbitration contract.

It returns only the AI component. The controller adds that component to the
unmodified manual right stick. Recoil is added afterward as it is today.

### 4. Lifecycle and authority

`BodylockLifecycle` becomes a state/reason producer. It may clear target-specific
observer state on identity change and request a release, but it may not call a
generic "reset all assist history" operation.

ADS acquisition and sustained BodyLock use separate evidence policies:

- ADS acquisition still requires current evidence and retains its ADS-only
  near-target/carry/output-validation behavior.
- ADS completion also requires terminal-approach evidence: centered position
  alone is insufficient while the AI-controlled closing component is still
  predicted to cross the target outside the accepted budget.
- BodyLock may use only an already-owned same track during the bounded
  continuity window.
- ADS-only brake stages must bypass BodyLock output and must never alter its
  requested or delivered assist after ownership changes. Their last actual
  post-brake, pre-recoil AI value is nevertheless handed to the shared envelope
  as the continuity seed; this is state transfer, not continued ADS authority.

## Relative-Motion Observer Algorithm

### Coordinate and scale

For a valid body box, define horizontal normalized error:

```text
e = (target_center_x - reticle_x) / max(body_height_px, h_min_px)
```

Using body heights instead of raw pixels makes the mobility prior less sensitive
to distance and resolution. The body-height lower bound is only numerical
protection; invalid or implausibly small geometry is rejected rather than used
for learning.

For two fresh observations of the same selected track, the exogenous relative
rate used for learning is the target-box displacement plus the existing
tracker's attributed camera displacement:

```text
camera_compensated_relative_rate =
    ((target_center_x_now - target_center_x_previous) + attributed_camera_dx)
    / (observation_dt * mean_body_height_px)
```

Equivalently, an existing tracker backend may supply its camera-attributed
horizontal target velocity divided by current body height. The raw change in
`e` remains useful as a diagnostic, but it is not used alone to learn mobility
while the right stick is moving. This prevents the observer from learning its
own camera correction as player-strafe response.

The accepted `observation_dt` range is approximately 4-40 ms. The exact bounds
are constants covered by tests, not user-facing tuning knobs. Duplicate or
out-of-order sequences, non-positive timestamps, a track-id change, and invalid
body geometry do not produce a velocity update.

The existing tracker already records manual, AI, dynamics, recoil, and final
right-stick components separately. Observer integration must use its
camera-attributed/pre-recoil motion rather than treating recoil or duplicate
controller ticks as target velocity.

### Low-order model

The horizontal model is deliberately two-dimensional in state, not 3D:

```text
relative_rate = target_drift - strafe_gain * shaped_left_x + noise
```

- `target_drift` is the target's residual normalized lateral motion for the
  currently selected track.
- `strafe_gain` is the effective normalized screen-motion response per unit
  left-stick input under the current ADS/stance/scope combination.
- `shaped_left_x` is the same physical intent signal used for output, after
  normal deadzone/curve handling.

The sign convention is fixed by deterministic simulator tests. Production code
does not guess sign from one frame.

This model is sufficient for the required behavior:

- stationary target plus player strafe produces a learned opposing component;
- same-direction target/player motion leaves the observed speed difference;
- opposite-direction motion adds, but remains inside existing BodyLock force
  limits;
- synchronized motion approaches zero feed-forward instead of applying
  `left_x * constant` blindly.

### Robust online update

The estimator uses a bounded recursive update with clipped innovation. A full
Kalman or EKF implementation is unnecessary; the state and covariance can be
represented by fixed scalar values.

On each eligible fresh frame:

1. Form normalized measured relative rate.
2. Predict it from `target_drift`, `strafe_gain`, and `shaped_left_x`.
3. Clip the innovation to a geometry-normalized maximum so a box jump cannot
   rewrite the prior.
4. Update target drift slowly when left intent is steady or near zero.
5. Update strafe gain mainly when left input has meaningful magnitude or a
   meaningful onset/reversal delta, because those frames make the two effects
   identifiable.
6. Increase confidence only after consecutive compatible fresh frames.
7. Collapse confidence quickly after consecutive incompatible innovations;
   cap feed-forward while validating the replacement gain.

Learning is frozen when any of the following is true:

- the observation is not fresh;
- the selected identity is missing or changed;
- body geometry is invalid;
- observation timing is outside bounds;
- pre-recoil camera attribution is unreliable;
- firing/recoil transients make the sample ambiguous;
- the input is inside the left-stick drift/deadzone region.

Freezing a sample is not an output brake. The planner can still use a decayed
last trustworthy estimate if lifecycle authority permits it.

### Estimator states and memory lifetime

The observer exposes four states:

- `Cold`: no usable mobility prior; output uses measured relative motion at a
  conservative cap.
- `Validating`: a prior exists but current ADS behavior has not confirmed it;
  feed-forward is confidence-capped.
- `Warm`: consecutive fresh evidence matches the gain; normal bounded
  feed-forward is available.
- `Rejected`: current evidence conflicts with the prior; confidence collapses
  and the observer relearns without a discontinuous output reset.

Memory rules:

- releasing ADS freezes and gradually decays mobility confidence instead of
  deleting the prior;
- a new ADS hold revalidates the prior in roughly 40-80 ms;
- a matching response warm-starts the prior;
- a changed weapon/stance/scope is detected only as gain mismatch, rejected in
  2-3 fresh frames, and relearned—no identity lookup is required;
- a selected-track change clears target drift/history but retains the mobility
  prior at low confidence;
- process restart clears all learned state;
- no learned value is written to disk or configuration.

### From estimate to desired assist

The observer does not directly emit stick force. It projects a virtual
horizontal lead:

```text
lead_body_heights = trusted_relative_rate * lead_horizon_seconds
lead_px = lead_body_heights * current_body_height_px
```

The lead horizon reuses the existing bounded motion-lead timing. `lead_px` is
clamped by the existing projection/lead bound before the planner adds it to the
position error. The planner then runs the normal pixels-to-stick map and
existing BodyLock maximum-force cap.

This preserves one force authority path and prevents a learned gain from
bypassing BodyLock strength or suddenly commanding full stick.

## Lifecycle and Safety State Machine

The externally meaningful states are:

### Tracking

The selected track is current and geometry is valid. Planner and envelope run
normally. Fresh observations update the observer; duplicate controller ticks
only project the last estimate.

### GeometryGrace

The same selected track is still authoritative but body geometry is briefly
invalid or missing. For at most 40 ms:

- observer learning is frozen;
- the planner reuses the last valid geometry projection with decaying
  confidence;
- the envelope keeps output continuous and bounded;
- recovery of valid geometry resumes without a cold output reset.

### Coast

The current observation is briefly absent but authority explicitly confirms
same-track continuity. For at most the existing 96 ms continuity window:

- no new target or mobility evidence is learned;
- the last valid desired assist is projected conservatively and decays
  monotonically;
- existing BodyLock maximum-force and tick-delta bounds still apply;
- remaining detector candidates cannot replace the missing selected target.

### Release

Identity is unconfirmed, authority rejects the target, the selected track
changes without handoff, or the safe continuity window expires:

- the planner contributes no new compensation;
- the envelope decays only the previously delivered AI component to zero;
- zero is reached within 40 ms;
- manual right-stick and recoil remain immediate and unfiltered.

### Reacquire

A valid selected target returns:

- target-specific drift state starts from the new track's evidence;
- mobility prior enters `Validating` unless current evidence already matches;
- desired assist may resume immediately at a conservative cap;
- the envelope approaches it from the currently delivered value, not zero or a
  reset history value;
- useful assist returns within 60 ms without a large first-tick jump.

Transitions are explicit in telemetry. No other component may independently
invent a BodyLock coast, release, or reacquire brake.

## ADS-to-BodyLock Handoff Contract

ADS acquisition and BodyLock have different jobs: ADS moves toward the target,
while BodyLock maintains relative alignment after acquisition. The mode switch
must therefore be based on both position and approach state.

### Completion eligibility

`AdsCompletionGate` keeps its distinct-fresh-frame requirement, but a centered
frame counts only when all of the following are true:

- the selected target identity and current evidence are valid;
- the target lies inside the configured completion radius;
- the existing manual crossing-brake condition is not active;
- camera-attributed closing rate is finite and inside the terminal budget;
- the AI position-closing component is already decreasing, or its projected
  time-to-cross stays inside the accepted no-overshoot envelope.

The terminal check consumes fresh observations and the actual pre-recoil output
components already available in the controller. It does not infer velocity from
duplicate 1000 Hz ticks. A timeout may end aggressive ADS acquisition, but it
does not waive safe handoff: the planner/envelope enters the same bounded
terminal transfer instead of jumping directly to unrestricted BodyLock.

### Transfer payload

On the last ADS tick, the controller records a compact payload:

- selected track id and vision sequence;
- body-relative error and camera-attributed closing rate;
- ADS position-closing AI component;
- actual post-ADS-brake, pre-recoil AI component;
- transition timestamp and completion reason.

Manual input is not included in the AI seed. Recoil is excluded because it is
added after the handoff boundary.

### First BodyLock ticks

The BodyLock planner separates position feedback from trusted motion
feed-forward:

- motion feed-forward may continue when it is supported by fresh target/player
  relative motion;
- position feedback may not increase in the old closing direction while the
  projected error would cross the lock point outside the terminal budget;
- after a real error sign change, stale position feedback decays through zero
  before it can reverse;
- the envelope begins from the last actual ADS AI seed and approaches the new
  combined desired assist under the ordinary tick-delta/jerk bounds.

The terminal rule is a continuous cap inside the existing planner, not a new
`AdsCarryBrakePolicy` pass and not a timer that blindly zeros all BodyLock force.
It naturally releases when fresh approach evidence is safe. For a stationary
target with no manual input it prevents transition-attributed overshoot; for a
moving target it retains only the feed-forward required to match measured
relative motion.

## Simplification and Migration Map

The implementation must remove or merge existing transformations as follows:

| Existing behavior | New owner | Required change |
| --- | --- | --- |
| BodyLock motion observer updated each control tick | `RelativeMotionObserver` | Fresh sequence/timestamp gate; update only once per vision frame |
| Axis zero-cross hold | `BodylockPlanner` | Replace discrete hold state with a continuous soft deadband |
| Motion lead | observer + planner | Observer estimates lead; planner is the only force mapper |
| Vertical-tail and stabilization boosts | `BodylockPlanner` | Merge into one bounded continuous confidence/motion scale |
| `NativeAiAim` BodyLock smoothing | `AssistEnvelope` | Remove planner-local output history |
| Manual overlap/takeover/escape paths | unified manual arbitration | One continuous manual-priority rule; no multiple stateful zeroing paths |
| Lifecycle generic history reset | lifecycle state + explicit component reset | Reset target estimator separately; envelope releases rather than jumps |
| ADS near/carry/output validation | ADS only + explicit handoff payload | These stages cannot mutate BodyLock after the switch; final ADS AI and approach state seed the planner/envelope transfer |
| Recoil | recoil boundary | Remains the final independent feed-forward component |

`BodyLockShortPlanPolicy` is currently bypassed when `last_mode()` is
`body_lock`; its BodyLock-sounding name and unreachable residual branch should
not become another sustained-aim owner. Implementation should either rename it
to its actual ADS role or remove the dead BodyLock branch during the same
refactor, provided its ADS behavior remains covered by tests.

## Manual Arbitration

Manual intent has two independent meanings and must not be conflated:

- left stick is an estimator input for player-relative lateral motion;
- right stick is direct camera intent and always remains under the player.

For right-stick arbitration, the planner computes the desired AI vector first.
A single continuous priority function then scales only the AI component based on
manual magnitude and whether AI helps or opposes the current correction. It must
preserve these properties:

- useful same-direction assistance may remain within the normal authority cap;
- opposing AI falls continuously as deliberate manual input grows;
- there is no hard one-tick BodyLock cutoff at a threshold;
- drift-sized manual input is not treated as takeover;
- final composition is `manual + delivered_ai`, clamped once;
- manual input itself is never smoothed or rate-limited.

The implementation plan must identify one canonical function for this rule and
delete/bypass redundant BodyLock manual escape/takeover mutations.

## Smoothness Contract

Smoothness applies to the AI component, not to the entire output.

- At 1000 Hz, BodyLock's delivered AI approaches its desired value through one
  envelope with an explicit delta/rate bound.
- Coast and Release are monotonic in AI magnitude unless a fresh valid target
  changes the desired direction through Reacquire.
- A sign change crosses a continuous deadband; it cannot alternate between two
  nontrivial signs on successive ticks.
- Reacquire begins from the currently delivered AI value.
- ADS-to-BodyLock begins from the actual last post-brake ADS AI value; the mode
  label may change, but delivered AI must not reset or regain a large closing
  component.
- Manual right-stick changes and recoil feed-forward are not delayed to make a
  smoothness metric look better.
- Final-output clipping must be attributed separately so saturation is not
  misclassified as envelope jitter.

The accepted benchmark baseline for ordinary BodyLock has P95 AI tick delta
`0.035`. This change must remain within 5% of that baseline, while lifecycle
edges obey their separate maximum-delta requirement.

## Configuration Compatibility and Complexity Budget

### Existing configuration

Existing configuration must not silently change meaning or stop parsing:

- `[gamepad.bodylock].smoothing` and the legacy
  `gamepad.ai_aim.body_lock_smoothing` remain accepted aliases. Their value now
  configures the BodyLock portion of the single assist envelope instead of
  planner-local smoothing.
- `manual_escape_threshold` / `body_lock_manual_escape_input_threshold` map to
  the onset of the unified manual-priority curve.
- `manual_escape_preservation` /
  `body_lock_manual_escape_preservation` map to that curve's deliberate-manual
  preservation floor/shape. They must not activate a separate escape stage.
- existing manual-takeover keys remain accepted for compatibility. If the
  unified curve makes a field redundant, the parser stores it and emits at most
  one deprecation diagnostic; runtime must not create a second state machine.
- existing `lead_strength`, projection age, lead horizon, and lead maximum are
  reused where their current semantics are compatible.

Configuration tests must prove that the current `config.toml` and
`testdata/legacy_full_config.toml` still parse and produce equivalent bounded
authority.

### New configuration

At most three new user-facing knobs are permitted:

- geometry grace duration, default 40 ms;
- safe release duration, default 40 ms;
- reacquire blend duration, default no more than 60 ms.

The existing authority continuity value supplies the 96 ms Coast limit; it is
not duplicated. Estimator learning rates, innovation clips, valid timestamp
bounds, and confidence frame counts are internal constants first. They may
become configuration only after benchmark evidence shows a real need.

ADS-handoff terminal eligibility reuses the existing completion radius,
camera-attributed motion, and bounded projection horizon. Its initial
time-to-cross and overshoot budgets are benchmarked internal constants, not a
fourth user-facing knob.

### Hot-loop budget

- at most one new estimator source/header pair, or an equivalent refactor of
  the current BodyLock motion policy;
- O(1) state and arithmetic per controller tick;
- estimator math runs only on fresh observations;
- no allocation, container growth, locks, file I/O, model invocation, or log
  formatting in the 1000 Hz path;
- no new output-mutation stage in `NativeGamepadController`;
- diagnostic emission is sampled through the existing telemetry cadence.

## Diagnostics

Add only enough observability to prove ownership and diagnose a mismatch:

- observer state (`cold`, `validating`, `warm`, `rejected`);
- estimated `strafe_gain` and confidence;
- normalized relative rate and projected lead;
- lifecycle state/reason (`tracking`, `geometry_grace`, `coast`, `release`,
  `reacquire`);
- ADS handoff eligibility, closing rate, position-feedback component, and
  completion/hold reason;
- desired versus delivered BodyLock AI;
- fresh sequence consumed and estimator rejection reason.

No new large telemetry subsystem or per-tick unbounded trace is introduced.
Benchmark JSON may record full deterministic traces because it is offline.

## Benchmark and Test Strategy

Implementation follows test-driven development. The evidence benchmark remains
RED under `--require-fixed` until each production behavior is implemented.

### Real-frequency simulator matrix

The left-stick closed-loop fixture changes from 100 Hz control to 1000 Hz
control. Vision delivery runs at:

- 80 Hz and 100 Hz as primary live-frequency gates;
- 50 Hz as low-rate stress;
- 160 Hz as high-rate stress.

Each matrix run must preserve capture timestamps and sequences, deliver the same
box for all intervening control ticks, and assert that the estimator consumes
each sequence exactly once.

Scenarios cover:

- cold left-stick onset;
- warm onset, reversal, and release;
- stationary target;
- target moving in the same direction slower than the player;
- target/player synchronized motion;
- target moving in the opposite direction;
- bounded deliberate right-stick correction;
- ADS release/re-entry with matching mobility;
- unannounced mobility-gain change representing weapon/stance/scope change;
- firing/recoil transient while learning is frozen;
- ADS acquisition arriving near center with high AI-driven closing velocity,
  followed by the ADS-to-BodyLock mode switch;
- the same handoff while trusted target or player relative motion requires a
  non-zero feed-forward component;
- invalid body geometry and short same-track observation loss;
- long identity loss with other detector candidates present;
- reacquisition of same identity and a different identity.

### Production-chain benchmark semantics

The evidence-matched production-chain probe is split into four independently
scored safety behaviors:

1. A short same-track gap requires continuous bounded Coast output.
2. A long or unconfirmed identity loss requires smooth safe Release to zero.
3. Valid reacquisition requires bumpless restoration.
4. Other candidates while the original selected target is absent must not
   receive blind assistance.

This replaces the ambiguous old expectation that any candidate presence should
prevent a zero-assist gap.

### Output-ownership tests

Focused tests instrument every controller stage and assert:

- BodyLock desired AI changes only in the planner;
- BodyLock delivered AI changes only in the envelope;
- lifecycle changes state/reason but performs no stick arithmetic;
- ADS near-target brake, ADS carry brake, short-plan policy, and output
  validation do not mutate BodyLock;
- the last actual ADS AI component seeds the first BodyLock envelope tick;
- transition terminal control reduces only position feedback and preserves
  trusted motion feed-forward;
- the unified manual rule is the sole BodyLock manual arbitration path;
- recoil remains separately attributed and is added after AI/manual composition;
- manual and recoil discontinuities are excluded from AI smoothness scoring but
  remain present in final output.

### Existing regression suites

All existing ADS, BodyLock, slide/jump/crouch, manual takeover, recoil,
tracker-authority, pipeline-contract, runtime-config, controller behavior,
metrics, and native gamepad self-tests remain green. Small/far target tests must
show no increase in authority or force.

The existing test whose expectation says ADS carry brake must allow bounded
BodyLock overshoot is replaced by two narrower contracts: ADS policy cannot
mutate BodyLock after ownership changes, and the planner/envelope handoff itself
must prevent transition-attributed overshoot while preserving motion
feed-forward.

## Acceptance Criteria

### Motion response

- Cold left-stick response begins within 80 ms.
- Warm left-stick onset or reversal begins within 20 ms.
- Left-stick release removes its feed-forward contribution within 40 ms.
- Synchronized player/target motion produces near-zero feed-forward.
- Same-direction motion compensates only the measured speed difference.
- Opposite-direction motion produces stronger compensation but never exceeds
  existing BodyLock maximum authority.
- Frames where AI opposes committed manual correction: 0.
- Frames where final output opposes trusted relative-motion correction: 0.
- At 80 Hz and 100 Hz vision, fast-strafe mean and P95 error improve by at least
  20% against the committed RED artifact.
- The same-direction scenario regresses by no more than 5%.

### Smoothness

- P95 delivered BodyLock AI tick delta is no more than 5% above the accepted
  `0.035` baseline.
- Maximum delivered-AI tick delta during Coast, Release, and Reacquire is no
  more than `0.07`.
- No repeated sign flips with magnitude greater than `0.03`.
- No BodyLock AI spike of magnitude `0.20` or greater caused by a lifecycle or
  duplicate-frame transition.
- Manual and recoil remain unsmoothed and independently attributed.

### ADS-to-BodyLock handoff

- A stationary target with zero manual right-stick input has no error crossing
  greater than 2 px attributable to the mode transition.
- A centered ADS sample does not complete acquisition while the AI position
  component is still predicted to carry through the lock point outside that
  budget.
- The last ADS tick to first BodyLock tick obeys the `0.07` delivered-AI delta
  bound and never resets to zero solely because the mode label changed.
- During terminal transfer, the closing position-feedback magnitude is
  non-increasing until approach evidence is safe.
- A moving-target or left-strafe fixture retains the trusted feed-forward needed
  for relative-motion continuity; it is not forced to zero by proximity alone.
- Overshoot scoring distinguishes player-owned manual carry from AI-attributed
  transition output. Manual input remains unchanged.

### Lifecycle safety

- Same-track loss within the existing 96 ms authority-continuity window uses
  bounded Coast rather than immediate manual/reset.
- A geometry anomaly of at most 40 ms uses monotonic grace/coast and resumes
  without a cold output reset.
- Beyond the safety window the planner creates no new compensation and the
  delivered AI reaches zero within 40 ms.
- No remaining detector candidate is followed without selected-target identity
  authority.
- Reacquisition has no large first-tick jump and returns useful assist within
  60 ms.
- A mobility-gain mismatch rejects the warm prior in 2-3 fresh frames.

### Frequency and determinism

- Primary gates pass at 1000 Hz control with 80 Hz and 100 Hz vision.
- Stress fixtures are deterministic at 50 Hz and 160 Hz vision.
- Duplicate sequences never update velocity, gain, confidence, or target drift.
- Two benchmark runs produce identical scored JSON apart from explicitly
  excluded metadata.

## Failure Handling

- Non-finite observer state immediately invalidates the estimate, retains no
  projected lead, and asks the envelope for normal safe Release; it never sends
  a full-force fallback.
- Invalid geometry freezes learning and enters GeometryGrace only when identity
  authority allows it.
- Timestamp/sequence anomalies reject the sample without resetting delivered
  output.
- Gain mismatch reduces confidence before it changes force authority.
- Identity loss always dominates estimator confidence: even a warm mobility
  prior cannot authorize target-specific assistance.
- A disabled or unavailable observer leaves position-error BodyLock functional
  through the same planner/envelope path, providing a controlled rollback
  without restoring duplicate smoothing/brakes.

## Expected Implementation Areas

The implementation plan is expected to touch these areas, subject to exact TDD
test placement:

- `native/controller_native/ai_aim.*`: expose one BodyLock planner path and
  remove local BodyLock output smoothing/redundant manual mutation;
- current BodyLock motion policy or a focused `relative_motion_observer.*`:
  fresh-frame normalized estimator and in-memory mobility prior;
- `native/controller_native/aim_assist_dynamics.*`: single BodyLock envelope,
  coast/release/reacquire and ADS-handoff continuity;
- `native/controller_native/ads_completion_gate.*`: fresh-frame
  terminal-approach eligibility in addition to centered position;
- `native/controller_native/bodylock_lifecycle.*`: state/reason transitions and
  targeted reset semantics;
- `native/controller_native/native_gamepad_controller.*`: pass left intent and
  fresh-frame/identity inputs, split ADS versus BodyLock evidence, preserve
  output ordering;
- `native/controller_native/runtime_config.*`: compatible alias mapping and at
  most three lifecycle timing fields;
- focused unit tests, controller behavior tests, and
  `left_stick_motion_defect_benchmark.*` for 1000 Hz/frequency-matrix gates.

## Completion Definition

The change is complete only when the RED benchmark passes `--require-fixed`,
all acceptance gates above pass, existing regressions remain green, and stage
traces demonstrate that BodyLock has exactly two output owners: planner for
desired assist and envelope for delivered assist. Passing only the synthetic
motion cases is insufficient if production-chain Coast/Release/Reacquire or
manual/recoil ownership remains ambiguous.
