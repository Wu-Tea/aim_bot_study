# Native Runtime Performance and Simplified Configuration Design

**Date:** 2026-07-12
**Status:** Approved design
**Owner:** Codex / user discussion

## Goal

Make the default native gamepad runtime efficient and predictable on constrained
machines while preserving its behavioral contracts:

- keep the controller loop at a 1 ms logical cadence;
- keep non-aim vision warm at 20 Hz so the first aimed frame does not pay a cold
  initialization penalty;
- make the requested active vision frequency configurable through one clear key;
- make detailed logging opt-in and remove file-formatting work from the control
  thread;
- reduce avoidable GPU-to-CPU color-transfer overhead without changing color,
  target, aim-authority, or fire-authority semantics;
- separate modern RTX/Turing+ deployment from legacy Pascal deployment;
- reduce the normal user-facing `config.toml` to settings a user is expected to
  understand, while keeping recoil fully configurable.

## Context

The default live gamepad path is native C++:

```text
DXGI ROI capture
  -> CUDA preprocess
  -> TensorRT inference
  -> target selection and color evidence
  -> tracker and target authority
  -> ADS/bodylock/manual arbitration
  -> recoil
  -> ViGEm output
```

The current local configuration requests a 480x416 engine and contains a large
number of controller and authority parameters. The active native runtime uses a
GPU service, but `capture_fps` and `gpu_service_active_fps` currently describe
overlapping cadence concepts. Detailed aim JSONL logging can run every 1 ms while
aiming. The vision service keeps the engine warm at 20 Hz while not aiming, but
an idle wait is not explicitly interrupted by an aim rising edge.

The existing design
`docs/superpowers/specs/2026-06-18-native-vision-memory-gpu-optimization-design.md`
already covers persistent buffer reuse and feature-flagged direct CUDA-array
preprocessing. This design complements it; it does not supersede its parity,
fallback, or rollback requirements.

## User Requirements

- Detailed logging exists for debugging, later performance investigation, and
  future user-operation profiling, but it may be disabled during normal play.
- Non-aim 20 Hz vision keepwarm remains available and enabled by the selected
  normal profile.
- The active requested rate, including 160 Hz, remains configurable.
- Controller timing remains logically 1 ms. The waiting implementation may
  change only if cadence and output behavior are preserved or improved.
- Color classification remains behaviorally equivalent. Its transfer path may
  be optimized behind parity and fallback checks.
- Normal configuration should be small. The user is comfortable tuning recoil
  but should not need to understand dozens of selector/controller parameters.

## Non-Goals

- Do not reduce the controller loop below 1000 Hz.
- Do not remove the non-aim keepwarm mode.
- Do not hard-code 60 Hz or silently lower a configured 160 Hz request.
- Do not dynamically tune target freshness, target authority, ADS/bodylock force,
  fire authority, or recoil based on runtime load.
- Do not disable color classification merely to save transfer time.
- Do not implement online user-profile learning in this slice.
- Do not remove legacy config parsing until a migration period has completed.
- Do not claim GTX 1060 support until a Pascal build and engine run on SM 6.1
  hardware.

## Design Overview

The work is split into six independently verifiable boundaries:

1. simplified configuration with versioned runtime profiles;
2. unified active/idle vision cadence and immediate aim wakeup;
3. opt-in asynchronous telemetry;
4. a 1 ms precision tick scheduler;
5. staged color-transfer optimization;
6. Pascal deployment compatibility.

Each boundary must be independently revertible. None may change target or
controller policy as an incidental performance optimization.

## 1. Simplified Configuration

### Configuration Layers

Resolve configuration in this order:

```text
compiled safety defaults
  -> version-controlled runtime profile
  -> user config.toml overrides
  -> environment / CLI overrides
  -> validation and hard safety clamps
```

The normal `config.toml` is an override file, not a copy of every default.

Recommended normal file shape:

```toml
[runtime]
profile = "balanced"

[runtime.vision]
capture_fps = 160
idle_capture_fps = 20
keepwarm_when_idle = true

[runtime.telemetry]
enabled = false
mode = "debug"

[gamepad.recoil]
# Keep the existing documented recoil parameters here.
enabled = true
feedback_amount = 0.30
profile_amount = 1.0
profile_x_amount = 1.0
profile_lead_ms = 0
profile_velocity_reference_ms = 100
```

Version-controlled profile files hold advanced defaults:

```text
config/runtime_profiles/balanced.toml
config/runtime_profiles/low_resource.toml
config/runtime_profiles/diagnostic.toml
config/runtime_profiles/pascal_balanced.toml
```

Advanced controller, selector, tracker, auto-fire, and authority parameters stay
in profiles or compiled safety defaults. Users can still override them in
`config.toml`, but they are absent from the normal template.

Provide a complete expert reference separately:

```text
config/examples/config.full.toml
```

### Backward Compatibility

- Continue accepting the existing full `config.toml` sections and keys.
- Preserve environment and CLI precedence.
- Unknown keys produce a warning; they must not be silently ignored.
- Deprecated keys produce a migration warning and identify the resolved new key.
- Add `--dump-effective-config` or equivalent read-only output that prints the
  resolved profile, source of each override, and effective values.
- Startup output must print a concise summary, not all advanced parameters.

### User-Facing Versus Safety Settings

Normal user-facing keys:

- runtime profile;
- active and idle vision rates;
- keepwarm;
- telemetry switch and mode;
- model/engine selection when needed;
- recoil settings.

Profile-managed advanced keys:

- selector thresholds and validity weights;
- tracker projection and continuity parameters;
- ADS/bodylock policy constants;
- manual/AI arbitration thresholds;
- fire-authority gates;
- diagnostic-only limits.

Safety rules remain compiled/profile-controlled and are never relaxed by a
friendly user-facing preset.

## 2. Unified Vision Cadence and Aim Wakeup

### Canonical Cadence Keys

Use these canonical meanings:

```toml
[runtime.vision]
capture_fps = 160          # active requested poll rate
idle_capture_fps = 20      # non-aim keepwarm rate
keepwarm_when_idle = true
```

`capture_fps` must control both the GPU-service path and the direct synchronous
path. A configured rate is a requested maximum, not guaranteed throughput.

Legacy `gpu_service_active_fps` and `gpu_service_idle_fps` remain accepted during
migration. A nonzero explicit legacy value may override the canonical key, but
startup output must report the source. Their new defaults should inherit rather
than create a second independent default.

### Latest-Only Scheduling

- Never create an inference backlog to satisfy a requested rate.
- Each poll captures the newest available frame.
- If inference is slower than the requested interval, start the next poll only
  after the previous poll completes.
- Record requested and achieved rates separately.

### Immediate Aim Wakeup

Preserve 20 Hz non-aim keepwarm, but do not let its 50 ms interval delay an aim
transition.

On `aiming: false -> true`:

1. signal the vision worker immediately;
2. cancel or interrupt the idle wait;
3. perform a fresh poll without waiting for the next idle deadline;
4. enter the active cadence;
5. do not grant aim or fire authority from a pre-aim result;
6. submit authority only from a valid post-transition fresh sequence.

Use a condition variable or Windows event/wait primitive rather than an
uninterruptible sleep loop. Record:

```text
aim_wakeup_to_capture_ms
aim_wakeup_to_result_ms
requested_vision_fps
achieved_vision_fps
```

## 3. Opt-In Asynchronous Telemetry

### Modes

```toml
[runtime.telemetry]
enabled = false
mode = "debug"             # debug | profile
manual_controller_hz = 100
vision_on_new_frame = true
candidate_details = "on_event"
queue_capacity = 8192
rotate_size_mb = 256
max_files = 10
event_pre_ms = 500
event_post_ms = 1000
```

When disabled:

- do not create a log file;
- do not encode JSON;
- do not calculate diagnostics used only for logging;
- retain only cheap counters required for control correctness.

`debug` mode supports short, high-detail captures. It may request 1000 Hz
manual/controller samples, but file formatting remains off the control thread.

`profile` mode is intended for long sessions and future user-operation analysis:

- manual/controller snapshots at 50-100 Hz;
- vision records only on a new vision frame;
- candidate details only on new frames or selected events;
- event-triggered pre/post windows for opposition, overshoot, target switch,
  stale/projected authority, fire decisions, and timing spikes.

### Event Model

Use separate records joined by identifiers:

```text
ManualControllerTick: tick_id, frame_id, intent_id, timestamps
VisionFrame: frame_id, detections, candidates, selection, timings
RuntimeEvent: event_id, tick_id, frame_id, reason, pre/post window
```

Do not repeat a full candidate list in every 1 ms controller record.

### Writer Boundary

- The control thread copies a bounded, fixed-size sample into a bounded queue.
- A background writer serializes and writes records.
- Queue overflow drops telemetry rather than blocking control.
- Record dropped sample/event counts.
- Rotate files by size and cap retained file count.
- Drain the queue on normal shutdown within a bounded timeout.
- Preserve abrupt-exit tolerance: a partially written final line must not make
  prior records unreadable.

Legacy `aim_perf_file_log` and `aim_perf_log_interval_ticks` remain accepted and
map to telemetry settings with a migration warning.

## 4. One-Millisecond Controller Scheduler

### Contract

```text
controller_tick_hz = 1000
logical_interval = 1 ms
absolute deadlines
no accumulated drift
no replay of multiple missed ticks
```

Replace full-interval repeated `yield()` with a dedicated scheduler:

1. calculate an absolute next deadline;
2. use a high-resolution Windows waitable timer for coarse waiting;
3. use a configurable short spin/yield tail for the final tens of microseconds;
4. if late, run one current tick and re-align to the next valid deadline;
5. retain the current timing path as a fallback when high-resolution timers are
   unavailable.

Do not couple controller cadence to vision cadence.

### Timing Metrics

Collect cheap rolling counters and expose them through telemetry/perf output:

```text
tick_interval_p50/p95/p99
tick_late_us_p50/p95/p99
missed_deadlines
max_consecutive_late_ticks
controller_pipeline_ms
vigem_update_ms
```

The implementation is accepted only if cadence and output behavior remain stable
and CPU/context-switch pressure does not worsen.

## 5. Color Transfer Optimization

### Stage A: Behavior-Equivalent Pinned Readback

Keep the existing selector and CPU color semantics.

- Reuse a high-watermark host buffer.
- Prefer pinned host memory for color readback.
- Copy only the selector-required region.
- Use the existing CUDA stream and an asynchronous D2H operation where possible.
- Synchronize before CPU color consumers access the buffer.
- On allocation or transfer setup failure, fall back to the existing pageable
  buffer path for that frame.
- Do not alter friendly, cue, color bonus, target validity, aim authority, or
  fire authority as part of this optimization.

Record:

```text
color_copy_required
color_copy_bytes
color_copy_region_ratio
color_copy_ms
color_classify_ms
color_candidate_count
color_readback_mode
```

### Stage B: Compact GPU Color Evidence

Only implement this if Stage A target-hardware logs show a meaningful p95 cost.

The later GPU path may compute per-candidate HSV/cue evidence on the GPU and copy
only compact evidence structures to CPU. It must retain the CPU path as fallback
and pass fixture-level CPU/GPU parity before affecting live selection.

### Relationship to Direct CUDA-Array Preprocess

The June 18 feature-flagged direct CUDA-array preprocessing remains a separate
optimization. Color-readback work must not silently extend D3D/CUDA resource map
lifetime or introduce extra full-stream synchronizations.

## 6. Pascal / GTX 1060 Deployment Boundary

The current CUDA 13.1 / TensorRT 10.15 development stack does not target Pascal
SM 6.1. Treat Pascal as a separate deployment artifact:

```text
modern build:
  CUDA 13.x + TensorRT 10.x
  Turing/SM 7.5 or newer

pascal build:
  CUDA 11.8 or supported CUDA 12.x
  TensorRT 8.6.1
  Pascal/SM 6.1
```

Requirements:

- separate build preset and documented dependency roots;
- compile-time adaptation for TensorRT API differences;
- a separately built Pascal engine;
- FP32, FP16, and calibrated INT8 target-hardware A/B;
- no claim of support based only on compilation on modern hardware;
- record runtime, engine, CUDA, TensorRT, GPU, and driver signatures in debug
  telemetry.

If no Pascal target is available, code/build boundaries may be prepared, but the
feature remains unverified.

## Error Handling and Fallbacks

- Invalid profile name: fail startup with a clear list of available profiles.
- Invalid user override: report key, source, and accepted range; do not silently
  clamp ordinary configuration mistakes.
- Safety-bound override: clamp or reject according to the existing safety rule
  and identify the rule.
- Telemetry queue full: drop telemetry and increment counters.
- Telemetry writer failure: disable file telemetry, keep runtime control active.
- High-resolution timer unavailable: use the existing scheduler and report the
  fallback mode.
- Pinned color buffer failure: use the current pageable path.
- Aim wake signal failure: keep polling on the vision worker's existing fallback
  schedule, report the degraded wake path, and never move inference onto or block
  the controller thread.
- Unsupported GPU/runtime pair: fail startup before loading an incompatible
  engine and explain the required build family.

## Validation Plan

### Configuration

- Parse the new minimal `config.toml`.
- Parse an existing full legacy config unchanged.
- Verify precedence: defaults -> profile -> user -> environment/CLI.
- Verify legacy cadence and log keys map predictably.
- Verify unknown/deprecated key diagnostics.
- Snapshot-test concise startup output and effective-config dump.

### Vision Cadence and Wakeup

- Unit-test active and idle interval resolution.
- Test `capture_fps` controls service and direct paths.
- Test no inference backlog is created.
- Test false->true aim transition interrupts idle wait.
- Assert the first authority-bearing result is post-transition and fresh.
- Measure wake-to-capture and wake-to-result p50/p95/p99.

### Telemetry

- Disabled mode creates no file and performs no JSON formatting.
- Queue operation remains bounded and non-blocking.
- Overflow drops records and increments counters.
- Debug and profile sampling rates are correct.
- Vision/candidate records are not duplicated per controller tick.
- Rotation, bounded shutdown drain, and malformed final-line recovery work.

### Controller Scheduler

- Unit-test deadline and late-tick arithmetic.
- Measure at least 60 seconds at 1000 Hz.
- Compare p50/p95/p99 interval and lateness against the current scheduler.
- Compare CPU usage and context switches.
- Verify controller, ViGEm, ADS, bodylock, and recoil tests are unchanged.

### Color Readback

- Compare pageable and pinned paths on deterministic BGRA fixtures.
- Require identical per-candidate color/friendly/cue/authority outputs.
- Exercise allocation failure and fallback.
- Compare copy/classification p50/p95/p99 and mapped-resource lifetime.

### Native Contracts

Run focused runtime/config/vision tests, then:

```powershell
scripts\verify\native_pipeline_contract.bat
```

Controller and recoil behavior must not change.

### Target Hardware

For each hardware profile, capture:

- game resolution, refresh rate, graphics preset, and game frame-time p95/p99;
- requested/achieved vision FPS;
- capture, map, preprocess, inference, color copy, selector, and vision-age
  p50/p95/p99;
- controller tick lateness and missed deadlines;
- process RAM and GPU memory;
- telemetry overhead off versus debug/profile;
- aim wake latency from idle 20 Hz;
- target-source, authority, wrong-way, overshoot, and bodylock health metrics.

## Acceptance Criteria

- Normal `config.toml` is small and recoil remains directly editable.
- Existing full config files continue to work during migration.
- `capture_fps` controls the active service cadence and remains freely
  configurable, including 160 Hz.
- Non-aim 20 Hz keepwarm remains enabled in the balanced profile.
- Aim rising edge no longer waits for the next idle 20 Hz deadline.
- Telemetry disabled mode has no file/JSON work in the control loop.
- Telemetry enabled mode cannot block the controller thread.
- Controller cadence remains 1 ms with no behavioral regression.
- Color classification and authority outputs remain equivalent.
- Unsupported Pascal/modern runtime combinations fail clearly.
- No performance optimization weakens fire-authority or recoil isolation rules.

## Implementation Sequence

1. Add runtime profile loading and a minimal config template while preserving
   legacy parsing.
2. Make `capture_fps` canonical for service/direct active cadence and add concise
   effective-config reporting.
3. Add interruptible vision-service waiting and immediate aim wakeup tests.
4. Introduce the telemetry config boundary and disabled fast path.
5. Move serialization/writing to a bounded background writer.
6. Add debug/profile record schemas, sampling, event windows, rotation, and drop
   counters.
7. Implement and benchmark the precision tick scheduler behind a fallback.
8. Add pinned color readback with parity and fallback tests.
9. Run native contracts and live modern-hardware A/B.
10. Prepare the Pascal build/API boundary and build target-specific engines.
11. Validate on real SM 6.1 hardware before marking GTX 1060 supported.

## Rollback

- Profiles can resolve to existing compiled defaults.
- Legacy config keys remain usable during migration.
- Vision service can fall back to the current sleep scheduler.
- Controller scheduler can fall back to the current `sleep_until_precise` path.
- Telemetry can be disabled completely.
- Pinned color readback can fall back per-frame to pageable memory.
- Modern and Pascal artifacts remain separate; neither overwrites the other.
