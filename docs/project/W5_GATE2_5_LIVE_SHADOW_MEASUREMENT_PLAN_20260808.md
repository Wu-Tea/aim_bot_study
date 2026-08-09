# W5 Gate 2.5 Live-Shadow Measurement Plan

**Status:** partially landed as the Gate2.5A default-off diagnostic; no
activation or live COD evidence. Unfinished items are listed in the
[Gate2.5A report](../../artifacts/benchmarks/w5-causal-memory-gate2_5a-20260808/GATE25A_REPORT.md).

**Date:** 2026-08-08

**Depends on:** accepted W5 Phase B bookkeeping and Gate 2 evidence package

**Activation decision:** `FAIL_FOR_ACTIVATION` until real COD evidence satisfies
the gates below

## 1. Purpose and hard boundaries

Gate 2.5 is the smallest live measurement needed to turn the Gate 2 questions
into evidence from the real game. It observes the existing Vision target
observations and the final right-stick state that was successfully delivered to
ViGEm. It does not add an optical-flow path, does not infer a second camera
motion signal, and does not replace the W5 ledger.

The observer is diagnostic-only:

- it never writes `TargetPlan`, Remaining, the fuser, ADS/BodyLock, gains or the
  final output;
- it does not choose a controller mode or change an authority value;
- candidate models are scored and reported, but no winner is fed back into the
  runtime;
- it is default-off and is enabled only for an explicitly marked calibration
  capture;
- W3 ego motion remains frozen/off, W4 learning remains frozen/off, and
  motion-primitive calibration remains deferred.

The Gate 2 artifact contains deterministic synthetic-plant pixel magnitudes.
Those values are **model-sensitivity evidence, not measured COD gameplay error**.
Gate 2.5 is the first package allowed to report real-game cohort measurements,
and it must keep the two evidence classes separate in every output.

## 2. Questions the capture must answer

### Q1 - How does COD admit ViGEm reports?

For a known successful delivery sequence, determine whether the game behaves
closer to:

1. continuous zero-order-hold (every successful report can contribute for its
   time interval);
2. latest-only at a game/camera poll (only the latest state at each poll is
   admitted); or
3. another phase-dependent/admission rule that cannot yet be represented by
   either candidate.

The deterministic test seam must cover 180 Hz and 240 Hz poll hypotheses and a
full phase sweep. A steady held-X control is required; onset, a pulse entirely
between polls, and a +X to -X reversal are separate test cases. Live capture is
passive: it scores the actual delivered final values and their observed phase
bins, and does not assume that a person can create an exact between-poll pulse
or align a transition to a 1 kHz tick. Delivery success plus a target-anchor
change is an observed-effect candidate, not proof of game admission; the final
report must retain `UNRESOLVED` when no valid effect endpoint is available.

### Q2 - What response is applied at effect time?

Measure whether the response used by the game/controller changes during:

- a 260 ms ADS ramp;
- a 400 ms ADS ramp;
- near-target slowdown while a command crosses far-to-near and near-to-far
  regions;
- a target switch where a command is delivered under one response state and
  could take effect under another.

Compare delivery-time response metadata with the response state that can be
joined to the later observed effect. `result_ready_ns` is not an effect
timestamp. If a physical-effect timestamp cannot be established, report
processing delay and response-state evidence separately and mark the applied-
time conclusion `INSUFFICIENT_EVIDENCE` rather than relabeling result time.

### Q3 - What deadzone and axis response does the game apply?

Estimate radial deadzone, per-axis gain and diagonal behavior from clean
single-target actuator cohorts. Cover 0, 3 and 5 percent deadzone hypotheses;
1, 2, 3, 5 and 10 percent *measured final-output bins*; X-only, Y-only,
diagonal, saturation and immediate reversal. Exact stick levels belong to the
deterministic test seam. Test a scalar response model against an asymmetric X/Y
model and retain `MODEL_GAP` when the observations cannot distinguish game
deadzone from controller response or target motion.

### Observed effect signal without optical flow

The no-optical-flow effect signal is a paired displacement of the same accepted
target observation. For two compatible present endpoints `p` and `c`, define:

```text
delta_target_screen = anchor_screen(c) - anchor_screen(p)
neutral_baseline_delta = neutral_baseline(target/profile/viewport, p, c)
observed_camera_work_screen = -(delta_target_screen - neutral_baseline_delta)
```

`anchor_screen` is the existing target aim anchor in local screen coordinates
(screen +X right, screen +Y down). A stationary target anchor moves opposite the
camera on both axes, so the complete two-dimensional negation is required. The
resulting `observed_camera_work_screen` is screen-space camera work and is the
quantity comparable to W5 `realized_px`/`pending_total_px`; production delivery
reports the same camera-work convention as `{curve_x*scale, -curve_y*scale}`.
The record stores
`camera_motion_sign_convention=target_screen_delta_to_camera_work_screen_negation`
and the raw target-screen delta separately. It must not substitute a one-axis
stick/control flip. If another consumer later needs controller-stick
coordinates, it must apply a separately named, full two-axis conversion; that
conversion is not used for W5 ledger scoring. The sign is validated by a
deterministic known-direction control before live collection and is not inferred
from an arbitrary target transition.

The neutral baseline is a separate, same-identity/same-profile cohort collected
with final right stick neutral and no left stick, firing or recoil. It estimates
detector jitter and target drift for the same present cadence using robust median
and MAD/P95 summaries. A target is not required to keep its anchor fixed: a
right-stick camera command is expected to move that anchor. Instead, pose/box
scale/aspect/available detector attributes, identity/generation, viewport, ADS
and FOV must remain compatible; any residual drift not explained by the neutral
baseline rejects the actuator window as `target_motion_contamination`.

An observed effect is valid only when both fresh endpoints, the identity and the
baseline are valid, and the command-induced displacement clears the noise floor:

```text
effect_snr = norm(observed_camera_work_screen) / max(noise_floor_px, epsilon)
effect_valid = endpoint_valid && baseline_valid && effect_snr >= min_effect_snr
```

`min_effect_snr` and the minimum absolute margin are provisional and reported in
the artifact. A micro/deadzone segment that does not exceed the neutral noise
floor is `INSUFFICIENT_EVIDENCE`, never a zero-effect claim. This signal is named
`observed_effect`, not `game_admitted_observation`; its vector is
`observed_camera_work_screen`. It is an observed camera-work candidate used to
score admission/response models, not a direct game-poll truth label. The px
values are local near-center screen-coordinate
effects for one fixed viewport/FOV/zoom/profile. They cannot be compared across
FOV, resolution, zoom or ADS profiles without an explicit coordinate conversion;
otherwise the cohort is `profile_identity_incomplete`.

## 3. Data sources and strict separation of signals

The plan reuses data already owned by the native pipeline:

### Source observations

For each accepted fresh Vision observation, use the existing fields when
available:

- `frame_id`, `source_observation_id`, persistent target id and selector
  generation;
- target box/anchor, target size, raw error and reliability/confidence;
- physical ADS epoch and target acquisition id;
- capture/result/publish/controller-consume stage timestamps;
- source-present raw QPC, QPC frequency and calibrated steady endpoint data.
- the manual game-profile identity supplied by the calibration manifest when
  available: sensitivity, response curve, ADS multiplier, FOV, resolution/
  viewport, weapon/optic and nominal ADS time.

An unavailable or invalid field is a join rejection, not a guessed value. No
frame-local detection id is promoted to persistent identity. OCR/profile reading
remains off; if the operator cannot supply a complete profile manifest, the
session is kept in a separate `profile_identity_incomplete` bucket and is not
mixed across sensitivity, FOV, zoom or weapon/optic settings.

### Final actuator history

Use the successful final right-stick delivery history after all target-relative
fusing, ADS/BodyLock shaping, recoil and output limiting. Each sample needs:

- `controller_tick_id`/`tick_id` and delivery timestamp;
- final `right_x/right_y`, including a successful zero neutral;
- `delivered=true`, output-enabled state and physical actuator/backend epoch;
- response curve, response scale/confidence and validity/reason as recorded by
  the existing W5 shadow seam;
- the source observation/acquisition ids only as attribution metadata.

Failed updates, output-disabled intervals without a known successful neutral and
backend disconnects are not physical zero. They create an explicit invalid
boundary. A successful neutral is a zero-valued delivered sample and remains
part of the physical history. The plan must not use raw manual or shaped AI as
separate forces; the final delivered vector is the actuator proposal being
measured.

### Exogenous signal boundary

Left-stick strafe, firing/recoil, target rotation and target motion are recorded
as exclusion labels or separate exogenous cohorts. They are not silently folded
into actuator response residuals. No optical flow is required or permitted for
the actuator-calibration cohort.

## 4. Clock, identity and join contract

### Clock fields

The compact record must preserve clock domains instead of flattening them:

| Field | Meaning | Use |
| --- | --- | --- |
| `source_present_qpc`, `qpc_frequency` | raw DXGI present endpoint | source provenance only |
| `source_present_steady_ns` | same endpoint after paired QPC-to-steady calibration | interval endpoint when calibration is valid |
| `captured_at_ns` | capture/copy endpoint, if defined by the existing source | capture processing timing |
| `result_ready_ns` | Vision result-ready/submission stage | processing delay only, never effect time |
| `vision_publish_ns` | service/mailbox publish | source availability timing |
| `controller_consume_ns` | controller consumes the source snapshot | controller join |
| `plan_decision_ns` | plan decision completes | decision stage |
| `final_output_ready_ns` | final output after aim/recoil/limits | output stage |
| `vigem_submit_complete_ns` / `delivered_at_ns` | successful ViGEm submission | actuator history |

For a calibrated present interval, retain both endpoint calibration ids and
uncertainties. `present_clock_valid` requires both endpoints to map successfully,
the ids to be nonzero, the interval to be monotonic and the clock-domain contract
to be the same. Raw QPC must remain available even when steady calibration is
unavailable. Report `present_to_result_ns` independently from any proposed
physical response delay.

### Separate observation, decision and delivery keys

An observation is not a controller tick. One fresh source observation may be
consumed by many controller decisions before the next Vision result arrives.
Keep these keys separate:

- `observation_key = source_frame_id + source_observation_id + present_endpoint`
  plus persistent target id, selector generation, physical ADS epoch and target
  acquisition id;
- `decision_key = controller_tick_id + controller_consume_ns + plan_decision_ns`
  plus `final_output_ready_ns`;
- `delivery_key = delivery_sequence + controller_tick_id + delivered_at_ns +`
  physical backend epoch.

The relationship is `one observation -> zero or more decisions -> zero or more
successful deliveries`. A repeated decision references the same observation key
and cannot create a second capture interval. The source observation, decision and
actuator history retain their native timestamp domains. A missing field produces
an enum reason such as `missing_present_clock`, `stale_source`,
`duplicate_source`, `backend_epoch_unknown` or `identity_unavailable`; it never
falls back to nearest-time matching.

### Fresh-frame admission and duplicate policy

1. Accept one source row only when its frame/present pair is strictly newer than
   the last accepted pair for that source stream.
2. Duplicate frame id, duplicate present timestamp, backward timestamp and stale
   latest-only replay increment a bounded reject counter and cannot create an
   interval or score.
3. A new frame id with the same present timestamp is not a new physical interval;
   it is retained only as a diagnostic `same_present_endpoint` rejection.
4. A selector generation change is an identity boundary. The new source may be
   scored for its own cohort, but the previous compatible realized pair is not
   silently reused. This changes the observation key; it does not manufacture a
   new decision key for replayed controller ticks.
5. Missing or accumulated frames are counted. They are not expanded into
   invented intermediate samples.

### Delivery-to-observation accounting

Use the accepted half-open interval convention:

- `realized`: `[previous_present - delay, current_present - delay)`;
- `in_flight`: `[current_present - delay, current_present)`;
- `scheduled`: `[current_present, decision_ns)`;
- `pending_total = in_flight + scheduled`.

Only successful deliveries whose timestamps fall in the relevant physical
backend epoch are eligible. A delivery at the left endpoint belongs to that
interval; a delivery at the right endpoint belongs to the next interval. The
current decision's newly delivered output cannot be included in the output it
just helped create; it becomes eligible at the next decision. Repeated decisions
on one source capture must refresh scheduled/pending from successful delivery
history without advancing previous/current capture or double-counting realized
work.

No calibrated present mapping means no applied-time interval score. The plan may
still report stage delay and delivery counts, but it must say
`INSUFFICIENT_EVIDENCE` for a physical response conclusion. A valid
`observed_effect` pair is the effect endpoint for scoring; `result_ready_ns` or a
controller decision timestamp cannot substitute for it.

## 5. Eligible cohorts and confound rejection

Every row is assigned one eligibility outcome and one stable reason. A row that
fails a clean cohort is useful for rejection counters but cannot contribute to a
response or admission score. Eligibility is based on effect observations and
noise/SNR, not on a fixed number of five-second windows.

### Neutral baseline and noise-floor cohort

Before scoring a command segment, collect same-identity neutral observations
under the same manual profile, viewport, FOV, ADS state and approximate target
size. Final right stick must be neutral and left stick, firing and recoil must be
inactive. The baseline estimates the distribution of target-anchor displacement
from detector jitter, target drift and cadence variation using median, MAD/P95
and per-axis values. The baseline is a required input to `observed_effect`; if it
is missing, too short or not profile-compatible, the command segment is
`INSUFFICIENT_EVIDENCE`, not a zero-response sample.

The baseline itself does not require the anchor to remain motionless. Its
identity, box scale/aspect and available pose attributes must remain compatible;
the anchor delta is precisely the quantity whose noise distribution is being
measured.

### Clean actuator-calibration cohort

Required conditions (thresholds marked provisional until real data):

- exactly one nominally stationary bot/target is visible; stationary validation
  uses pose/box-scale/aspect/identity stability and the neutral baseline, not a
  requirement that the target anchor stay fixed while the camera moves;
- selector acceptance/provenance is in an existing strong/accepted tier. Do not
  gate the first capture at `0.90`: observed strong sessions may legitimately
  span approximately `0.47-0.82`. The provisional numeric gate is `0.60` only
  after enrollment confirms it retains a nonzero stable cohort; otherwise use
  the existing accepted tier and report the measured confidence distribution.
  `confidence_threshold_mode` and the rows excluded by it are mandatory output;
- persistent identity and selector generation are stable for the complete window;
- one physical ADS epoch and one viewport/frame-size contract;
- a complete supplied manual game-profile identity, or an explicit
  `profile_identity_incomplete` exclusion;
- no duplicate, stale, backward or accumulated-frame gap in the scored window;
- no left-stick input, jump/slide, firing, recoil, weapon animation or output
  failure/disable;
- box scale/aspect and available pose attributes are within the provisional
  compatibility bounds; unexplained drift above the neutral baseline is
  `target_motion_contamination`;
- response metadata is valid, or the row is retained only in the explicit
  `response_model_unavailable` bucket.

Manual-only and AI-only are separate cohorts. Manual-only requires shaped AI to
be neutral/materially absent; AI-only requires physical manual right-stick input
to be neutral/materially absent. They are not added together and do not imply
that either source owns the final output in production.

### Response and admission cohorts

The 260 ms and 400 ms cohorts are separate weapon/optic/profile cohorts. Each
one starts with LT fully released, a neutral interval and a fresh physical
release/re-ADS rising edge. A two-second held-ADS warm-up is not an ADS ramp and
cannot be used to label either cohort. The slowdown cohort records target error
radius and response state on both sides of the boundary. The admission cohort
uses a steady held-X control plus naturally occurring onset/reversal variants;
the deterministic seam, not the operator, supplies exact pulses and phase
positions. Each live cohort stores actual present/delivery intervals and any
hypothesized poll phase without selecting a poll model prematurely.

Micro/deadzone holds must be long enough to exceed the neutral anchor jitter:
each scored segment requires at least 8 valid effect observations, at least five
median present intervals (or 100 ms, whichever is longer), and at least three
independent repeats before a model comparison is allowed. These are provisional
collection requirements, not COD gameplay thresholds.

### Exogenous cohorts

These are deliberately separate and labelled `NON_GOAL` for actuator
calibration:

- right-stick/final actuator neutral with left-stick strafe moving a stationary
  target;
- right-stick/final actuator neutral during firing/recoil;
- right-stick/final actuator neutral while near-target rotation/assist-like
  motion moves the target.

They prove that non-actuator screen motion exists; they must not be used to tune
the ledger or falsely inflate actuator residuals.

### Rejection reasons

At minimum: `no_single_target`, `low_confidence`, `identity_changed`,
`target_motion_contamination`, `left_stick_active`, `firing_or_recoil`,
`ads_epoch_changed`, `viewport_changed`, `capture_gap`, `duplicate_or_stale`,
`present_clock_invalid`, `delivery_failed`, `backend_epoch_unknown`,
`response_state_invalid`, `manual_ai_ambiguous`, `profile_identity_incomplete`,
`neutral_baseline_missing`, `effect_below_noise_floor`, `observed_effect_invalid`,
`insufficient_effect_observations`, `insufficient_window`.

## 6. Bounded implementation shape and budgets

The eventual implementation must be O(1) per controller tick and allocation-free
on the controller/Vision hot paths:

- fixed arrays of aggregate slots keyed by a bounded cohort/model id;
- integer counters, sums, sum-of-squares, bounded maxima and fixed histogram
  buckets only;
- one preallocated anomaly ring of 256 entries, each at most 256 bytes;
- no JSON construction, file I/O, console output, mutex wait or dynamic
  allocation on a 1 kHz path;
- the existing W5 history remains the source of successful final-output samples;
  this plan does not create a second delivery history.

Proposed budgets, to be measured rather than assumed:

| Resource | Provisional budget |
| --- | --- |
| Added controller hot-path cost | <= 0.5 us P95 and <= 1.0 us P99 per tick in Release |
| Persistent shadow state | <= 128 KiB, including the anomaly ring |
| Aggregate writer cadence | one bounded flush every 5 seconds |
| Normal persisted record | <= 32 KiB per five-second window |
| Anomaly ring behavior | overwrite oldest with a dropped-count; never allocate or block |

A bounded background writer may serialize one compact aggregate record per
five-second window. The normal stream must not become a per-frame JSON log. A
writer failure affects only diagnostics and must not affect control, Vision
latest-only delivery or ViGEm submission.

## 7. Shadow candidate models and scoring

The score engine consumes the final delivered vector and the joined observation;
it never consumes the output of another candidate as truth and never writes back
to control.

### Admission candidates

- `continuous_zoh`: successful final output is held until the next successful
  report;
- `latest_only_180hz` and `latest_only_240hz`: only the latest report at a
  hypothesized poll contributes. These are hypotheses, not assumed COD truth;
- `observed_present_interval`: the empirical distribution of accepted
  source-present/effect intervals in this session, including cadence gaps and
  phase coverage. It describes what was actually observed and is the candidate
  against which fixed 180/240 hypotheses are compared;
- `other_or_unresolved`: retained when neither model explains the joined
  evidence or the effect endpoint is unavailable.

Report actual delivery/present interval, inferred phase bin, steady-state versus
onset/reversal label, sample count and a per-model residual. A pre-rolled held-X
control must be an explicit control row; otherwise an onset transient can make
every latest-only model appear wrong. Live data does not claim a full phase sweep
unless the observed intervals actually cover it; missing phase coverage is an
explicit `INSUFFICIENT_EVIDENCE` reason. Exact full-phase probes remain in the
deterministic test seam. If an exact command injector/calibration driver is ever
proposed, it is a separate owner-approved component and is not assumed by this
plan.

### Applied-time response candidates

Compare delivery-time response scale with any defensible effect-time state. Keep
260 ms and 400 ms ramp labels, near-target slowdown direction, delay and target
switch state. Score absolute residual, normalized residual only when truth
magnitude is nontrivial, component sign agreement and vector cosine. A matched
constant-response control should be near zero in synthetic validation; a real
COD threshold is not assigned by this document.

### Deadzone/axis candidates

Score radial deadzone hypotheses `0.00`, `0.03`, `0.05`, scalar gain and
asymmetric X/Y gain. Preserve X-only, Y-only, diagonal, saturation and reversal
labels. Do not install a deadzone or axis plugin from shadow ranking alone.

Each candidate result contains `valid`, `sample_count`, `invalid_count`,
`mean_abs_residual_px`, `p50_abs_residual_px`, `p95_abs_residual_px`,
`max_abs_residual_px`, `normalized_valid`, `normalized_count`,
`sign_agreement`, `cosine_agreement`, `score_margin` and `reason`. Near-zero
truth uses `null`/invalid normalized values and absolute residual instead of an
epsilon quotient.

## 8. Compact output contract

The normal persisted record is one compact aggregate per five-second window. It
must not contain per-frame arrays, per-tick arrays, source-frame timestamp lists
or final-vector lists. Exact frame/tick/vector data is reserved for the bounded
anomaly ring in this section. This split is a hard requirement so Gate 2.5 does
not recreate the high-volume detailed JSONL that it is intended to replace.

### Normal five-second aggregate

The normal record contains:

#### Session and last/min/max provenance

`schema_version`, `record_schema`, `session_id`, `window_index`,
`window_start_ns`, `window_end_ns`, `executable_sha256`, `source_commit`,
`config_hash`, `engine_hash`, `telemetry_enabled`, `active_capture_fps`,
`controller_tick_hz`, `telemetry_hz`, `w3_enabled`, `w4_enabled`,
`effect_formula_version`, `camera_motion_sign_convention`,
`effect_coordinate_scope`, `min_effect_snr`,
`profile_identity_complete`, `profile_id`, `sensitivity`, `response_curve`,
`ads_multiplier`, `fov`, `resolution`, `viewport`, `weapon`, `optic`,
`nominal_ads_time_ms`, `last_source_frame_id`, `last_source_observation_id`,
`last_controller_tick_id`, `last_delivery_sequence`,
`last_present_calibration_id`, `last_reason`, plus min/max stage-delay and
present-interval summaries. These are scalar provenance values, not arrays.

#### Counters and coverage

`source_frames_seen`, `source_frames_accepted`, `duplicate_frames`,
`stale_frames`, `backward_frames`, `same_present_endpoint`,
`accumulated_frames_gt_one`, `observation_decision_fanout`, `join_success`,
`join_missing_stage`, `delivery_success`, `delivery_failure`,
`output_disabled_unknown`, `backend_epoch_changes`, `viewport_changes`,
`capture_gaps`, `neutral_baseline_pairs`, `observed_effect_pairs`,
`observed_effect_valid`, `observed_effect_invalid`, `effect_below_noise_floor`,
`anomaly_ring_drops`, `cohort_valid`, `cohort_rejected` and a bounded reason
counter table.

#### Cohort/model aggregates

For each bounded `cohort_id + profile_id + mode + curve + axis_bin + command_bin`
slot, store only counts, sums, sum-of-squares, min/max, fixed histograms and
model scores:

`sample_count`, `valid_count`, `invalid_count`, `effect_snr_count`,
`effect_snr_mean/p50/p95/min/max`, `noise_floor_px_mean/p95`,
`observed_effect_abs_mean/p50/p95/max`, `observed_effect_sign_agreement`,
`observed_effect_cosine_agreement`, `response_confidence_mean/min/max`,
`present_interval_histogram`, `delivery_interval_histogram`,
`poll_phase_histogram`, `realized/in_flight/scheduled/pending_total` validity
and magnitude summaries, and the Section 7 candidate score fields. Include
`model_sample_count`, `model_invalid_count`, `model_margin` and
`model_decision={preferred,undecided,insufficient}`. Normal records contain no
per-sample `observed_effect_px`, final stick or timestamp values.

`effect_observation_available` is an aggregate count/validity field only. Do not
call it `game_admitted_observation`: an anchor displacement is an observed
effect candidate, not a direct game-poll label.

### Bounded anomaly ring: exact evidence only here

The preallocated ring contains at most 256 fixed-size entries. An entry may
include `kind`, `reason`, the separate `observation_key`, `decision_key` and
`delivery_key`, `frame_id`, `source_observation_id`, `persistent_target_id`,
`selector_generation`, `physical_ads_epoch`, `target_acquisition_id`,
`controller_tick_id`, `delivery_sequence`, `previous_present_qpc`,
`current_present_qpc`, `qpc_frequency`, both present steady endpoints and
calibration ids/uncertainties when valid, `captured_at_ns`, `result_ready_ns`,
`vision_publish_ns`, `controller_consume_ns`, `plan_decision_ns`,
`final_output_ready_ns`, `vigem_submit_complete_ns`, `delivered_at_ns`,
`present_to_result_ns`, `final_right_x/y`, `delta_target_screen_x/y`,
`observed_camera_work_screen_x/y`, `noise_floor_px`, `effect_snr`, response/profile fields
and the invalid/rejection reason. These exact values are for transitions,
rejections and model anomalies only; ordinary rows never enter the ring merely
because a controller tick occurred.

Typical ring kinds are `duplicate_source`, `stale_source`, `clock_invalid`,
`identity_change`, `capture_gap`, `backend_disconnect`,
`target_motion_contamination`, `effect_below_noise_floor`,
`profile_identity_incomplete`, `model_divergence` and
`output_invariance_failure`. Ring overflow overwrites the oldest entry and
increments `anomaly_ring_drops`.

## 9. Deterministic tests before any live collection

The test seam must use the same bounded aggregate/reason code as the eventual
shadow path, but synthetic truth remains test truth rather than COD truth.

1. **Source ordering:** duplicate frame, duplicate timestamp, new frame with
   same timestamp, stale/backward frame and a fresh recovery frame. Assert one
   accepted observation interval, no duplicate realized work and explicit reject
   reasons.
2. **Observation fan-out:** consume one source observation at many controller
   ticks, then publish one new observation. Assert repeated decisions share the
   observation key, have distinct decision keys, refresh pending once per actual
   delivery and do not advance capture state.
3. **Admission phase:** deterministic tests use unique phases across a full 180
   Hz and 240 Hz period; pre-rolled held-X must agree under continuous/latest-only
   controls, while onset, between-poll pulse and 1 kHz-grid reversal remain
   separate. A passive live path bins actual final-output values and does not
   pretend to reproduce this exact phase injection.
4. **Observed effect/noise/sign:** generate known target-anchor displacement plus
   detector jitter, build a neutral baseline, verify full camera-work negation
   on +X, +Y and -Y, require the SNR margin, and reject below-floor or
   missing-baseline effects.
   The deterministic sign fixture must assert, with the same positive magnitude
   `K` and zero neutral baseline:

   ```text
   camera_work=(+K, 0) -> target_delta=(-K, 0) -> observed_camera_work=(+K, 0)
   camera_work=(0, +K) -> target_delta=(0, -K) -> observed_camera_work=(0, +K)
   camera_work=(0, -K) -> target_delta=(0, +K) -> observed_camera_work=(0, -K)
   ```

   The resulting vectors must compare directly to the ledger's `realized_px`
   on both axes; a one-axis Y flip or raw target displacement must fail.
5. **ADS response:** separate 260/400 ms weapon/optic cohorts with a release,
   neutral interval and fresh re-ADS edge; test fast-to-slow and slow-to-fast
   near-target crossings, matched constant-response control and target-switch
   attribution.
6. **Deadzone/axis:** 0/3/5 percent hypotheses, exact deterministic 1/2/3/5/10
   percent commands, X/Y/diagonal, saturation, reversal, symmetric control and
   asymmetric X/Y negative control. The live path must use actual-output bins.
7. **Target contamination:** moving target, low confidence, identity change,
   selector generation change, left strafe, firing/recoil and near-target
   rotation must be rejected from clean actuator cohorts and appear in the
   corresponding reason bucket.
8. **Lifecycle/delivery:** successful neutral, failed delivery, output disabled,
   backend epoch change, capture gap and invalid present calibration. A same-epoch
   failure must not silently recover physical history.
9. **Output invariance:** identical controller input/timing with shadow disabled
   versus shadow interleaved must produce bitwise-identical complete
   `GamepadOutputState`/ViGEm command sequences.
10. **Budget:** 1,000 controller ticks produce zero normal per-tick persisted
    records, bounded aggregate updates and no allocation/file-I/O call on the hot
    path; a five-second flush emits one bounded compact record.

## 10. Provisional gates and stop conditions

These are engineering guardrails, not COD acceptance thresholds. They remain
provisional until a real collection establishes the distribution:

- no output difference in the shadow-on/off A/B;
- added hot-path cost <= 0.5 us P95 / <= 1.0 us P99 in Release;
- bounded state <= 128 KiB and normal output <= 32 KiB per five seconds;
- synthetic known-contract controls preserve their expected result class;
- at least 99.9% of eligible synthetic joins are unique, ordered and fully
  staged; rejected rows are never scored;
- each scored segment has at least 8 valid effect observations, spans at least
  five median present intervals or 100 ms, and has at least three independent
  repeats; a candidate with fewer observations is `INSUFFICIENT_EVIDENCE`;
- a model may be called preferred only after at least 24 valid effect observations
  across at least three segments and a provisional 20% score margin (or a
  documented statistical margin); a count of five-second windows alone is not a
  sample-size gate;
- the provisional effect gate is `effect_snr >= 3` plus a reported absolute
  noise margin. This is a proposed collection guardrail, not a COD gameplay
  threshold, and can only be revised after the neutral baseline distribution is
  visible;
- normalized residual is valid only for nontrivial truth magnitude;
- source-present calibration is monotonic and valid at both endpoints.

Stop the capture and return `INSUFFICIENT_EVIDENCE` if any output changes, the
hot path allocates/blocks/writes, identity or timestamps cannot be joined without
guessing, the valid cohort is too small, the backend epoch is unknown, source
cadence is dominated by latest-only gaps, or exogenous motion contaminates the
actuator cohort. A model mismatch is a reportable `MODEL RED`, not a reason to
lower the gate or alter controller gains.

## 11. Ranked gameplay collection script

The user-facing script is passive and must not require a human to synthesize
exact 1/2/3 percent values, a pulse between polls or a 1 kHz-aligned transition.
The operator records the actual supplied game-profile identity and simple
scenario markers; the analyzer bins the actual final delivered vector and its
real source-present/delivery intervals. Exact command injection and full phase
probes belong to deterministic tests. If an exact calibration driver is later
desired, it requires a separate owner-approved component and is not part of this
plan. Each run starts with telemetry disabled, then enables only this bounded
shadow capture through the reviewed calibration entry point. The user should
not run a normal ranked match while collecting calibration data.

### Rank 1 - stationary-target response anchor

1. Enter a private practice lane with one stationary bot and no other visible
   targets, and record one weapon/optic profile: either the 260 ms or the 400 ms
   nominal ADS cohort.
2. Fully release LT, wait through a neutral interval, then press ADS once to
   create a real rising edge. Keep the target in view without left stick,
   firing, recoil or movement; a held-ADS warm-up is not a ramp.
3. Repeat the same procedure in a separate weapon/optic/profile segment for the
   other nominal ADS time. Never merge 260 ms and 400 ms into one ramp.
4. Repeat far-to-near and near-to-far aim-offset segments without changing the
   target or profile.

The operator does not time or shape an ADS ramp; the only live ramp anchor is the
observed release/re-ADS event and the analyzer uses the actual delivered and
present timestamps.

Invalidate on target switch/loss, selector-generation change, target movement,
left-stick input, firing/recoil, output failure, viewport change or a capture
gap in the scored window. This is the highest-value response cohort.

### Rank 2 - manual actuator sweep

With the same single stationary target and AI assist disabled or demonstrably
neutral, make approximate low, medium and high X/Y/diagonal/reversal inputs,
then saturation. The analyzer bins the actual final delivered values into the
1/2/3/5/10 percent ranges; the operator is not expected to hit those values
exactly. Hold each level long enough to cross at least two game-poll hypotheses
and to meet the effect-observation/SNR requirement. Release to a successful
neutral between levels. Do not move, fire or change targets. Any non-neutral
shaped AI, target motion or identity event invalidates that level, not just the
whole session.

### Rank 3 - AI-only response sweep

Keep physical right-stick neutral and repeat the same stationary-target offsets
with the normal AI proposal. Record ADS and BodyLock separately. This tests the
delivered final vector without attributing it to a raw manual force. Any manual
right-stick movement, target motion or lifecycle transition rejects the scored
window.

### Rank 4 - game-admission phase probes

Passively repeat a naturally established held-X segment, natural onset/release
and a natural +X to -X reversal while the analyzer records actual final-output
and present timing. "Pre-rolled" means the output has already become steady; it
is not a prescribed phase or exact-duration probe. Do not ask the operator to
create a pulse fully between polls
or align a transition to the 1 kHz controller grid; those are deterministic
tests only. Compare the observed interval/phase distribution with the 180/240 Hz
hypotheses and the empirical `observed_present_interval` candidate. Do not claim
the hypothesized poll rate is real unless an independent game/effect marker is
present.

### Rank 5 - axis/deadzone matrix

On a stationary target, repeat approximate X-only, Y-only and diagonal levels
with neutral anchors, saturation and immediate reversal. Keep each axis/level
as a separate marked segment; actual final-output bins, not requested hand
strength, define the score. A target that walks, a second visible target, a
frame-size change or an output/backend interruption invalidates only the affected
segment with an explicit reason.

### Rank 6 - exogenous separation controls

Run three separate controls with final right stick neutral: left-stick strafe,
firing/recoil, and near-target rotational/assist-like motion. Do not combine them
into one row. These are `NON_GOAL` observations proving screen motion can be
nonzero while actuator pending is zero; they must not calibrate W5.

### Collection handoff

For every segment the operator records the scenario marker, supplied manual
game-profile identity and whether the segment was intentionally invalidated. The
analyzer derives source/delivery joins from the native session. It must emit the
exact session/executable/config identity, profile fields, cohort counts,
rejection reasons, clock validity, observed-effect/noise/SNR summaries, model
scores and artifact hashes. If a segment has no stable target, no neutral
baseline, no valid present interval or no successful final delivery, it is
reported as missing evidence rather than filled with a nearest timestamp or a
synthetic pixel estimate.

## 12. Decision checkpoint after live shadow

Gate 2.5 may recommend exactly one next architecture question, not implement it:

- choose an admission model only if real effect evidence distinguishes
  continuous from latest-only/other;
- choose an applied-time response contract only if both present/effect timing and
  response-state provenance are valid across 260/400 ms and slowdown cohorts;
- choose a deadzone/axis plugin only if clean manual/AI actuator cohorts separate
  game response from target motion and show repeatable axis/diagonal evidence.

If none is distinguishable, retain `FAIL_FOR_ACTIVATION`, list the smallest
missing signal and stop. There is no automatic W5 promotion, no Remaining
replacement and no motion-primitive work in this checkpoint.
