# User Input and ADS Telemetry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collect opt-in, model-ready native telemetry for user input habits, controller-to-image response, and same-target hipfire-to-ADS visual transitions without changing controller output.

**Architecture:** Keep the existing bounded asynchronous writer, but replace its underspecified record with a versioned fixed-size envelope. Add isolated observational collectors for target identity, event sampling, control-response windows, visual ADS transition, and ADS lifecycle/completeness; integrate them at the runtime boundary only when telemetry is enabled.

**Tech Stack:** C++17, MSVC/CMake, native runtime telemetry JSONL, deterministic executable tests, PowerShell native pipeline contract.

---

## File Structure

- `native/runtime_app/telemetry_schema.h`: fixed-size enums, samples, quality/readiness fields, and record envelope.
- `native/runtime_app/runtime_telemetry.{h,cpp}`: bounded queue, disabled fast path, JSONL serialization, rotation, counters.
- `native/runtime_app/telemetry_target_identity.{h,cpp}`: process-local target IDs and conservative identity quality.
- `native/runtime_app/telemetry_event_sampler.{h,cpp}`: 250 Hz POD pre-event ring and 100/250 Hz persistence decisions.
- `native/runtime_app/control_response_window.{h,cpp}`: consecutive-new-frame command integration and response records.
- `native/runtime_app/ads_visual_transition.{h,cpp}`: visual scale/offset progress and settle evidence.
- `native/runtime_app/ads_transition_collector.{h,cpp}`: ADS lifecycle, anchor validity, completeness, and calibration class.
- `native/runtime_app/telemetry_collectors.{h,cpp}`: enabled-only façade used by `RuntimeLoop`.
- `native/runtime_app/*_tests.cpp`: deterministic focused tests for each unit.
- `native/runtime_app/runtime_loop.{h,cpp}`: timestamped observational integration after controller output is computed.
- `native/vision_native/CMakeLists.txt`: sources and focused test executables.
- `scripts/verify/native_runtime_performance_acceptance.ps1`: include the new focused tests.
- `docs/project/NATIVE_CPP_RUNTIME.md`: operator-facing log schema and enablement notes.

### Task 1: Versioned Telemetry Schema and Complete JSONL Serialization

**Files:**
- Create: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/runtime_telemetry.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`

- [ ] **Step 1: Write failing schema/serialization tests**

Add tests that enqueue a controller record and an ADS aggregate, stop the writer, read JSONL, and require schema/readiness/sequence/completeness fields:

```cpp
TelemetryRecord value;
value.type = TelemetryRecordType::ControllerSample;
value.schema_version = 2;
value.sample_seq = 17;
value.readiness = TelemetryReadiness::ProfileEligible;
value.controller.manual_x = 0.25f;
value.completeness = {10, 17, 8, 8, 0, true};
REQUIRE(telemetry.enqueue(value));
// Require JSON contains schema_version=2, sample_seq=17,
// readiness=profile_eligible and complete=true.
```

Also extend disabled-mode assertions with `constructed_records == 0` and `collector_transitions == 0`.

- [ ] **Step 2: Build and prove the new test fails**

Run:

```powershell
& $cmake --build native/vision_native/build --config Release --target cod_native_runtime_telemetry_tests
native/vision_native/build/Release/cod_native_runtime_telemetry_tests.exe
```

Expected: compilation fails because the new schema types do not exist.

- [ ] **Step 3: Add the fixed-size schema**

Define string-free hot-path enums and POD payloads:

```cpp
enum class TelemetryRecordType : std::uint8_t {
    SessionMetadata, ControllerSample, InputEvent, TargetEvent,
    AdsTransitionSample, AdsTransition, ControlResponseWindow
};
enum class TelemetryReadiness : std::uint8_t {
    Diagnostic, ProfileEligible, ModelEligible
};
struct TelemetryCompleteness {
    std::uint64_t first_seq = 0, last_seq = 0;
    std::uint32_t expected = 0, written = 0, dropped = 0;
    bool complete = false;
};
struct TelemetryTimestamps {
    std::uint64_t physical_read_ns = 0, vision_capture_ns = 0;
    std::uint64_t inference_ready_ns = 0, controller_consume_ns = 0;
    std::uint64_t output_sent_ns = 0, sample_ns = 0;
};
```

Use a tagged `TelemetryRecord` with fixed-size payload structs. Do not store `std::string` or `std::vector` in queued records.

- [ ] **Step 4: Serialize every enum and completeness field**

Add total switch functions returning stable schema strings and serialize only the payload appropriate to the record type. Preserve legacy field names for controller samples where practical.

- [ ] **Step 5: Run focused telemetry tests**

Expected: all existing queue/rotation/failure tests and new schema tests pass.

- [ ] **Step 6: Commit**

```powershell
git add native/runtime_app/telemetry_schema.h native/runtime_app/runtime_telemetry.h native/runtime_app/runtime_telemetry.cpp native/runtime_app/runtime_telemetry_tests.cpp
git commit -m "Add versioned model-ready telemetry schema"
```

### Task 2: Conservative Runtime Target Identity

**Files:**
- Create: `native/runtime_app/telemetry_target_identity.h`
- Create: `native/runtime_app/telemetry_target_identity.cpp`
- Create: `native/runtime_app/telemetry_target_identity_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing identity tests**

Cover stable live boxes, brief projected continuity, strong reacquisition, incompatible reacquisition, explicit loss, geometry change, and crossing ambiguity:

```cpp
TelemetryTargetIdentity identity;
auto first = identity.observe(live(1, 100, 100, 180, 260));
auto same = identity.observe(live(2, 103, 101, 183, 261));
REQUIRE(first.track_id != 0);
REQUIRE(same.track_id == first.track_id);
REQUIRE(same.quality == TargetIdentityQuality::StrongGeometricMatch);
auto crossing = identity.observe(ambiguous_live(3));
REQUIRE(crossing.quality == TargetIdentityQuality::Ambiguous);
REQUIRE(!crossing.model_eligible());
```

- [ ] **Step 2: Build and prove the test fails**

Expected: missing target and types.

- [ ] **Step 3: Implement identity as an observer, not a selector**

Expose:

```cpp
struct TargetIdentityObservation {
    std::uint64_t frame_id;
    int frame_width, frame_height;
    bool live, projected, explicit_switch, association_ambiguous;
    float x1, y1, x2, y2, target_x, target_y;
};
struct TargetIdentityResult {
    std::uint64_t track_id;
    std::uint64_t previous_track_id;
    TargetIdentityQuality quality;
    TargetEventKind event;
    bool model_eligible() const noexcept;
};
```

Retain an ID only for compatible geometry and strict IoU/center evidence. Issue a new ID on explicit switch, incompatible reacquisition, or geometry change. Mark crossing/ambiguous evidence without guessing.

- [ ] **Step 4: Add CMake target and run tests twice**

Run `cod_native_telemetry_target_identity_tests.exe` twice; expected identical PASS.

- [ ] **Step 5: Commit**

```powershell
git add native/runtime_app/telemetry_target_identity.* native/runtime_app/telemetry_target_identity_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Add conservative telemetry target identity"
```

### Task 3: Event Sampler and User Input Episodes

**Files:**
- Create: `native/runtime_app/telemetry_event_sampler.h`
- Create: `native/runtime_app/telemetry_event_sampler.cpp`
- Create: `native/runtime_app/telemetry_event_sampler_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing sampler tests**

Feed 250 Hz samples for 200 ms, trigger an event, and require a unique 100 ms pre-window plus 300 ms post-window:

```cpp
TelemetryEventSampler sampler({250, 100, 100, 300});
for (std::uint64_t seq = 1; seq <= 50; ++seq) sampler.observe(sample(seq, seq * 4'000'000));
sampler.trigger(InputEventKind::AdsPressed, 200'000'000);
auto emitted = sampler.drain();
REQUIRE(unique_sequences(emitted));
REQUIRE(first_time(emitted) <= 100'000'000);
```

Add deterministic episode tests for deadzone noise, ramp, peak, reversal, crossing, settle, and end markers.

- [ ] **Step 2: Build and prove tests fail**

Expected: missing sampler.

- [ ] **Step 3: Implement fixed-capacity POD ring and deduplication**

Use a preallocated circular array sized from `ring_hz * retention_ms`. `observe()` is bounded and allocation-free. `trigger()` marks retained sequences for emission; it does not serialize.

- [ ] **Step 4: Implement episode state machine**

Expose threshold and settle duration in constructor options, emit fixed enum markers, and never calculate a profile or tuning value.

- [ ] **Step 5: Run sampler tests and telemetry overflow tests**

Expected: event evidence is complete when capacity is sufficient and explicitly incomplete when it is not.

- [ ] **Step 6: Commit**

```powershell
git add native/runtime_app/telemetry_event_sampler.* native/runtime_app/telemetry_event_sampler_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Collect input episodes and event windows"
```

### Task 4: Control-to-Image Response Windows

**Files:**
- Create: `native/runtime_app/control_response_window.h`
- Create: `native/runtime_app/control_response_window.cpp`
- Create: `native/runtime_app/control_response_window_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing response-window tests**

Construct frame N, three controller samples, and frame N+1. Require time-weighted component integrals and error delta:

```cpp
ControlResponseWindowAssembler assembler;
assembler.observe_vision(frame(10, 1'000, 20.0f, -5.0f, 7));
assembler.observe_controller(command(1'100, 0.2f, 0.1f, 0.3f));
assembler.observe_controller(command(1'200, 0.4f, 0.1f, 0.5f));
auto result = assembler.observe_vision(frame(11, 1'300, 15.0f, -3.0f, 7));
REQUIRE(result.has_value());
REQUIRE_NEAR(result->delta_error_x, -5.0f, 0.01f);
REQUIRE(result->readiness == TelemetryReadiness::ModelEligible);
```

Cover reused frame IDs, target switch, missing sequence, delayed result, and target-motion residual.

- [ ] **Step 2: Build and prove tests fail**

Expected: missing assembler.

- [ ] **Step 3: Implement consecutive-new-frame pairing**

Integrate physical/manual/AI/pre-recoil/recoil/final components using their sample durations. Do not create a window for repeated controller consumption of the same frame.

- [ ] **Step 4: Add completeness and identity gates**

Only compatible frame geometry and high-quality same-target identities may become model eligible. All other windows remain diagnostic with a reason enum.

- [ ] **Step 5: Run focused tests**

Expected: exact deterministic integrals, no duplicate windows.

- [ ] **Step 6: Commit**

```powershell
git add native/runtime_app/control_response_window.* native/runtime_app/control_response_window_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Pair controller commands with image response"
```

### Task 5: Visual ADS Progress and Settle Evidence

**Files:**
- Create: `native/runtime_app/ads_visual_transition.h`
- Create: `native/runtime_app/ads_visual_transition.cpp`
- Create: `native/runtime_app/ads_visual_transition_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing visual-transition tests**

Generate synthetic box sequences with scale 1.0 -> 1.4 and stable final frames:

```cpp
AdsVisualTransitionEstimator estimator({3, 0.01f, 0.25f});
estimator.start(anchor_box(100, 100, 200, 300), 1);
for (auto frame : zoom_sequence({1.10f, 1.25f, 1.39f, 1.40f, 1.40f, 1.40f}))
    last = estimator.observe(frame);
REQUIRE(last.settled);
REQUIRE_NEAR(last.scale_x, 1.40f, 0.01f);
```

Require LT/timer-only progression never settles, moving residual prevents clean calibration, clipped/ambiguous boxes lower confidence, and repeated frame IDs are ignored.

- [ ] **Step 2: Build and prove tests fail**

- [ ] **Step 3: Implement bounded estimator**

Maintain a fixed recent-frame window. Estimate scale from anchor/current width and height, center offset from transformed target/box center, derivative from new-frame timestamps, and settle only after consecutive derivative/residual passes.

- [ ] **Step 4: Run deterministic estimator tests twice**

Expected: scale/offset within the spec tolerances and identical results.

- [ ] **Step 5: Commit**

```powershell
git add native/runtime_app/ads_visual_transition.* native/runtime_app/ads_visual_transition_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Estimate visual ADS transition progress"
```

### Task 6: ADS Lifecycle, Calibration Class, and Completeness Gate

**Files:**
- Create: `native/runtime_app/ads_transition_collector.h`
- Create: `native/runtime_app/ads_transition_collector.cpp`
- Create: `native/runtime_app/ads_transition_collector_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing ADS lifecycle tests**

Cover 100 complete same-target transitions and one fixture for every invalid reason. Require distinct hipfire/settled frames and one aggregate per event ID.

```cpp
AdsTransitionCollector collector(options);
collector.observe_hipfire(clean_frame(1, 7));
collector.on_ads_pressed(1'000'000);
for (const auto& frame : clean_zoom_frames(7)) collector.observe_vision(frame);
auto event = collector.take_completed();
REQUIRE(event.valid);
REQUIRE(event.ads_calibration_class == AdsCalibrationClass::CalibrationClean);
REQUIRE(event.hipfire_frame_id != event.settled_frame_id);
```

- [ ] **Step 2: Build and prove tests fail**

- [ ] **Step 3: Implement lifecycle and anchor rules**

Use `HipfireStable -> AdsPressed -> AdsTransition -> AdsSettled -> Completed`, with any active state able to emit `Invalid`. Require live high-quality same-target anchors.

- [ ] **Step 4: Implement calibration classification**

`CalibrationClean` requires complete sequences, visual settle, and strict cumulative command/recoil/motion thresholds. `ConditionalModel` requires complete covariates. Everything else is `DiagnosticOnly` with a specific reason.

- [ ] **Step 5: Implement overflow/shutdown finalization**

Required-sample loss produces `QueueOverflow` or `SampleGap`; shutdown emits `RuntimeShutdown` without blocking.

- [ ] **Step 6: Run focused tests**

Expected: all 100 valid fixtures and every invalid-reason fixture classify exactly.

- [ ] **Step 7: Commit**

```powershell
git add native/runtime_app/ads_transition_collector.* native/runtime_app/ads_transition_collector_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Collect quality-gated ADS transitions"
```

### Task 7: Enabled-Only Runtime Integration

**Files:**
- Create: `native/runtime_app/telemetry_collectors.h`
- Create: `native/runtime_app/telemetry_collectors.cpp`
- Create: `native/runtime_app/telemetry_collectors_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing disabled/integration tests**

Instantiate the façade disabled and require zero allocation-visible state, records, transitions, and writer activity. Feed a deterministic enabled replay and require controller outputs byte-for-byte equal to the disabled replay.

- [ ] **Step 2: Build and prove tests fail**

- [ ] **Step 3: Implement optional façade**

Expose:

```cpp
class TelemetryCollectors {
public:
    explicit TelemetryCollectors(bool enabled, RuntimeTelemetry* sink);
    bool enabled() const noexcept;
    void observe_tick(const TelemetryTickInput&) noexcept;
    void observe_new_vision(const TelemetryVisionInput&) noexcept;
    void shutdown(std::uint64_t now_ns) noexcept;
};
```

When disabled, hold no collector objects and return immediately. When enabled, own the preallocated collectors and enqueue only fixed-size records.

- [ ] **Step 4: Integrate timestamp capture in `RuntimeLoop`**

Capture physical-read time immediately after input, controller-consume time when a new vision result is accepted, and output-sent time after ViGEm update. Pass the existing vision capture/inference/result timestamps, output components, LT/RT, aim mode, target geometry, and authority state to the façade.

- [ ] **Step 5: Emit session metadata once per file/session**

Use effective config/build/engine context already available at startup; unknown game sensitivity/optic/FOV fields remain explicitly unknown and diagnostic-only across incompatible sessions.

- [ ] **Step 6: Run integration and existing focused tests**

Run telemetry collectors, telemetry writer, runtime config, vision service, controller, target selector, and recoil contract tests. Expected: PASS.

- [ ] **Step 7: Commit**

```powershell
git add native/runtime_app/telemetry_collectors.* native/runtime_app/telemetry_collectors_tests.cpp native/runtime_app/runtime_loop.* native/vision_native/CMakeLists.txt
git commit -m "Integrate observational telemetry collectors"
```

### Task 8: Acceptance Script, Documentation, and Full Regression

**Files:**
- Modify: `scripts/verify/native_runtime_performance_acceptance.ps1`
- Modify: `docs/project/NATIVE_CPP_RUNTIME.md`
- Modify: `docs/project/NATIVE_RUNTIME_PERFORMANCE_ACCEPTANCE.md`

- [ ] **Step 1: Add all new executables to focused acceptance**

Require these binaries:

```powershell
cod_native_telemetry_target_identity_tests.exe
cod_native_telemetry_event_sampler_tests.exe
cod_native_control_response_window_tests.exe
cod_native_ads_visual_transition_tests.exe
cod_native_ads_transition_collector_tests.exe
cod_native_telemetry_collectors_tests.exe
```

- [ ] **Step 2: Document enablement and record readiness**

Document that telemetry defaults off; explain `diagnostic`, `profile_eligible`, `model_eligible`, ADS calibration classes, local-only JSONL rotation, and that this phase does not tune the controller.

- [ ] **Step 3: Build the complete Release tree**

Run:

```powershell
& $cmake --build native/vision_native/build --config Release --parallel
```

Expected: exit 0.

- [ ] **Step 4: Run all new tests and native pipeline contract**

Run each new executable, then:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verify/native_pipeline_contract.ps1 -SkipBuild
```

Expected: every test and contract exits 0.

- [ ] **Step 5: Re-run telemetry performance evidence**

Run one million enqueue samples and record p95/p99 plus drops. Verify the code-path test proves non-blocking behavior; retain maximum latency only as diagnostic.

- [ ] **Step 6: Inspect repository scope**

Run `git diff --check`, `git status --short`, and confirm `config.native.example.toml` deletion plus root benchmark JSON files remain untouched/uncommitted.

- [ ] **Step 7: Commit documentation and acceptance wiring**

```powershell
git add scripts/verify/native_runtime_performance_acceptance.ps1 docs/project/NATIVE_CPP_RUNTIME.md docs/project/NATIVE_RUNTIME_PERFORMANCE_ACCEPTANCE.md
git commit -m "Document user and ADS telemetry acceptance"
```
