# W5 Causal Short-Term Memory

**Decision date:** 2026-08-08  
**Current state:** Phase B + Gate 2 accepted as evidence; Gate 2.5B bounded
shadow-only package implemented in the isolated candidate and awaiting owner
review; `FAIL_FOR_ACTIVATION`; no actuation authority

## Why This Is the Mainline

W3 online background optical flow and W4 online response learning no longer gate
the controller mainline. W3 adds a second online Vision branch and its valid-rate
and cost are not justified at the current 180-200 Hz detector cadence. W4 depends
on W3-quality physical-effect observations and was consuming learner/history work
without a promotable response model.

Both tracks are frozen, not deleted:

- `runtime.vision.ego_motion_enabled = false` prevents W3 resource allocation and
  compute.
- `runtime.control_learning.enabled = false`, `mode = "disabled"` and
  `telemetry_enabled = false` stop the W4 learner in normal local runs.
- Their code remains available for explicit offline calibration experiments.

W5 solves a different and more immediate defect: each new Vision observation must
know which final commands were already delivered, which effects should already be
visible, and which work is still pending. A command must never be counted as
completed merely because it was submitted.

## Phase A Architecture

`CausalMotionLedger` records the exact final stick only after the ViGEm delivery
callback reports success. Each sample carries:

- delivery timestamp;
- final X/Y stick after manual/AI arbitration and recoil;
- response-curve-aware screen-camera velocity;
- persistent target identity;
- physical ADS epoch;
- output-enabled and delivery-success state.

At a new, monotonic Vision capture the ledger separates remembered camera work:

| Phase | Time ownership | Meaning |
| --- | --- | --- |
| `realized` | previous capture to current capture, shifted by response delay | Output whose physical effect should be visible in the new observation |
| `in-flight` | delivered before capture but still inside response delay | Output accepted by the game but not yet visible at capture time |
| `scheduled` | delivered after capture and before the current decision | Work absent from the captured image and still pending |
| `pending_total` | `in-flight + scheduled` | Work that a future final-target solver must not request twice |

The default shadow contract is a configurable 20 ms response delay and 200 ms
memory horizon. The **current implementation** resets on target replacement, ADS
epoch change, target loss, ADS release, failed delivery, disabled output,
invalid/non-monotonic timestamps or ring-history loss. Gate 1 proved that the
logical target/ADS resets are not an acceptable final physical-memory contract:
already delivered work continues through the virtual actuator after those
logical boundaries. This reset behavior is therefore a measured Phase A defect,
not the target architecture.

The configured response curve is part of history. Delivered COD Dynamic stick is
converted through the same forward plugin used by the virtual camera before it is
integrated. Historical linear-stick accounting is therefore not reused for a
nonlinear game curve.

## Current Safety Boundary

Phase A is observation-only:

- it does not modify `TargetPlan`, `Remaining`, ADS, BodyLock, the final fuser or
  ViGEm output;
- the legacy `PendingControlMotion`/Remaining path remains the production owner;
- a deterministic A/B integration fixture asserts bit-identical final output with
  W5 shadow off and on;
- unit fixtures cover phase separation, lifecycle resets, invalid delivery and
  COD Dynamic forward/inverse accounting;
- sustained AimLab now applies the configured forward response curve and reports
  W5 valid/realized/pending statistics per run.

A 1,000-tick COD Dynamic smoke run produced its first valid estimate at 33 ms,
967 valid pending estimates and 967 valid realized estimates. This proves the
shadow path is populated; it is not yet an accuracy or live-effect acceptance.

## Gate 1 Owner Review (2026-08-08)

Gate 1 is **FAIL**. The exact fixed-response estimator math is sound, but the
physical-history ownership, controller query timing and response-model contract
are not promotable. The complete reproducible evidence is in
[Gate 1 report](../../artifacts/benchmarks/w5-causal-memory-gate-20260808/GATE1_REPORT.md).

| Area | Result | Evidence / consequence |
| --- | --- | --- |
| Exact fixed plant | PASS | Linear/COD Dynamic, ADS/BodyLock and 5/20/45 ms delays pass 12/12 controls; pending mean residual is about `6.9e-6..4.22e-5 px` |
| Target/ADS lifecycle | P0 RED | Target switch, new ADS epoch and loss/reacquire erase `1 px` of independently retained delayed work in deterministic fixtures |
| Global manual carry | P0 RED | A real delayed-plant fixture retains `12.6268 px` of targetless/manual work after target acquisition, while the target-owned ledger records none |
| Same-capture decisions | P0 RED | The controller-exposed cached scheduled work stays `0`; recomputing after six successful deliveries gives `1.6 px` |
| State-dependent response | RED | Default aim slowdown raises pending mean/P95 error to `0.61142/1.42696 px`; fixed-response control is about `0.0000277 px` |
| Delay sensitivity | Gate RED | A 24 ms plant with a 20 ms ledger reaches `0.417429 px` mean and `0.154401` normalized mean, yet the declared gate still passes |
| Distributed response | RED | A ramped plant fails the normalized gate with mean `0.387394` |
| Time partition | PASS | Half-open `realized`, `in-flight`, `scheduled` and decision-endpoint ownership is exact |
| Game admission | UNRESOLVED | ViGEm delivery versus a game's latest-only polling behavior has no live game-poll evidence |

The owner independently reran the isolated Release CTest suite (`38/38 PASS`)
and `git diff --check`. Green test execution means the evidence harness is
deterministic; it does **not** override the Gate 1 FAIL verdict.

## Phase B Owner Acceptance (2026-08-08)

The Phase B bookkeeping redesign is **accepted for shadow use**.  This does not
promote W5 into the controller and does not overturn the response/admission REDs
from Gate 1.  The detailed evidence is in the
[Phase B report](../../artifacts/benchmarks/w5-causal-memory-phase-b-20260808/PHASE_B_REPORT.md).

The accepted contract now has one global actuator ledger, explicit physical
backend epochs, per-controller-decision pending refresh, logical-only capture
compatibility, explicit history coverage, and separate response-confidence
values/validity.  Targetless manual input, successful neutral, final post-recoil
output and old-owner work all remain in physical history.  A cold start or new
device epoch no longer invents a zero-valued prefix before the first known
delivered state.

Owner verification independently reproduced the three focused PASS executables,
`38/38` Release CTest in `1.98 s`, and a clean `git diff --check` apart from line
ending notices.  The protected user runtime remained byte-identical with SHA-256
`E25C433A7DDF64A04E1B9099C2F44F78C78AFC6CE422AC54654AF5CEDB19734A`.

Phase B closes the known target/ADS/loss reset, pre-acquisition carry,
same-capture cache, post-kill recoil-tail, backend recovery and cold-start
coverage defects.  Gate 2 now attacks what Phase B deliberately does not solve:
game-poll admission, applied-time response changes, ADS/slowdown transitions,
deadzone/axis mismatch, exogenous left-stick/recoil motion and capture-clock
robustness.

## Gate 2 Owner Acceptance (2026-08-08)

The Gate 2 evidence package is **accepted as evidence** but remains
`FAIL_FOR_ACTIVATION`.  It is not a production-control acceptance and it does
not authorize W5 to read from the controller path.  The owner independently
verified the focused matrix and the full `39/39` CTest run; the protected runtime
identity remained
`E25C433A7DDF64A04E1B9099C2F44F78C78AFC6CE422AC54654AF5CEDB19734A`.
The machine-readable summary, controller capture and report are preserved in
the [Gate 2 evidence directory](../../artifacts/benchmarks/w5-causal-memory-gate2-20260808/).

This is the prior Gate 2 checkpoint. Its `E25C...` protected-runtime identity and
`39/39` CTest result are historical and are not the current Gate2.5A identity.
The current Gate2.5A evidence uses protected runtime
`A77A856F170D2D74FE3222255C72DDC5E60E4A300979FACBA09A5A0FCE275939`, isolated
Release `40/40` CTest, and the implementation report in
`../../artifacts/benchmarks/w5-causal-memory-gate2_5a-20260808/`.

Gate 2 is intentionally stratified rather than reduced to one pass/fail score:

| Evidence class | Gate 2 status | Meaning |
| --- | --- | --- |
| Global bookkeeping, half-open intervals and capture robustness | PASS | Deterministic shadow bookkeeping contracts hold in the tested fixtures. |
| Applied-time response, slowdown, distributed response and scalar deadzone/axis model | MODEL RED | The current model is not sufficient to explain the independent synthetic plant in all controls. This is not a COD gameplay error measurement. |
| Continuous versus latest-only game admission | UNRESOLVED | Delivery history alone cannot prove how the game samples ViGEm state. |
| Left-stick, recoil and rotational/exogenous screen motion | NON-GOAL | These effects must remain separate from right-stick actuator memory; no optical-flow dependency is authorized here. |

All Gate 2 pixel magnitudes are deterministic synthetic-plant model-sensitivity
evidence. They are useful for exposing time-domain, response-state and axis
assumptions, but must not be quoted as measured COD gameplay error or used as a
promotion threshold. The default slowdown, 24/20 ms delay, distributed-response
and latest-only controls remain negative or unresolved controls; they were not
tuned away.

The next bounded step is the [Gate 2.5 low-overhead live-shadow measurement
plan](W5_GATE2_5_LIVE_SHADOW_MEASUREMENT_PLAN_20260808.md). It uses existing
Vision observations and successful final ViGEm delivery history, has no optical
flow dependency, writes compact five-second aggregates plus a bounded anomaly
ring, and cannot influence `TargetPlan`, Remaining or the final output. W3 and
W4 remain frozen/off, and motion-primitive calibration remains deferred.

### Defect-finding scenario set

These scenarios remain the minimum regression set for the memory redesign:

1. **Target A -> target B inside the response delay:** A delivers strong `+X`;
   B becomes current before the delayed effect appears and requests neutral or
   opposite output. Old A work must remain in global pending and influence B's
   final target-relative solution without inheriting A's target ownership.
2. **ADS release/re-press inside the delay:** test both a successfully delivered
   neutral and an ineligible/disabled transition. A neutral changes future held
   output; it does not retroactively delete work already in flight.
3. **Brief loss/cue hold and same-target reacquire (10-30 ms):** losing logical
   evidence must not erase physical work which is still being realized.
4. **Pre-acquisition manual carry:** deliver a strong manual final stick while no
   target exists, admit a target before the plant delay expires, then verify the
   new target solver knows that camera work is still pending.
5. **Repeated decisions on one Vision capture:** at controller rate, append
   successful deliveries after the first decision. `scheduled`/`pending_total`
   must update every decision while `realized` remains tied to capture pairs.
6. **Kill/despawn while recoil is active:** the post-recoil final output and any
   earlier AI work must survive target loss; an immediate new target must not
   receive duplicate horizontal work or a forgotten downward tail. This is the
   next fixture to add because it directly matches a prior post-kill pull case.
7. **Response-state transitions:** cross the aim-slowdown region during a held
   command, test 20/24 ms delay mismatch, and retain the distributed-response
   negative control. Low-confidence response estimates must not be promoted as
   accurate memory.
8. **Timing/curve boundaries:** cover 4/6/11/25/60 ms capture gaps, horizon
   overflow, explicit neutral, Linear/COD Dynamic deadzone, diagonal saturation
   and rapid reversal with exact X/Y sign checks.
9. **Backend and game-admission boundaries:** failed ViGEm delivery and disabled
   output remain an explicit unknown physical epoch unless a successful neutral
   is observed. Latest-only game polling remains a negative control until live
   evidence exists.

### Phase B defect-discovery order

After the structural Phase B fixtures pass, run the following adversarial cases
in this order.  The first group validates bookkeeping and must be deterministic;
the second group intentionally attacks the response assumptions and may remain a
documented negative control.  A model-mismatch case must not be made green by
weakening its independent plant truth.

| Priority | Adversarial scene | Defect signature | Layer under test |
| --- | --- | --- | --- |
| P0 | Start the ledger, or enter a new backend epoch, while a non-neutral stick is already held | The interval before the first known report is silently treated as zero | history coverage / epoch initialization |
| P0 | Kill or despawn the target while a post-recoil downward output and older horizontal AI output are still inside the delay; admit a new target immediately | downward or old-owner work disappears, or is charged twice to the new target | global actuator ownership |
| P0 | One source capture, six controller decisions and six successful deliveries | controller-facing pending stays cached while direct ledger truth grows | per-decision refresh |
| P0 | Target switch, ADS release/re-press and 10-30 ms loss/reacquire, each with and without a delivered neutral | logical lifecycle erases physical work, or neutral retroactively erases earlier work | capture compatibility versus physical history |
| P1 | Sweep delivery just before/after a 180 Hz game-poll boundary, including a `+X -> -X` reversal entirely inside one 5.6 ms frame | continuous-hold ledger and latest-only game truth disagree by one poll interval | ViGEm admission semantics; currently unresolved |
| P1 | Hold the same stick through hip-to-ADS animation, then cross into/out of near-target aim slowdown before the delayed effect is realized | response resolved at delivery time disagrees with response at physical-effect time | state-dependent response model |
| P1 | Apply 1-5% right-stick micro-input around the configured game deadzone, then diagonal and saturated inputs | ledger predicts phantom micro-motion or wrong diagonal magnitude | curve/deadzone/axis model |
| P1 | Keep right stick neutral while using strong left-stick strafe near one stationary target; repeat while firing | observed relative motion exists although right-stick pending is zero | exogenous player motion / rotational aim assist / recoil |
| P1 | Duplicate, stale and out-of-order source frames followed by a fresh frame; also test 4/6/11/25/60/150/210 ms capture gaps | capture state advances twice, pending is double-consumed, or invalid history silently recovers | clock, freshness and horizon contract |

The P1 cases are especially important before activation.  At 180 Hz, even a
single 5.6 ms admission-phase error can be several pixels at strong output, so a
high detector rate does not by itself prove that the actuator memory is accurate.
The first live-shadow pass should therefore report these cases separately rather
than averaging them into one mean error.

### Gate 2.5A implementation checkpoint (historical baseline, 2026-08-08)

Gate 2.5A is implemented as a default-off, shadow-only diagnostic path. It
uses the existing source-present target observations and successful final
ViGEm delivery history, keeps observation/decision/delivery keys separate, and
emits only bounded five-second aggregates plus a bounded anomaly ring. It does
not read back into TargetPlan, Remaining, the fuser, gains, recoil or final
output. W3/W4 remain frozen/off and motion primitives remain deferred.

The deterministic bookkeeping contracts are green: source ordering and fresh
recovery, one-observation fan-out, delayed present interval/sign handling,
neutral-baseline protection against signed cancellation, mode boundaries,
invalid geometry/exogenous rejection, calibration/clock-domain transitions,
72-cell sufficient statistics, Gate-only collector isolation, bounded
transport/shutdown accounting and the fully covered 1 kHz/180 Hz mixed hot path.
This is evidence-harness acceptance only, not activation acceptance.

The complete Gate-only footprint currently measures at least:

| Object | Bytes |
| --- | ---: |
| `Gate25LiveShadow` | 44,280 |
| reused `ControlHistory<1024>` | 393,240 |
| Gate-only ordinary queue (16 slots) | 41,216 |
| dedicated Gate transport queue (accessor) | 246,784 |
| measured lower-bound increment | **725,520** |

The `128 KiB` persistent-state budget is therefore **RED**. The shadow object
static size is not a complete Gate-only budget, and the transport accessor is
the authoritative queue allocation (the aggregate-struct-times-capacity
figure omits the full queue-entry variant/alignment). A later package may
evaluate reusing the controller `CausalMotionLedger` (512 final-output samples)
instead of constructing the Gate-only `ControlHistory<1024>`/full response
assembler, while retaining manual/AI decomposition only as diagnostics. It may
also separate the large aggregate queue from the small anomaly queue instead of
embedding both variants in every `Gate25QueueEntry`; neither optimization is
part of Gate 2.5A.

### Gate 2.5B bounded source-present/memory checkpoint (2026-08-08)

Gate2.5B is the bounded follow-up to the Gate2.5A RED baseline. It remains
default-off and shadow-only; it does not activate W5, read into `TargetPlan`,
Remaining, the fuser or final output, and it does not deploy or overwrite the
protected runtime. The isolated evidence package is in
[GATE25B_REPORT.md](../../artifacts/benchmarks/w5-causal-memory-gate2_5b-20260808/GATE25B_REPORT.md)
and
[GATE25B_SUMMARY.json](../../artifacts/benchmarks/w5-causal-memory-gate2_5b-20260808/GATE25B_SUMMARY.json).

The tracked Gate-only budget is now 61,080 B state/observer/delivery view +
41,216 B ordinary queue capacity + 18,768 B dedicated Gate transport =
121,064 B, below the 131,072 B hard target with a 10,008 B margin. This is an
explicit measured component sum and excludes allocator/process metadata; it
does not reuse the historical 725,520 B Gate2.5A number. Gate2.5A's 725,520 B
record remains preserved as the RED-before evidence.

The W5 source-present seam is independent from the legacy copy-complete
capture clock. `CausalMotionPhaseEstimate` carries previous/current endpoint
provenance, and Gate joins only against its own accepted prior. The focused
controller fixture holds source-present/delivery timing fixed while varying
copy-complete by 0/1/3/6 ms; estimates and shadow final outputs remain
identical, while a source-present +6 ms shift changes in-flight and scheduled
motion by 0.6 px on X. A separate controller + Gate observer A/B performs
four observer calls, forms two compatible pairs and one duplicate rejection,
while the complete `GamepadOutputState` sequence remains bitwise identical.
This is a real bounded seam, not a RuntimeLoop/FPS/ViGEm proof. The isolated
`cod_native_runtime.exe` was also linked in `build-w0-w2-review\Release` and
was not started or installed.

Gate2.5B is therefore a reviewer checkpoint, not an activation decision:
tracked bounded-state evidence is green, live COD evidence is absent, the
latest-only admission question and applied-time response model remain
unresolved/model-red, and W3/W4 plus motion-primitive calibration remain
frozen/deferred.

#### Next live/shadow probes (recorded, not implemented)

1. Pure AI, stationary single target, `manual=0`, zero error: detect a second
   correction or self-excited oscillation.
2. Strong AI/manual followed immediately by a 1-3% same/opposite micro-trim:
   check whether pending overestimate suppresses scalp-position control.
3. Cadence changes `1 kHz -> 200 Hz -> 1 kHz` and
   `180 Hz -> 40 Hz -> 180 Hz`: score by timestamps, not sample count.

These are live/shadow probes only and are not Gate2.5B pass evidence.

The full owner-readable evidence, including isolated executable identities,
perf path mix, RED-before state and the discriminator matrix, is in
[GATE25A_REPORT.md](../../artifacts/benchmarks/w5-causal-memory-gate2_5a-20260808/GATE25A_REPORT.md)
and [GATE25A_SUMMARY.json](../../artifacts/benchmarks/w5-causal-memory-gate2_5a-20260808/GATE25A_SUMMARY.json).

#### Required discriminator matrix (not yet modeled as live truth)

Before any activation, each row below must have a trigger assertion, a
quantitative symptom oracle, a counterfactual and explicit covariates. The
most informative family holds the current observation and current final command
constant while changing only the previous 20-200 ms history; different effects
would prove a latent game-response state that an integral-only ledger cannot
represent.

| Class | Required scenes | Current interpretation |
| --- | --- | --- |
| P0 bookkeeping, zero-error contract | one observation/many controller consumes; target A -> B, loss/reacquire and kill inside 20 ms; ADS release/repress with successful neutral and without; backend fail/recover; duplicate/stale/backward recovery; source-present vs 0/1/3/6 ms copy-complete jitter; ADS/BodyLock mode boundary; 10/30/60/150/210 ms blind gaps; ring wrap/horizon | Must pass with no double count or false zero history. Source-present/copy-complete remains a W5 clock seam until W5 consumes a calibrated source-present field. |
| P1 response-model controls | neutral -> step, preheld steady and opposite -> step; steady 10%, half-duty 20% and +X/-X cancellation with equal signed integral; identical final time series with different manual/AI decomposition; max -> neutral vs max -> opposite; 5.6 ms reversal phase; fixed 20/24 ms vs distributed/ramped response; ADS 260/400 and BodyLock near/far; 1/2/3/5/10% X/Y/diagonal/saturation | Keep as `MODEL RED`/`INSUFFICIENT_EVIDENCE` until real game admission, applied-time response, profile and deadzone/axis provenance exist. Manual/AI components never become physical forces. |
| Contamination/non-goal | slow target drift, partial occlusion, box resize, FOV/zoom, multi-target identity overlap, left strafe, fire, recoil and rotational assist-like motion | Reject/count separately; do not fit right-stick actuator response or use optical flow in this phase. |

All pixel values from known-plant Gate 1/2 fixtures are synthetic model
sensitivity evidence, not measured COD gameplay error. Latest-only game
admission remains unresolved.

### Required architecture revision

The next shadow-only package should separate physical history from logical target
history:

- one **global actuator ledger** records every successfully delivered final
  right-stick output, including manual-only, recoil and successful neutral
  samples; target id and ADS epoch remain attribution metadata and cannot erase
  physical samples;
- only a true actuator/backend epoch break, invalid clock/history, or explicitly
  unknowable delivery state invalidates global history;
- target/capture reconciliation remains logical: `realized` can be compared only
  across compatible observations, while global pending camera work survives a
  target or ADS transition and is applied to the new target-relative solve;
- capture arrival updates the realized interval, but `in-flight`, `scheduled` and
  `pending_total` are queried at every controller decision using all successful
  deliveries since the current capture;
- response-model confidence and mismatch reasons are first-class validity data.
  The 24/20 ms sensitivity gap must be resolved before any output authority.

## Activation Gates

Do not let W5 affect output until all of the following pass:

1. In simulation, predicted `realized` displacement agrees with the known plant
   displacement for linear and COD Dynamic curves, multiple fixed delays, ADS and
   BodyLock, stop/reverse and target-switch fixtures. **Bookkeeping result:
   Phase B PASS. Overall result remains FAIL while state-dependent response,
   delay/distributed response and game-admission controls are unresolved.**
2. Shadow telemetry reports sign agreement, cosine agreement, mean/P95 residual
   error and invalid/reset reasons. Validity must not be inferred from a nonzero
   estimate alone.
3. A current live capture shows no stale-target inheritance and no increase in
   controller latency or output discontinuities.
4. W5 first replaces the old Remaining accounting behind an A/B switch. It must
   not become a second additive controller.
5. Only after the old owner is retired may `pending_total` influence the single
   final target-relative solver. `T = M + AI` remains a diagnostic decomposition;
   the product objective is the correct final `targetX/Y`, including suppression
   or cancellation of wrong manual input when policy permits.

## Deferred Research: Motion-Primitive Calibration

**Priority state:** explicitly deferred until W5 causal memory passes its
known-plant, live-shadow and legacy-Remaining replacement gates. This research
must not consume the current implementation or validation budget.

The simulator already owns continuous, smooth curve construction. It does not need
another game, a recorded player route or raw noisy tracks to prescribe every point
of a trajectory. It needs COD-specific distributions for a small set of
short-horizon **motion primitives** that constrain those generated curves.

The calibration contract is:

| Primitive | Parameters to measure | Simulator use |
| --- | --- | --- |
| Strafe start/stop | normalized top speed, acceleration time, release/braking time | set speed and time-constant ranges |
| Direction reversal | approach speed, time to zero crossing, opposite-direction acceleration, speed reached after 50/100/150/200 ms | generate credible stop/reverse demand inside the W5 horizon |
| Jump/fall | action onset, normalized peak height, rise time, apex duration, fall acceleration/speed, landing recovery | generate separate rise and fall phases rather than one symmetric arc |
| Slide | onset delay, downward velocity, normalized depth, hold duration, recovery velocity | generate the dive, low phase and stand-up phases |
| Compound motion | retained horizontal-speed ratio and timing correlation during jump/slide | combine primitives without inventing impossible motion |

Screen displacement and velocity should be normalized by a stable, unoccluded
standing target-box height. Samples with truncated boxes, identity replacement,
large scale changes, ambiguous multiple targets or non-monotonic capture time must
not train the distribution. Near/mid/far distance tier and ADS/BodyLock state remain
explicit conditions rather than being averaged together.

The current simulator is already close to this representation:
`PlayerStrafeScript` has top speed and a response time constant, while
`PlayerVerticalMotionScript` has slide drop/hold/recovery and jump height/duration.
Its current jump curve is a symmetric sine and its parameter ranges are synthetic.
The first model refinement after calibration is therefore separate rise/apex/fall
parameters, not importing recorded paths or adding generic smoothing.

### 1. Controlled 180-200 Hz in-game calibration (primary)

Use the existing detector and controller clocks to write a compact calibration row:
capture timestamp, persistent target identity, target anchor and unoccluded box,
confidence, candidate count, raw left/right sticks, final delivered right stick,
ADS state, distance tier and an optional manually marked action label. Detailed
runtime telemetry and online optical flow are not required.

Collect two isolated classes:

- **Target primitives:** keep the local player and right stick nearly still, retain
  one strongly observed bot, then mark strafe/reverse/jump/slide intervals.
- **Player-induced relative motion:** keep a stationary target visible and issue
  controlled left-stick/jump/slide inputs. The inverse target-anchor displacement
  measures the screen motion introduced by the local player.

Align repeated samples at the detected/action onset and fit robust parameter
summaries such as median and P10/P90. Twenty or more clean repetitions per action,
distance tier and movement mode are more useful than hours of unlabeled play. The
result is a compact versioned motion-primitives file consumed only by the simulator.

This is the only source with both the temporal resolution and the current COD,
camera, detector and target-anchor domain needed by a 150-200 ms memory horizon.

### 2. CrossFPS COD clips (coarse secondary check)

CrossFPS provides 5-second 20 fps clips with frame-aligned controller axes for
several COD titles. Its roughly 50 ms sampling can sanity-check a coarse jump/fall
envelope, but is marginal for reversal timing and does not provide the enemy-action
labels needed for reliable slide calibration. It must not set the primary BO7
parameter values or become a runtime dependency.

Source: <https://huggingface.co/datasets/zizhaotong/CrossFPS-train>

### 3. Synthetic engines (implementation validation only)

ViZDoom exposes object identity, world position/velocity, screen boxes, camera pose
and actions. FirstPersonScience can configure target motion, frame rate and latency.
Use them to prove that the extractor and W5 scorer recover known parameters; do not
use their movement constants as COD truth.

Sources: <https://github.com/Farama-Foundation/ViZDoom> and
<https://github.com/NVlabs/abstract-fps>

Activision Caldera breadcrumbs are intentionally excluded from primitive
calibration because their roughly two-second spacing is orders of magnitude too
sparse. Counter-Strike movement traces and old IW-engine mod values may provide
qualitative test ideas, but their physics and camera contracts are not BO7 data.

## Immediate Work Order

1. Keep W3/W4 disabled, preserve Gate 1 and Phase B evidence, and keep W5
   `FAIL_FOR_ACTIVATION`.
2. Preserve the Gate 2 result classes: bookkeeping PASS, model RED, unresolved
   admission and exogenous-motion non-goal. Do not alter production semantics to
   make a synthetic negative control green.
3. Review the Gate2.5B bounded memory/source-present/output-invariance package,
   then use the bounded Gate 2.5 live-shadow measurement plan. Keep latest-only
   game admission unknown until real COD evidence is collected.
4. Use only eligible, joined live cohorts to choose an admission model,
   applied-time response contract or deadzone/axis plugin. Candidate scoring must
   stay shadow-only and must not become a second controller owner.
5. Only after live-shadow evidence is accepted may legacy Remaining receive one
   exclusive A/B review. Deferred motion-primitive work remains paused until
   that boundary is complete.
