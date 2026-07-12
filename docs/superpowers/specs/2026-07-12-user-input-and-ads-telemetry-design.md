# User Input and ADS Transition Telemetry Design

## Goal

Extend the opt-in native runtime telemetry pipeline so it can collect enough
structured evidence for two future offline uses:

1. derive a user control profile from manual input habits and controller
   interaction; and
2. calibrate the two-dimensional target displacement from stable hipfire into
   settled ADS so a future tracker can model the transition.

This phase only collects evidence. It does not generate a user profile, change
configuration, tune the controller, or feed learned values back into runtime
control.

The collected evidence must support three explicitly different readiness
levels:

- `diagnostic`: sufficient to reconstruct what the runtime observed and did;
- `profile_eligible`: sufficient to describe a user's input habits without
  claiming a causal controller adjustment; and
- `model_eligible`: time-aligned, identity-safe, complete evidence suitable for
  future offline controller-response or ADS-transition model fitting.

A record being useful for diagnostics does not make it eligible for a profile
or model. Readiness is assigned by explicit quality gates, never inferred from
the mere presence of fields.

## Scope and Safety Boundary

- The default remains `runtime.telemetry.enabled = false`.
- Disabled telemetry creates no files, starts no writer thread, allocates no
  telemetry queue, creates no session identifier, encodes no record, and
  maintains no user-input or ADS event state.
- Enabled telemetry is observational. It must not change target selection,
  tracker state, ADS/bodylock authority, recoil, auto-fire, or controller
  output.
- No screenshots, video, usernames, machine names, window titles, or biometric
  identity features are recorded.
- A locally generated random session identifier and process-local target track
  identifiers are sufficient. Target identifiers do not persist across runs.
- Raw logs remain local and follow the existing bounded rotation policy.

## Architecture

Use the existing asynchronous `RuntimeTelemetry` writer and bounded,
non-blocking queue. Add three observational producers upstream of it:

```text
Physical input + controller components
  -> UserInputEpisodeCollector -----> controller_sample / input_event

Vision selector + tracker association
  -> TelemetryTargetIdentity -------> target_event / target_track_id

ADS state + identified target + new vision frames
  -> AdsVisualTransitionEstimator --> visual ADS progress / settle evidence
  -> AdsTransitionCollector --------> ads_transition_sample / ads_transition

Vision frame N + controller command window + vision frame N+1
  -> ControlResponseWindowAssembler -> control_response_window

Record sequences + quality evidence
  -> TelemetryCompletenessGate -----> readiness / invalid reason

All fixed-size records
  -> existing bounded telemetry queue
  -> background serialization and rotation
```

Collectors exist only when telemetry is enabled. The controller thread may
populate fixed-size records and attempt a non-blocking enqueue. Target
association summaries, visual transition estimation, response-window pairing,
event aggregation, quality gating, and JSON serialization must not block the
1 ms controller loop. Work that is not a constant-time field copy or bounded
state update runs on the telemetry consumer side.

## Record Envelope

Every record contains:

- `schema_version`;
- `record_type`;
- `session_id`;
- monotonic `timestamp_ns`;
- `tick_id` and latest `frame_id` when available;
- `ads_event_id`, `input_episode_id`, and `target_track_id` when applicable;
- `config_hash`, `build_commit`, and `engine_hash` through a session metadata
  record rather than repeating strings on every sample.

Record types are:

- `session_metadata`;
- `controller_sample`;
- `input_event`;
- `target_event`;
- `ads_transition_sample`;
- `ads_transition`;
- `control_response_window`.

Every sampled stream also has a monotonic `sample_seq`. Aggregate records
contain the relevant first and last sequence, expected count, written count,
dropped count, and `complete` flag so offline readers can reject events with
hidden gaps.

Unknown fields must be ignored by offline readers. A schema-version change is
required before removing or changing the meaning of an existing field.

## Session Metadata

Write one `session_metadata` record after the writer successfully opens its
first file. It contains:

- random local `session_id`;
- executable build commit and runtime binary hash;
- effective configuration hash;
- model/engine hash;
- capture resolution, controller coordinate resolution, and vision ROI;
- active and idle capture rates;
- controller and telemetry sampling rates;
- tracker backend;
- effective input deadzone/response configuration;
- in-game sensitivity, aim-response curve, optic/zoom identity, and base FOV
  when available, otherwise `unknown` with a source-quality label;
- recognized weapon/profile identifier when available, otherwise `unknown`;
- monotonic session start timestamp.

The metadata record must not contain personally identifying operating-system
or account data.

## Runtime Target Identity

`frame_id` does not prove that two samples refer to the same target. Introduce
an observational `TelemetryTargetIdentity` that consumes the selector/tracker
association result and emits a process-local `target_track_id`.

The identifier is retained while the selected candidate remains associated by
the production selector/tracker evidence. A new identifier is issued when:

- the selected target explicitly changes;
- association fails beyond the existing continuity window;
- reacquisition is incompatible with the prior box, predicted center, or
  target evidence; or
- frame geometry or ROI changes invalidate the coordinate relationship.

Short tracker-only continuity may retain the identifier, but every sample must
label its evidence as `live`, `projected`, `reacquired`, or `missing`.

The observational identity layer must not create a second target selector or
alter the production association decision. It only assigns identifiers and
quality labels to decisions the runtime already made.

Every identity-bearing sample includes `target_identity_quality`:

- `production_associated`: a stable identity exposed by the production
  association/tracker path;
- `strong_geometric_match`: strict box, center, motion, and evidence agreement;
- `weak_geometric_match`: plausible continuity that is useful for diagnostics
  but not model fitting;
- `projected_continuity`: tracker-only continuity without a live detection;
- `ambiguous`: crossing candidates or incompatible evidence prevent a safe
  identity decision.

ADS model eligibility accepts only `production_associated` or
`strong_geometric_match`. `weak_geometric_match`, `projected_continuity`, and
`ambiguous` remain diagnostic and cannot silently enter calibration data.

Emit `target_event` records for `created`, `switched`, `lost`, `reacquired`,
and `released`, including the previous and current identifiers and a compact
reason code.

## User Input Collection

### Controller Samples

At the configured normal sampling rate, record:

- raw physical right-stick X/Y and magnitude;
- deadzone-processed manual X/Y;
- AI aim X/Y;
- final pre-recoil X/Y;
- recoil X/Y and final output X/Y;
- LT and RT values;
- ADS/bodylock/controller mode;
- target presence, evidence state, authority state, and target age;
- target `dx`, `dy`, Euclidean error, body box, and frame dimensions;
- manual/AI dot product and direction relationship;
- output limiting reason flags;
- active `target_track_id`, identity quality, and `ads_event_id`;
- physical-input-read, vision-capture, inference-ready, controller-consume,
  output-sent, and sample timestamps when applicable;
- monotonically increasing per-stream `sample_seq`.

The normal persisted sampling rate is 100 Hz. While telemetry is enabled, a
fixed-size POD ring retains the latest 150 ms at 250 Hz in memory. Normal
operation enqueues a 100 Hz subset. When an event occurs, the collector flushes
the retained 100 ms pre-event evidence and continues enqueueing at 250 Hz for
300 ms after the event:

- ADS press or release;
- target create, switch, loss, or reacquisition;
- manual/AI direction conflict;
- target-axis crossing or measured overshoot;
- bodylock enter or exit;
- aim or fire authority change.

Flushed records retain their original timestamp and sequence. Deduplication by
`sample_seq` prevents the 100 Hz subset from being written twice. This design
is required because data sampled at only 100 Hz cannot be reconstructed as a
250 Hz pre-event window after an event occurs.

Do not log every controller tick by default. A separate debug mode may request
up to 1000 Hz while retaining the same non-blocking overflow behavior.

### Input Episodes

`UserInputEpisodeCollector` groups continuous manual input into anonymous
episodes. An episode begins when processed stick magnitude crosses the active
threshold and ends after it remains below the release threshold for a bounded
settle interval. It emits event markers rather than a derived user profile.

Markers include:

- input start, peak, direction reversal, target-axis crossing, settle, and end;
- reaction time relative to target availability or ADS press when known;
- target/controller mode context;
- whether AI was aligned, opposing, absent, or authority-limited.

Future offline analysis must be able to derive stick noise, effective deadzone,
initial strength, ramp rate, peak magnitude, braking distance, overshoot,
reverse-correction delay, correction count, and manual/AI conflict rate. No
such aggregate changes runtime behavior in this phase.

Episode statistics alone are descriptive and therefore at most
`profile_eligible`. A future controller compensation recommendation must also
use complete `control_response_window` evidence; it must not interpret an
input habit or correlation as a causal gain adjustment.

## Control-to-Image Response Pairing

`ControlResponseWindowAssembler` pairs an observed target error on vision frame
N with the controller commands sent after that observation and the target error
on the next compatible new vision frame N+1.

Each `control_response_window` contains:

- frame N and N+1 identifiers and target identity qualities;
- capture, inference-ready, controller-consume, output-sent, and next-capture
  timestamps;
- target `dx/dy`, box center/size, and evidence state before and after;
- time-integrated physical manual, processed manual, AI, pre-recoil, recoil,
  and final output over the command window;
- duration and sample count for every integral;
- tracker-predicted target motion over the same interval;
- observed `delta_error_x/y` and residual after predicted target motion;
- controller mode, ADS progress, weapon/profile context, and readiness;
- sequence ranges and completeness counters.

The window is `model_eligible` only when it uses compatible consecutive new
vision frames for the same high-quality identity, has complete controller
samples, and has a bounded observation-to-output timing relationship. Repeated
controller ticks that reuse the same vision frame do not create additional
response windows.

This pairing is necessary for future compensation. Without it, latency-driven
sustained input could be misclassified as a user preference and produce an
incorrect controller gain recommendation.

## ADS Transition Collection

### State Machine

Each ADS attempt follows:

```text
HipfireStable
  -> AdsPressed
  -> AdsTransition
  -> AdsSettled
  -> Completed

Any active state
  -> Invalid
```

- `HipfireStable`: a live selected target with a stable process-local identity
  is observed before LT crosses the ADS press threshold.
- `AdsPressed`: capture the last qualifying hipfire sample and start a new
  `ads_event_id`.
- `AdsTransition`: collect new vision samples and controller context while ADS
  FOV/state is changing.
- `AdsSettled`: visual transition evidence is stable and a qualifying live
  sample for the same target is available.
- `Completed`: emit one aggregate `ads_transition` record.
- `Invalid`: emit the event with an explicit reason; never silently discard it.

### Samples

For the hipfire anchor, each new vision frame during transition, and the settled
ADS anchor, record:

- target point, body box, screen center, frame dimensions, ROI, and FOV scale;
- `dx`, `dy`, normalized X/Y error, and Euclidean error;
- target identity and evidence state;
- target confidence/tier and aim/fire authority;
- tracker projected position, velocity, age, and continuity state;
- physical manual, AI, and final pre-recoil stick;
- elapsed milliseconds from ADS press;
- vision capture, result-ready, controller-consume, and output-sent timestamps;
- visual zoom scale, center offset, transition progress, settle confidence, and
  target-motion residual;
- cumulative manual, AI, pre-recoil, recoil, and final output since ADS press;
- sample sequence and completeness state.

The completed event contains:

- `hipfire_dx`, `hipfire_dy`, `ads_dx`, and `ads_dy`;
- `delta_dx = ads_dx - hipfire_dx`;
- `delta_dy = ads_dy - hipfire_dy`;
- Euclidean displacement and normalized displacement;
- press-to-first-ADS-frame and press-to-settled durations;
- target and tracker quality summaries;
- visual transform estimates `scale_x/y` and `offset_x/y` with confidence;
- cumulative command and predicted-target-motion summaries;
- `readiness`, `ads_calibration_class`, `valid`, and `invalid_reason`.

The raw before/after displacement is observational. It is not labeled as pure
ADS displacement until target motion and camera-command contribution are small
enough for the clean calibration gate.

### Visual ADS Transition Estimator

LT activation and configured snap duration indicate that an ADS attempt began,
but they do not prove that the game's visual ADS animation has settled.
`AdsVisualTransitionEstimator` therefore consumes only distinct new vision
frames from the same high-quality target identity and estimates:

- `visual_scale_x/y` from compatible body-box dimensions;
- `visual_center_offset_x/y` from the target/box transform;
- normalized `visual_progress` from the transition sequence;
- first derivative of scale and offset;
- residual after tracker-predicted target motion and accumulated command
  context;
- `settle_confidence`.

Visual ADS is settled only after a configured number of consecutive new frames
have scale/offset derivatives and motion residuals below compiled profile
thresholds. Configured ADS duration is a timeout/fallback bound, not visual
ground truth. Moving, occluded, clipped, or rapidly changing boxes reduce
confidence and may make the event diagnostic-only.

The estimator observes target geometry; it does not modify production vision,
tracker, or controller behavior.

### Validity Rules

An ADS calibration event is valid only when:

- the hipfire anchor and settled ADS anchor have the same nonzero
  `target_track_id`;
- both anchors use live target evidence;
- both anchors have `production_associated` or `strong_geometric_match`
  identity quality;
- there is no target switch during the event;
- frame dimensions and coordinate mapping remain compatible;
- the visual transition reaches settled confidence before its timeout;
- target age at both anchors is within the configured live-evidence limit;
- user input does not exceed the large-turn invalidation threshold; and
- the event contains the minimum number of new vision frames;
- sequence and required-sample completeness checks pass; and
- tracker/visual motion residual remains below the eligibility limit.

Valid ADS events receive an `ads_calibration_class` independent of the common
record `readiness`:

- `calibration_clean`: same high-quality live target, complete event, visual
  settle proven, and cumulative manual/AI/recoil plus target-motion residual
  are all below strict thresholds. This is `model_eligible` and may estimate
  the pure ADS visual transform directly.
- `conditional_model`: complete and identity-safe, but contains measurable
  command or target motion. This is `model_eligible` only with the recorded
  command and motion covariates.
- `diagnostic_only`: useful for debugging but excluded from model fitting.

Invalid reasons are an enum with at least:

- `no_hipfire_target`;
- `target_switched`;
- `target_lost`;
- `projected_only_anchor`;
- `ads_not_settled`;
- `large_manual_turn`;
- `geometry_changed`;
- `insufficient_frames`;
- `identity_ambiguous`;
- `visual_settle_unproven`;
- `motion_residual_high`;
- `sample_gap`;
- `runtime_shutdown`;
- `queue_overflow`.

Invalid events remain useful for diagnosing selection and tracker behavior but
must not enter a future ADS displacement model by default.

## Configuration

Keep the normal config surface small:

```toml
[runtime.telemetry]
enabled = false
mode = "profile"
manual_controller_hz = 100
```

Advanced defaults for event sampling, ADS timeout, minimum frames, and
large-turn invalidation remain compiled profile defaults unless evidence shows
that users need to tune them. Debug mode may expose command-line overrides for
short diagnostic runs without adding ordinary config keys.

The legacy aim-performance logging switch remains a compatibility alias for
enabling asynchronous telemetry, but it must not reactivate the old synchronous
file writer.

## Storage and Lifecycle

- Use JSONL initially for inspectability and compatibility with current tools.
- Continue bounded size rotation and maximum-file retention.
- Write session metadata at the beginning of every rotated file so each file
  is independently interpretable.
- Preserve completed `ads_transition` records whenever possible. Under queue
  pressure, drop normal controller samples before critical lifecycle records.
- Record drop counters by record type and mark an active ADS event invalid with
  `queue_overflow` if required samples were lost.
- Include sequence ranges, expected/written/dropped counts, and completeness in
  every aggregate so an offline reader can independently detect a hidden gap.
- A later offline compaction tool may convert JSONL to Parquet or a structured
  user-profile input dataset. That tool is outside this phase.

## Failure Handling

- Queue contention or full queue drops telemetry and increments counters; it
  never waits on the controller thread.
- Writer failure disables telemetry for the rest of the session and leaves
  runtime control active.
- Shutdown attempts a bounded flush. Any unfinished ADS event is emitted as
  `runtime_shutdown` if the queue still accepts it.
- Corrupt or truncated JSONL lines are skipped by offline readers and reported
  as invalid rows.
- Missing hashes or weapon metadata use `unknown`; they do not prevent logging.
- Unknown sensitivity/FOV/optic context keeps records diagnostic but prevents
  cross-session model fitting unless an offline analysis explicitly groups a
  proven-compatible context.

## Quantitative Acceptance

### Disabled Mode

Over a five-minute runtime test with telemetry disabled:

- telemetry files created: `0`;
- telemetry records constructed or serialized: `0`;
- writer threads started: `0`;
- target identity, input episode, and ADS collector state transitions: `0`;
- visual ADS estimator, response-window assembler, and completeness-gate state
  transitions: `0`;
- controller output matches the telemetry-compiled-out reference exactly for a
  deterministic replay;
- controller pipeline p99 regression is at most 1% versus compiled-out.

### Enabled Hot Path

At 100 Hz normal sampling with 250 Hz event windows:

- enqueue p95 <= 0.010 ms;
- enqueue p99 <= 0.025 ms;
- controller thread never blocks on telemetry;
- a 30-minute representative run has no dropped critical event records;
- normal sample drops are counted exactly and do not change controller output;
- file retention never exceeds the configured count and size allowance.

The existing maximum single-enqueue measurement is retained as a diagnostic,
not a release gate, because operating-system preemption can dominate one
sample. The release gate uses p99 plus an explicit non-blocking code-path test.

### Target Identity

Deterministic fixtures cover stable tracking, brief projected continuity,
reacquisition, target switching, crossing targets, and geometry changes:

- stable same-target ID retention: 100%;
- explicit target switches issuing a new ID: 100%;
- incompatible reacquisitions incorrectly retaining an ID: 0;
- ambiguous crossing-target fixtures marked `ambiguous`: 100%;
- the observational layer changes production selector/controller outputs: 0
  frames.

### ADS Events

For at least 100 deterministic ADS transitions:

- complete same-target transitions marked valid: 100%;
- target-switch, target-loss, projected-anchor, large-turn, geometry-change,
  timeout, and insufficient-frame fixtures marked invalid with the expected
  reason: 100%;
- hipfire and settled anchors use distinct new vision frames: 100%;
- `delta_dx`, `delta_dy`, Euclidean distance, and normalized coordinates match
  fixture ground truth within 0.01 pixel or 1e-5 normalized units;
- clean synthetic zoom sequences recover `scale_x/y` and `offset_x/y` within
  1% or 0.25 pixel, whichever bound is larger;
- visual settle is not declared from LT/configured duration alone: 100%;
- moving-target and accumulated-command fixtures are never labeled
  `calibration_clean` unless their residuals satisfy the clean thresholds;
- duplicate completed records per `ads_event_id`: 0;
- missing sequence fixtures are marked incomplete and excluded from model
  eligibility: 100%;
- controller/authority/fire outputs differ from logging-disabled replay: 0
  frames.

### User Input Evidence

Deterministic manual-input fixtures cover deadzone noise, slow ramp, fast flick,
braking, overshoot, reverse correction, aligned assistance, opposing
assistance, and target switching:

- episode boundaries and event markers match fixture ground truth exactly;
- normal samples remain within one sampling interval of schedule;
- event windows include at least 100 ms of pre-event and 300 ms of post-event
  evidence unless the session starts or ends inside the window;
- every sample needed to distinguish manual, AI, recoil, and final output
  contains all four components and the active controller mode.

### Control Response Windows

Deterministic fixtures cover zero command, manual-only, AI-only, recoil-only,
combined command, target motion, reused vision frames, delayed inference, and
missing controller samples:

- frame N to N+1 command integrals match fixture ground truth within 1e-5;
- reused vision frames create zero duplicate response windows;
- capture/consume/output/next-capture ordering is preserved exactly;
- target-motion residual matches fixture ground truth within 0.01 pixel;
- missing or ambiguous input is excluded from `model_eligible`: 100%;
- telemetry-enabled and disabled controller outputs differ on 0 frames.

## Non-Goals for This Phase

- generating or persisting `user_control_profile.json`;
- recommending or applying controller parameters;
- online learning or adaptive control;
- training a tracker or ADS displacement model;
- cross-session person recognition;
- video or screenshot capture;
- changing selector, tracker, controller, recoil, or fire-authority behavior.

## Future Use

After representative data has been collected and audited, a separate design
may introduce an offline profile builder. Any runtime compensation must have
its own shadow-mode evaluation, confidence threshold, bounded parameter set,
rollback path, and explicit user approval. Raw telemetry alone never grants
authority to change controller behavior.
