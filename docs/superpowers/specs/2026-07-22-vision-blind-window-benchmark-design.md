# Vision Blind-Window Control Benchmark Design

Date: 2026-07-22  
Status: proposed for implementation  
Scope: deterministic benchmark and evidence contract only; no production control change

## 1. Purpose

The controller runs faster than Vision, but a high controller tick rate does not create new information. Between a frame's capture and the next committed fresh observation, the controller may repeatedly act on an old target state while previously delivered stick input has not yet appeared in visual feedback.

This benchmark must answer two questions:

1. Does response/delay/pending-aware control reduce the future burden created inside this blind window?
2. Does it improve control by using available causal information rather than by globally weakening AI output or reading future observations?

The benchmark is an optimization instrument. It is not a new product feature, runtime gate, or substitute for live feel validation.

## 2. Evidence basis and limitations

The initial timing envelopes come from the 2026-07-19 telemetry session `20260719T115058Z_60544_1`:

```text
391,355 controller samples
95,992 control-response windows
3,010 target events
70,043 input events
```

Observed repeated-error durations during active assistance:

| Mode | P50 | P90 | P95 | P99 |
|---|---:|---:|---:|---:|
| ADS | 9.0 ms | 15.8 ms | 22.0 ms | 70.0 ms |
| BodyLock | 10.0 ms | 15.7 ms | 19.0 ms | 56.5 ms |

Observed coast burst durations:

| Mode | P50 | P90 | P95 | P99 |
|---|---:|---:|---:|---:|
| ADS | 9.0 ms | 44.0 ms | 65.5 ms | 90.2 ms |
| BodyLock | 5.0 ms | 26.7 ms | 41.5 ms | 83.9 ms |

The session is not an authoritative current baseline because it used `640x512`, declared 160 Hz capture, had unknown revision/config/engine hashes, omitted left-stick state, and did not populate exact capture age in controller records. It supplies scenario shapes and duration envelopes only. Exact delay and response ground truth comes from the deterministic plant; current runtime calibration requires new G0 telemetry.

## 3. Definitions and coordinates

Use stable-centered screen pixels and monotonic nanoseconds.

```text
e(t)        ground-truth target minus reticle error in stable pixels
a(t)        base AI proposal before manual fusion and recoil, stick units
m(t)        physical/manual right-stick input, stick units
l(t)        physical left-stick input, stick units
u(t)        final delivered right-stick state, stick units
R(t)        local right-stick-to-reticle response, px / stick / s
L(t)        local left-stick-to-target-relative-motion response, px / stick / s
p(t)        predicted reticle motion from delivered input not yet visually realized, px
c_k         capture time of frame k
r_k         time frame k becomes causally available to the controller
T_v         nominal Vision capture period
```

`R*u` is reticle motion. Therefore the control contribution to error rate is `-R*u`. Positive `dot(unit(e), R*u)` closes current error.

The benchmark plant runs at 1 kHz. Vision capture, result availability, target motion, user input, and delivered-control response are separate clocks.

## 4. Blind-window episode

A synthetic episode begins with a valid committed target and a controller proposal. A controlled event occurs after frame `k` has been captured but before frame `k+1` is causally available.

```text
capture k                         c_k
event                             c_k + phase * T_v
capture k+1                       c_k + T_v
result k+1 becomes available      r_(k+1)
```

The primary blind interval is:

```text
[event_time, r_(k+1))
```

The controller may not read target state, viewport state, or ground truth generated after the latest available result. Ground truth is used only by the plant and hindsight metrics.

## 5. Four knowledge classes

### K1: Self-predictable

The target is stationary or smoothly moving. The hidden future burden comes mainly from the controller's own already-delivered input and plant delay.

Expected result: response/pending-aware control must clearly improve.

### K2: Input-observable

The next Vision result is unavailable, but a new manual-right or left-stick input is already visible at controller rate.

Expected result: the new policy must reduce stale AI conflict and future correction burden without declaring that the user is always correct.

### K3: Externally unobservable

The target changes direction, jumps, or falls immediately after capture without prior causal evidence.

Expected result: the policy may reduce accumulated pending debt, but it must not appear to predict the surprise. Large oracle-like gains are treated as possible future leakage.

### K4: Observation-invalid

The next result contains an identity switch, association jump, reused frame, coordinate fault, or implausible geometry flip.

Expected result: the learner performs no identification update, clears interval pairing where required, lowers confidence, and avoids an output discontinuity.

## 6. Required first fixtures

### 6.1 `bodylock_pending_crossing_track_6671`

Derived shape:

```text
error (-0.25, -16.05), AI (+0.114, +0.388)
approximately 15 ms without a trustworthy new correction
subsequent error/AI direction reversal and center crossing
```

The cleaned fixture uses a stationary or constant-velocity target. Previously delivered control alone is sufficient to cross the target during the blind window. This isolates K1 pending-motion benefit from target prediction.

### 6.2 `ads_manual_rescue_track_1429`

Derived shape:

```text
large ADS error
manual and AI strongly oppose
AI proposal remains strong while the user has already begun correction
```

The event is manual input onset after capture. The plant consumes final delivered output, never raw `ai_x`; the old log shows AI components can exceed the final stick range.

### 6.3 `left_stick_onset_reversal`

The existing log does not contain left-stick state, so the first fixture uses the project's established left-stick benchmark distributions. It covers onset, release, reversal, same-direction manual correction, and opposing manual correction. New G0 logs later replace amplitudes and durations with real distributions without changing fixture semantics.

### 6.4 `invalid_observation_flip_track_2989`

Derived shape:

```text
Y error approximately -39 -> +33 -> -3 -> +42 -> -3 px
target-switch/association evidence nearby
```

This is K4. It must not be treated as learnable target kinematics or response evidence.

### 6.5 Dropout envelope

Every fixture also runs with:

```text
normal blind interval      9-16 ms
stress blind interval      20-45 ms
dropout blind interval     65-90 ms
```

Dropout is a test dimension, not a separate total-score bonus.

## 7. Phase and timing matrix

Required capture rates:

```text
80 Hz     T_v = 12.5 ms
100 Hz    T_v = 10.0 ms
120 Hz    T_v = 8.333... ms
```

Required event phases:

```text
5%, 25%, 50%, 75%, 95% of T_v after capture
```

Required result delivery profiles:

```text
fixed-low       capture-to-result 8 ms
fixed-normal    capture-to-result 16 ms
fixed-high      capture-to-result 28 ms
jittered        deterministic bounded sequence 8-32 ms
```

Required plant response profiles:

```text
right-stick delay       10, 25, 45, 70, 100 ms
anisotropic R           X/Y response differs
limited cross-coupling  off-diagonal R terms
slowdown                response scale changes near target boundary
left response           target-local L with confidence/reset boundary
```

Required seeds:

```text
1337, 7331, 20260722
```

Report worst phase, phase median, phase P95, and uniformly randomized phase. A policy cannot pass by improving only the 5% worst-case phase.

## 8. Primary metrics

All metrics are uncapped and reported per episode, cohort, phase, and seed.

### 8.1 Blind duration

```text
blind_duration_ms = (r_(k+1) - event_time) / 1e6
```

This describes exposure; lower is not a controller achievement because the policy does not control Vision timing.

### 8.2 Stale AI impulse

Let `d_stale = unit(R(event) * a(event^-))` be the reticle direction proposed immediately before the event.

```text
stale_ai_impulse_stick_ms =
    integral_blind max(0, dot(a(t), d_stale_stick)) dt
```

The implementation uses the normalized pre-event stick direction for this stick-space diagnostic. It measures how much the AI continued its previous command, not whether that command was harmful.

### 8.3 Harmful AI motion

Using ground truth only for hindsight scoring:

```text
closing_speed_ai(t) = dot(unit(e(t)), R(t) * a(t))

harmful_ai_motion_px =
    integral_blind max(0, -closing_speed_ai(t)) dt
```

This distinguishes stale-but-helpful output from stale-and-wrong output.

### 8.4 Harmful pending at reveal

At `r_(k+1)`:

```text
closing_pending_px = dot(unit(e), p)
opposing_pending_px = max(0, -closing_pending_px)
excess_closing_pending_px = max(0, closing_pending_px - norm(e))

harmful_pending_at_reveal_px =
    opposing_pending_px + excess_closing_pending_px
```

This counts both wrong-direction debt and same-direction debt large enough to overshoot the remaining error.

### 8.5 Future error burden

For horizons `H = 40, 80, 160 ms`:

```text
future_error_burden_px_ms(H) =
    integral_[reveal, reveal+H] norm(e(t)) dt
```

Report all three horizons separately.

### 8.6 Reverse correction impulse

Let `d_pre` be the normalized average AI reticle direction during the final 5 ms before reveal.

```text
reverse_correction_stick_ms(H) =
    integral_[reveal, reveal+H]
        max(0, -dot(u(t), d_pre_stick)) dt
```

Use `H = 80 ms` as primary and retain 40/160 ms diagnostics.

### 8.7 Reveal-to-reacquire

Reacquired means:

```text
norm(e) <= 8 px continuously for at least 30 ms
```

```text
reveal_to_reacquire_ms = first_reacquired_time - r_(k+1)
```

If the episode horizon expires first, record a miss plus the full horizon; do not silently drop the episode.

### 8.8 Post-cross burden

A center crossing is evaluated along the pre-cross radial error axis, not independently per X/Y axis.

```text
post_cross_area_px_ms =
    integral_after_cross max(0, -signed_radial_error(t)) dt
```

Useful lead and harmful overshoot are labelled separately using target inertia, reversal evidence, pending motion, future burden, and terminal residual.

### 8.9 User-fight area

```text
opposition(t) = max(0, -cos_angle(m(t), a(t)))

user_fight_stick_ms =
    integral min(norm(m(t)), norm(a(t))) * opposition(t) dt
```

Values below the configured physical-stick drift floor are excluded. The initial synthetic drift floor is `0.03` stick units; runtime replay uses the session's measured neutral distribution.

## 9. Guardrail metrics

### 9.1 ADS acquisition time

Time from the physical ADS epoch start to the first 30 ms continuous stay within 8 px. Report miss count and censored duration.

### 9.2 Far-error closing speed

Median radial closing speed while `norm(e) >= 40 px`. This detects a policy that wins only by globally weakening AI.

### 9.3 Useful crossing retention

The ratio of beneficial lead/crossing episodes retained relative to baseline. A crossing is beneficial only when it lowers 80 ms future burden and terminal residual while target inertia remains consistent.

### 9.4 Output total variation

```text
output_total_variation = sum norm(u_i - u_(i-1))
```

Also report per-second normalization and P95 single-tick delta. This is the benchmark smoothness/jerk guardrail.

### 9.5 Incorrect interruption

An interruption begins when AI magnitude falls below `0.05` for at least 10 ms while:

```text
norm(e) > 8 px
ground-truth closing assistance remains useful
manual escape/takeover is not active
target identity remains valid
```

Report count and total interrupted milliseconds.

### 9.6 Identity and authority violations

Count any candidate evaluation or learning update that crosses target identity, ADS epoch, invalid coordinate, fire authority, or output-delivery boundaries. Required value is zero.

## 10. Causal and anti-cheating metrics

### 10.1 Future dependency audit

Every controller decision records the maximum source timestamp it read. Required invariant:

```text
max_read_timestamp_ns <= decision_timestamp_ns
```

### 10.2 Output identity

Disabled and shadow evaluation must produce byte-identical sequences for:

```text
TargetPlan
base controller proposal
fusion decision
AutoFire decision
recoil component
final delivered output
```

### 10.3 Knowledge-class sanity

- K1/K2 may show substantial causal improvement.
- K3 must not approach hindsight oracle performance without prior maneuver evidence.
- K4 must produce zero learner updates and zero cross-boundary pairing.

### 10.4 Strength-only mutation

Compare against fixed global AI scales `0.70, 0.80, 0.90`. A new policy does not pass if its gains are matched by one global weakening scale with equal or better acquisition and guardrail metrics.

## 11. Acceptance gates

All comparisons are paired by fixture, timing profile, phase, and seed.

### 11.1 K1 acceptance

Across phase median:

```text
harmful_pending_at_reveal_px       at least 20% lower
future_error_burden_px_ms(80)      at least 10% lower
reverse_correction_stick_ms(80)    at least 15% lower
reveal_to_reacquire_ms             at least 8% lower
```

At least four of five fixed phases must not regress future burden by more than 2%.

### 11.2 K2 acceptance

Across phase median:

```text
user_fight_stick_ms                at least 15% lower
harmful_ai_motion_px               at least 15% lower
future_error_burden_px_ms(80)      at least 8% lower
```

Manual escape/takeover semantics must remain unchanged.

### 11.3 K3 acceptance

```text
phase-median future burden         no worse than 3%
worst-phase future burden          no worse than 5%
future dependency violations       zero
```

Any causal improvement exceeding 80% of hindsight headroom without pre-event maneuver evidence triggers a future-leakage audit rather than automatic acceptance.

### 11.4 K4 acceptance

```text
learner identification updates     zero
cross-identity interval pairing    zero
non-finite state                   zero
P95 output single-tick delta       no worse than 5%
```

### 11.5 Global guardrails

```text
ADS acquisition phase median       no worse than 3%
far-error closing speed            no worse than 3%
useful crossing retention          at least 98%
incorrect interruption count       no increase
identity/authority violations      zero
```

The candidate must outperform every fixed strength-only mutation on at least two primary K1/K2 metrics while satisfying all guardrails.

## 12. Report structure

No capped total score is produced. The JSON report contains:

```text
provenance
fixture_semantics_version
policy_identity
seed
timing_profile
phase
knowledge_class
primary_metrics
guardrail_metrics
causal_audit
hindsight_headroom
mutation_results
worst_episodes
acceptance_gate_results
```

Additive AimLab score may remain as a secondary compatibility metric, but it cannot override a failed primary or guardrail gate.

## 13. Implementation boundary

The first implementation adds fixtures, metrics, report schema, deterministic phase scheduling, baseline controller replay, and mutation tests. It does not implement the learner or alter production output.

Only after the baseline benchmark reliably triggers K1-K4 defects and catches timestamp/strength-only mutations may implementation proceed to response/delay/pending learning.

## 14. Failure handling

- A fixture with no baseline defect is marked `non_discriminating` and fails benchmark acceptance.
- An episode with invalid target identity is moved to K4; it is not silently included in K1-K3 averages.
- Missing causal timestamps fail provenance; result-arrival time cannot substitute for capture time.
- Missing real left-stick data keeps real-log calibration pending but does not block the synthetic K2 fixture.
- If a metric denominator is zero, report the raw numerator and `not_applicable`; never manufacture a zero-percent improvement.
- If only the worst phase improves, reject the candidate as phase-overfit.

## 15. Delivery order

```text
B0 metric/report contracts
B1 deterministic phase and timing scheduler
B2 K1 pending-crossing fixture
B3 K2 manual/left-stick fixtures
B4 K3 target-surprise fixture
B5 K4 invalid-observation fixture
B6 strength-only and timestamp mutations
B7 retained fixed-seed baseline artifact
```

After B7, review baseline discrimination before writing or integrating the causal learner.

