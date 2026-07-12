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

ADS state + identified target + samples
  -> AdsTransitionCollector --------> ads_transition_sample / ads_transition

All fixed-size records
  -> existing bounded telemetry queue
  -> background serialization and rotation
```

Collectors exist only when telemetry is enabled. The controller thread may
populate fixed-size records and attempt a non-blocking enqueue. Target
association summaries, event aggregation, and JSON serialization must not
block the 1 ms controller loop.

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
- `ads_transition`.

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
- active `target_track_id` and `ads_event_id`.

The normal default sampling rate is 100 Hz. A 250 Hz event window is permitted
from 100 ms before through 300 ms after these events, using an in-memory
pre-event ring:

- ADS press or release;
- target create, switch, loss, or reacquisition;
- manual/AI direction conflict;
- target-axis crossing or measured overshoot;
- bodylock enter or exit;
- aim or fire authority change.

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
- `AdsSettled`: the runtime ADS state is stable and a qualifying live sample
  for the same target is available.
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
- elapsed milliseconds from ADS press.

The completed event contains:

- `hipfire_dx`, `hipfire_dy`, `ads_dx`, and `ads_dy`;
- `delta_dx = ads_dx - hipfire_dx`;
- `delta_dy = ads_dy - hipfire_dy`;
- Euclidean displacement and normalized displacement;
- press-to-first-ADS-frame and press-to-settled durations;
- target and tracker quality summaries;
- `valid` and `invalid_reason`.

### Validity Rules

An ADS calibration event is valid only when:

- the hipfire anchor and settled ADS anchor have the same nonzero
  `target_track_id`;
- both anchors use live target evidence;
- there is no target switch during the event;
- frame dimensions and coordinate mapping remain compatible;
- the transition reaches ADS settled before its timeout;
- target age at both anchors is within the configured live-evidence limit;
- user input does not exceed the large-turn invalidation threshold; and
- the event contains the minimum number of new vision frames.

Invalid reasons are an enum with at least:

- `no_hipfire_target`;
- `target_switched`;
- `target_lost`;
- `projected_only_anchor`;
- `ads_not_settled`;
- `large_manual_turn`;
- `geometry_changed`;
- `insufficient_frames`;
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

## Quantitative Acceptance

### Disabled Mode

Over a five-minute runtime test with telemetry disabled:

- telemetry files created: `0`;
- telemetry records constructed or serialized: `0`;
- writer threads started: `0`;
- target identity, input episode, and ADS collector state transitions: `0`;
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
- duplicate completed records per `ads_event_id`: 0;
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
