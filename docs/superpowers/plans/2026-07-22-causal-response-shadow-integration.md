# Causal Response Shadow Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a production-shaped, memory-only causal response learner and short-horizon evaluator that can prove response, latency, pending-motion, and future-burden estimates in shadow mode without changing target selection, ADS, BodyLock, AutoFire, recoil, or delivered gamepad output.

**Architecture:** Extend the existing `ControlResponseWindowAssembler` into the single owner of frame-exact delivered-control history, then feed a capture-time committed observation contract into a fixed-capacity delay-bank + robust EW-RLS learner. Validate the learner first with a closed-loop synthetic plant and real telemetry replay; only after G2 passes, add a stateless G3 action-lattice evaluator over copied state. G4 live adjustment is deliberately excluded from this plan and requires a new evidence-based plan after G3 acceptance.

**Tech Stack:** C++17 production runtime, fixed-capacity `std::array` data structures, CMake/MSVC Release builds, existing native telemetry JSONL pipeline, deterministic native benchmark fixtures, PowerShell comparison scripts.

---

## 1. Scope and permanent constraints

This plan implements G0 through G3 only:

```text
G0  frame-exact control/observation journal
G1  closed-loop hindsight oracle and mutation benchmark
G2  shadow causal response/delay/pending learner
G3  shadow short-horizon candidate evaluator
```

The following are hard invariants rather than tunable behavior:

- learning state is process-memory only and is empty after application restart;
- no weapon identity, weapon database, FOV, sensitivity, or ADS-multiplier input;
- no active calibration pulse and no injected test stick in live runtime;
- no additional model, inference pass, CUDA work, or TensorRT allocation;
- disabled and shadow modes cannot alter target identity, target mode, ADS epoch, AutoFire, recoil, fusion state, or final output;
- the learner estimates the local environment response; it never emits a stick, selects a target, or owns a brake;
- all online decisions use only causally available data; future data is restricted to clearly labelled hindsight evaluation;
- dynamic ROI coordinates are converted with the frame-owned viewport before learning; a zero-offset fixed ROI remains valid;
- right-stick control contribution has exactly one owner before any future G4 integration.

## 2. Explicit non-goals

- Do not replace `AimResponseEstimator` or `ControlResponseEstimator` in G2/G3.
- Do not feed a learned response into `TargetPlan` in this plan.
- Do not add a second tracker, coordinator, controller, fuser, brake, or lifecycle state machine.
- Do not expose every RLS/gate constant through TOML. Only `enabled`, `mode`, and logging controls are public in G0-G3.
- Do not implement recursive IV, EKF, ARMAX, disturbance observer, neural policy, RL, or persistent tail-value tables unless the minimum model fails a named acceptance gate and a separate design is approved.
- Do not combine this work with dynamic ROI motion. This plan only establishes the coordinate/audit contract that dynamic ROI can populate later.

## 3. File ownership map

### Existing files to extend

| File | Responsibility after this plan |
|---|---|
| `native/runtime_app/control_response_window.h/.cpp` | Single frame-exact delivered-control history and interval/window assembly owner. |
| `native/runtime_app/runtime_loop.h/.cpp` | Owns learner lifetime, records successful final delivery, submits new committed frames, publishes shadow telemetry. |
| `native/runtime_app/vision_controller_adapter.h/.cpp` | Produces frame-owned capture metadata; never substitutes result time for capture time silently. |
| `native/runtime_app/telemetry_schema.h` | Versioned G0/G2/G3 fixed-size telemetry payloads and separate quality/update outcomes. |
| `native/controller_native/runtime_config.h/.cpp` | Three-state control-learning mode with disabled default. |
| `native/controller_native/sustained_aimlab_scenario.h/.cpp` | Closed-loop plant disturbances and identifiability cohorts. |
| `native/controller_native/sustained_aimlab_counterfactual.h/.cpp` | G1 hindsight labels and G3 delayed causal regret. |
| `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp` | Versioned report, fixed seeds, mutations, paired comparison. |
| `native/vision_native/CMakeLists.txt` | Focused test and benchmark targets. |

### New focused units

| File | Single responsibility |
|---|---|
| `native/pipeline_contract/committed_capture_observation.h` | Immutable capture-time, stable-centered observation passed to learning and telemetry. |
| `native/control_learning/control_history.h` | Fixed-capacity cumulative-integral ring used by the existing response-window assembler and learner. |
| `native/control_learning/causal_response_types.h` | Units, matrices, sample-quality and update-outcome contracts. |
| `native/control_learning/robust_ew_rls.h` | Transactional fixed-matrix robust EW-RLS primitive. |
| `native/control_learning/causal_online_response_learner.h/.cpp` | Delay bank, fast/stable models, identifiability, lifecycle, confidence. |
| `native/control_learning/pending_motion_model.h/.cpp` | Reconstructs realized/scheduled control debt from history on demand. |
| `native/control_learning/short_horizon_rollout.h/.cpp` | Pure G3 candidate trajectory/cost evaluator. |
| `native/control_learning/*_tests.cpp` | Focused unit and mutation tests for each unit. |
| `native/controller_native/causal_response_synthetic_benchmark.h/.cpp` | Configurable closed-loop ground-truth plant and retained metrics. |
| `native/controller_native/cod_native_causal_response_benchmark.cpp` | Standalone fixed-seed JSON report executable. |
| `scripts/benchmarks/run-causal-response-acceptance.ps1` | Builds, runs, fingerprints, compares, and retains G1-G3 artifacts. |

## 4. Stage gates

| Gate | Must pass before | Evidence |
|---|---|---|
| G0 | G1/G2 | Frame-exact replay, provenance complete, controller added p95 `< 10 us`, p99 `< 25 us`. |
| G1 | G2 | Mutations M1-M10 detected; action lattice shows repeatable hindsight headroom on at least one relevant cohort without future leakage. |
| G2 | G3 | No non-finite state; delay/response/pending calibrated; real replay is better than fallback or honestly reports unidentifiable windows; vision update p95 `< 0.15 ms`. |
| G3 | any G4 design | Candidate ranking is deterministic, causally valid, correlated with delayed outcome, and output remains bit-stable. |

Passing synthetic tests alone never authorizes live adjustment.

### Task 1: Freeze baseline identity and add the committed capture contract

**Files:**
- Create: `native/pipeline_contract/committed_capture_observation.h`
- Create: `native/pipeline_contract/committed_capture_observation_tests.cpp`
- Modify: `native/runtime_app/vision_controller_adapter.h`
- Modify: `native/runtime_app/vision_controller_adapter.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write the failing contract tests**

Cover capture/result timestamp ordering, stable-coordinate validity, viewport/frame binding, zero-offset fixed ROI, and rejection of future viewport identity.

```cpp
TEST_CASE(committed_capture_rejects_result_before_capture) {
    auto value = valid_observation();
    value.result_at_ns = value.captured_at_ns - 1;
    REQUIRE_FALSE(pipeline_contract::valid(value));
}

TEST_CASE(committed_capture_keeps_frame_owned_viewport) {
    auto value = valid_observation();
    value.source_frame_id = 91;
    value.viewport_sequence = 7;
    value.viewport_source_frame_id = 92;
    REQUIRE_FALSE(pipeline_contract::valid(value));
}

TEST_CASE(fixed_roi_zero_offset_is_valid) {
    auto value = valid_observation();
    value.viewport_offset_px = {};
    value.viewport_sequence = 0;
    value.viewport_source_frame_id = value.source_frame_id;
    REQUIRE(pipeline_contract::valid(value));
}
```

- [ ] **Step 2: Build and prove RED**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_committed_capture_observation_tests
```

Expected: build fails because `committed_capture_observation.h` and the target do not exist.

- [ ] **Step 3: Define the immutable observation contract**

Use stable pixels and monotonic nanoseconds throughout:

```cpp
struct CommittedCaptureObservation {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t viewport_sequence = 0;
    std::uint64_t viewport_source_frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    Vec2f stable_error_px{};
    Vec2f stable_body_size_px{};
    Vec2f viewport_offset_px{};
    Vec2f target_acceleration_px_per_sec2{};
    float reliability = 0.0f;
    float normalized_size = 0.0f;
    TargetLifecycle lifecycle = TargetLifecycle::None;
    TargetMotion motion = TargetMotion::Ambiguous;
    ControlMode mode = ControlMode::Manual;
    std::uint64_t ads_epoch = 0;
    bool fresh_observed = false;
    bool strong_observation = false;
    bool stable_coordinates_valid = false;
    bool reused_or_projected = false;
};
```

`valid()` must reject non-finite values, capture time zero, result before capture, viewport/frame mismatch, invalid unit intervals, and a claimed fresh observation without a persistent target.

- [ ] **Step 4: Adapt current fixed-ROI observations without changing controller behavior**

Add a separate adapter function that creates the capture contract from the committed production identity. It must use `captured_at_ns`, preserve `result_at_ns`, set viewport offset to zero for the current fixed ROI, and never derive learning error from a tracker-projected future state.

- [ ] **Step 5: Run focused and existing adapter tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_committed_capture_observation_tests cod_native_controller_tests
ctest --test-dir native/vision_native/build -C Release -R "CommittedCapture|NativeController" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit**

```powershell
git add native/pipeline_contract/committed_capture_observation.h native/pipeline_contract/committed_capture_observation_tests.cpp native/runtime_app/vision_controller_adapter.* native/vision_native/CMakeLists.txt
git commit -m "feat: define committed capture observation contract"
```

### Task 2: Refactor the existing response window into one shared control history

**Files:**
- Create: `native/control_learning/control_history.h`
- Create: `native/control_learning/control_history_tests.cpp`
- Modify: `native/runtime_app/control_response_window.h`
- Modify: `native/runtime_app/control_response_window.cpp`
- Modify: `native/runtime_app/control_response_window_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing history tests**

Tests must cover monotonic append, overwrite order, piecewise-constant integral interpolation, failed delivery, left/right output, recoil/firing/saturation flags, and no heap allocation after construction.

```cpp
TEST_CASE(history_integrates_piecewise_constant_delivery) {
    ControlHistory<8> history;
    REQUIRE(history.push(sample(10_ms, {0.5f, 0.0f}, {0.2f, 0.0f})));
    REQUIRE(history.push(sample(30_ms, {1.0f, 0.0f}, {0.4f, 0.0f})));
    const auto interval = history.integrate(20_ms, 40_ms);
    REQUIRE_NEAR(interval.right_stick_seconds.x, 0.015f, 1.0e-6f);
    REQUIRE_NEAR(interval.left_stick_seconds.x, 0.006f, 1.0e-6f);
    REQUIRE(interval.complete);
}

TEST_CASE(history_rejects_non_monotonic_sample) {
    ControlHistory<8> history;
    REQUIRE(history.push(sample(20_ms)));
    REQUIRE_FALSE(history.push(sample(19_ms)));
}
```

- [ ] **Step 2: Build and prove RED**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_control_history_tests
```

Expected: target or header missing.

- [ ] **Step 3: Implement the fixed-capacity cumulative-integral ring**

The stored sample must describe the final delivered state, not an AI proposal:

```cpp
struct DeliveredControlSample {
    std::uint64_t sample_seq = 0;
    std::uint64_t applied_at_ns = 0;
    Vec2f physical_right{};
    Vec2f physical_left{};
    Vec2f manual_component{};
    Vec2f ai_component{};
    Vec2f pre_recoil{};
    Vec2f recoil_component{};
    Vec2f final_right{};
    Vec2f final_left{};
    std::uint64_t ads_epoch = 0;
    bool output_delivered = false;
    bool firing = false;
    bool recoil_active = false;
    bool saturated = false;
};
```

Use a `std::array<..., 1024>` ring and cumulative right/left integrals. `integrate(a,b)` is O(log N) or O(N) only in tests; production must use two interpolated cumulative samples and remain O(1) per delay query.

- [ ] **Step 4: Make `ControlResponseWindowAssembler` consume this history**

Remove its private reset-on-frame `commands_` array. Its `observe_controller()` writes exactly once to `ControlHistory`; its `observe_vision()` queries the shared history for the anchor/frame capture interval. Preserve every existing `ControlResponseWindow` field and readiness result.

- [ ] **Step 5: Record at the actual delivery boundary**

In `RuntimeLoop::run_once()`, timestamp after `virtual_gamepad_.update(output)` returns. Record failed delivery as diagnostic with `output_delivered=false`; any interval containing it is ineligible. When `config_.output.enabled == false`, mark the simulated output delivered but include output-disabled provenance.

- [ ] **Step 6: Prove existing response-window semantics are unchanged**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_control_history_tests cod_native_control_response_window_tests
ctest --test-dir native/vision_native/build -C Release -R "ControlHistory|ControlResponseWindow" --output-on-failure
```

Expected: all selected tests pass and the old window fixtures retain identical numeric integrals.

- [ ] **Step 7: Commit**

```powershell
git add native/control_learning/control_history* native/runtime_app/control_response_window* native/runtime_app/runtime_loop.cpp native/vision_native/CMakeLists.txt
git commit -m "refactor: share frame-exact delivered control history"
```

### Task 3: Add G0 causal journal fields and provenance

**Files:**
- Modify: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`
- Modify: `native/runtime_app/telemetry_collectors.h`
- Modify: `native/runtime_app/telemetry_collectors.cpp`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Create: `docs/benchmarks/native-runtime-telemetry.md`

- [ ] **Step 1: Write failing JSONL round-trip tests**

The record must include controller applied time, final right/left, committed capture identity, capture/result timestamps, viewport sequence/offset, ADS epoch, mode, recoil/fire/delivery flags, and provenance.

```cpp
REQUIRE(json["schema"] == "causal_response_journal_v1");
REQUIRE(json["observation"]["captured_at_ns"] == 900u);
REQUIRE(json["observation"]["result_at_ns"] == 960u);
REQUIRE(json["control"]["applied_at_ns"] == 975u);
REQUIRE(json["control"]["final_left"][0] == Approx(0.25f));
REQUIRE(json["provenance"]["config_sha256"].size() == 64u);
```

- [ ] **Step 2: Run the serializer tests and prove RED**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime_telemetry_tests
ctest --test-dir native/vision_native/build -C Release -R RuntimeTelemetry --output-on-failure
```

Expected: missing v1 fields/assertions fail.

- [ ] **Step 3: Add fixed-size G0 payloads without a synchronous writer**

Reuse the existing async telemetry queue. Do not introduce another logging thread. Add record types for `CommittedCaptureObservation`, `DeliveredControlSample`, and later `CausalResponseShadow`; each payload must fit the existing fixed queue record.

- [ ] **Step 4: Separate quality from learning outcome now**

Define two telemetry fields:

```cpp
enum class VisionSampleQuality : std::uint8_t { Normal, SoftWeight, HardReject };
enum class IdentificationUpdateOutcome : std::uint8_t {
    NotEvaluated,
    AcceptedByAtLeastOneDelay,
    InsufficientExcitation,
    DeliveryGap,
    FiringOrRecoil,
    Saturated,
    TimingInvalid,
    CoordinateInvalid,
    IdentityBoundary,
    NoUsableDelay,
};
```

Never increment a `normal` learning counter merely because the source frame was visually normal.

- [ ] **Step 5: Document debug enablement and cleanup**

G0 records are emitted only when the existing runtime telemetry is enabled. Extend session manifests and existing whole-session cleanup; do not create free-floating log files.

- [ ] **Step 6: Run serializer, queue, and session-manager tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime_telemetry_tests
ctest --test-dir native/vision_native/build -C Release -R "Telemetry|LogSession" --output-on-failure
```

- [ ] **Step 7: Commit**

```powershell
git add native/runtime_app/telemetry_* native/runtime_app/runtime_loop.cpp docs/benchmarks/native-runtime-telemetry.md
git commit -m "feat: journal causal response evidence"
```

### Task 4: Build the G1 closed-loop synthetic plant before the learner

**Files:**
- Create: `native/controller_native/causal_response_synthetic_benchmark.h`
- Create: `native/controller_native/causal_response_synthetic_benchmark.cpp`
- Create: `native/controller_native/causal_response_synthetic_benchmark_tests.cpp`
- Create: `native/controller_native/cod_native_causal_response_benchmark.cpp`
- Modify: `native/controller_native/sustained_aimlab_scenario.h`
- Modify: `native/controller_native/sustained_aimlab_scenario.cpp`
- Modify: `native/controller_native/sustained_aimlab_counterfactual.h`
- Modify: `native/controller_native/sustained_aimlab_counterfactual.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing ground-truth plant tests**

The plant must accept actual error-feedback controller outputs rather than a deterministic open-loop sine sequence. Cover anisotropic/cross-coupled right response, target-local left response, latency, slowdown, reversal/jump/fall, target switch, ROI motion, dropped/reused frame, firing/recoil, delivery gap, saturation, and classic wrong/delayed/stale/corrective manual inputs.

```cpp
TEST_CASE(closed_loop_plant_applies_delayed_right_and_left_response) {
    PlantConfig cfg;
    cfg.right_response = {{{900.0f, 40.0f}, {-20.0f, 760.0f}}};
    cfg.left_response = {{{-180.0f, 0.0f}, {0.0f, -80.0f}}};
    cfg.delay_ms = 45.0f;
    auto trace = run_feedback_fixture(cfg, 1337);
    REQUIRE(trace.controls_depend_on_prior_error);
    REQUIRE(trace.ground_truth_delay_ms == Approx(45.0f));
}
```

- [ ] **Step 2: Build and prove RED**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_causal_response_benchmark_tests
```

Expected: missing plant/target.

- [ ] **Step 3: Implement versioned deterministic fixtures**

Retain these required cohorts under fixed seeds `1337, 7331, 20260722`:

```text
ordinary_feedback
low_excitation
right_left_collinear
target_reversal_jump_fall
target_identity_switch
half_occluded_xy_motion
roi_offset_motion
dropout_reused_result_jitter
firing_recoil_saturation_delivery_gap
slowdown_nonlinearity
response_change_point
mixed_classic_human_errors
double_compensation
```

- [ ] **Step 4: Add G1 hindsight labels and headroom**

For every 40/80/160/250 ms decision window, calculate actual future cumulative error, terminal error, reverse burden, jerk, user-fight, interruption, switch/handoff burden, and the best hindsight candidate from `{release, 0.70, 0.85, 1.00} * base`. Label output `hindsight_only=true`.

- [ ] **Step 5: Add anti-cheating mutations M1-M10**

Each mutation must flip a named assertion rather than merely change a score:

```text
M1 result_at replaces captured_at
M2 ROI-local coordinates leak
M3 maneuver gate disabled
M4 low excitation grows confidence
M5 future frame used causally
M6 pending accumulated instead of reconstructed
M7 disabled mode changes output
M8 tracker/controller double compensation
M9 delay confidence without separation
M10 firing/recoil learned as response
```

- [ ] **Step 6: Require meaningful, not trivial, maneuver contamination evidence**

The maneuver-gate mutation fails only if parameter/prediction error worsens beyond deterministic noise; a test such as `12.925% < 13.167%` is insufficient. Require both an absolute degradation of at least `1.0 percentage point` and a relative degradation of at least `5%` in the maneuver cohort, or explicitly fail the fixture as non-discriminating.

- [ ] **Step 7: Run G1 tests twice for determinism**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_causal_response_benchmark_tests cod_native_causal_response_benchmark
& native/vision_native/build/Release/cod_native_causal_response_benchmark.exe --seed 1337 --output artifacts/benchmarks/causal-response/g1-a.json
& native/vision_native/build/Release/cod_native_causal_response_benchmark.exe --seed 1337 --output artifacts/benchmarks/causal-response/g1-b.json
Compare-Object (Get-Content -Raw artifacts/benchmarks/causal-response/g1-a.json) (Get-Content -Raw artifacts/benchmarks/causal-response/g1-b.json)
```

Expected: no difference after excluding wall-clock runtime fields; all M1-M10 mutations are detected.

- [ ] **Step 8: Commit**

```powershell
git add native/controller_native/causal_response_synthetic_benchmark* native/controller_native/cod_native_causal_response_benchmark.cpp native/controller_native/sustained_aimlab_* native/vision_native/CMakeLists.txt
git commit -m "bench: add closed-loop causal response oracle"
```

### Task 5: Implement transactional robust EW-RLS primitives

**Files:**
- Create: `native/control_learning/causal_response_types.h`
- Create: `native/control_learning/robust_ew_rls.h`
- Create: `native/control_learning/robust_ew_rls_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing numerical safety tests**

Cover finite update, Huber down-weighting, low excitation rejection, rank/condition reporting, and complete rollback of theta, covariance, information matrix, residual scale, counters, and confidence.

```cpp
const auto before = rls.snapshot();
REQUIRE_FALSE(rls.update(non_finite_sample()));
REQUIRE(rls.snapshot() == before);
```

- [ ] **Step 2: Build and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_robust_ew_rls_tests
```

- [ ] **Step 3: Implement fixed-size transactional updates**

Compute into a local candidate state, validate every scalar/matrix, then assign once:

```cpp
State candidate = state_;
apply_forgetting(candidate, lambda);
apply_information(candidate, phi, weight);
apply_theta(candidate, phi, y, weight);
update_residual_scale(candidate, residual);
if (!finite(candidate) || !positive_definite(candidate.covariance)) return false;
state_ = candidate;
return true;
```

No heap allocation, exceptions, or dynamic matrix library is permitted.

- [ ] **Step 4: Run focused tests under Release and Debug**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_robust_ew_rls_tests
cmake --build native/vision_native/build --config Debug --target cod_native_robust_ew_rls_tests
ctest --test-dir native/vision_native/build -C Release -R RobustEwRls --output-on-failure
ctest --test-dir native/vision_native/build -C Debug -R RobustEwRls --output-on-failure
```

- [ ] **Step 5: Commit**

```powershell
git add native/control_learning/causal_response_types.h native/control_learning/robust_ew_rls* native/vision_native/CMakeLists.txt
git commit -m "feat: add transactional robust response estimator"
```

### Task 6: Implement the corrected G2 delay-bank learner

**Files:**
- Create: `native/control_learning/causal_online_response_learner.h`
- Create: `native/control_learning/causal_online_response_learner.cpp`
- Create: `native/control_learning/causal_online_response_learner_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing lifecycle/classification/delay tests**

Required tests:

```text
hard reject clears interval pairing for every hard-reject reason
stable right prior survives target identity change with confidence decay
left model resets on target identity change
low excitation remains visually normal but identification outcome is InsufficientExcitation
firing/recoil/saturation/delivery gap set identification outcome to hard rejection
accepted_by_any_delay is false when all delay candidates reject
selected delay confidence refers to selected delay
pending delay switch reports best, selected, and switch_pending separately
long fresh-frame gap clears pairing without deleting stable right prior
new ADS epoch clears fast state and pending reference
```

- [ ] **Step 2: Build and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_causal_online_response_learner_tests
```

- [ ] **Step 3: Implement the minimum delay bank**

Use delays `10..100 ms` in `5 ms` increments, with fixed arrays. Regress differenced stable target error against integrated delivered right and left stick. Maintain fast and stable states per delay. Do not interpret sample count as confidence.

- [ ] **Step 4: Implement three-layer sample reporting**

Every `observe_vision()` returns:

```cpp
struct SampleAssessment {
    VisionSampleQuality vision_quality;
    IdentificationUpdateOutcome update_outcome;
    std::uint32_t reason_bits;
    bool accepted_by_any_delay;
    std::uint8_t accepted_delay_count;
};
```

Any hard reject calls `clear_interval_pairing()` after recording the reason. It retains only those priors whose lifecycle contract permits retention.

- [ ] **Step 5: Use separate right and left lifecycle rules**

Right response is a slowly decayed session prior because it represents local camera response. Left response is target-local because its screen motion depends on target depth/geometry; reset its fast and stable state on target identity change. Record normalized target size for future conditioning, but do not create size buckets in this plan.

- [ ] **Step 6: Make delay confidence selected-specific**

Expose:

```cpp
float best_delay_ms;
float selected_delay_ms;
float selected_delay_confidence;
bool delay_switch_pending;
```

During dwell, confidence must be computed for the selected delay or suppressed; it must never describe a different best candidate than the response matrix being output.

- [ ] **Step 7: Add identifiability and confidence gates**

Confidence requires excitation magnitude, right/left/joint rank, delay separation, stable residual, and accepted evidence time. When inputs are collinear, lower joint confidence and freeze the unidentifiable contribution instead of emitting a smooth pseudo-precise matrix.

- [ ] **Step 8: Run focused tests and the G1 benchmark with learner enabled**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_causal_online_response_learner_tests cod_native_causal_response_benchmark
ctest --test-dir native/vision_native/build -C Release -R CausalOnlineResponseLearner --output-on-failure
& native/vision_native/build/Release/cod_native_causal_response_benchmark.exe --learner shadow --seed 1337 --output artifacts/benchmarks/causal-response/g2-core.json
```

Expected: low-excitation cohort reports zero accepted updates and zero confidence without falsely reporting identification-normal samples.

- [ ] **Step 9: Commit**

```powershell
git add native/control_learning/causal_online_response_learner* native/vision_native/CMakeLists.txt artifacts/benchmarks/causal-response/g2-core.json
git commit -m "feat: estimate causal response and delay in shadow"
```

### Task 7: Reconstruct pending motion from history

**Files:**
- Create: `native/control_learning/pending_motion_model.h`
- Create: `native/control_learning/pending_motion_model.cpp`
- Create: `native/control_learning/pending_motion_model_tests.cpp`
- Modify: `native/control_learning/causal_online_response_learner.h`
- Modify: `native/control_learning/causal_online_response_learner.cpp`
- Modify: `native/controller_native/causal_response_synthetic_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing pending-motion tests**

Cover exact fixed-delay reconstruction, fractional boundary interpolation, capture gaps, delivery gap, ADS reset, identity reset, delay switch, and no accumulator drift.

```cpp
const auto pending = model.estimate(capture_ns, now_ns, history, response);
REQUIRE_NEAR(pending.realized_px.x, expected_realized_x, 1.0e-4f);
REQUIRE_NEAR(pending.scheduled_px.x, expected_scheduled_x, 1.0e-4f);
REQUIRE_NEAR(pending.total_px.x,
             pending.realized_px.x + pending.scheduled_px.x,
             1.0e-6f);
```

- [ ] **Step 2: Build and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_pending_motion_model_tests
```

- [ ] **Step 3: Implement reconstruction, not accumulation**

For current capture `t_n` and selected delay `d`:

```text
realized input interval   = [previous_capture - d, current_capture - d]
scheduled input interval  = [current_capture - d, current_capture]
pending motion            = R * right_integral + L * left_integral
```

Recompute from history on every estimate. Never mutate pending by adding a new command and subtracting an observed delta.

- [ ] **Step 4: Attach calibrated confidence and bounds**

Pending confidence is bounded by selected-delay confidence, response confidence, history completeness, coordinate validity, and identity continuity. Incomplete history returns invalid pending with zero confidence; it does not extrapolate silently.

- [ ] **Step 5: Run focused and mutation tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_pending_motion_model_tests cod_native_causal_response_benchmark
ctest --test-dir native/vision_native/build -C Release -R PendingMotion --output-on-failure
& native/vision_native/build/Release/cod_native_causal_response_benchmark.exe --mutation pending_accumulator --expect-detected
```

- [ ] **Step 6: Commit**

```powershell
git add native/control_learning/pending_motion_model* native/control_learning/causal_online_response_learner* native/controller_native/causal_response_synthetic_benchmark.cpp native/vision_native/CMakeLists.txt
git commit -m "feat: reconstruct pending control motion"
```

### Task 8: Integrate G2 into runtime as disabled-by-default shadow only

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`
- Modify: `config.native.example.toml`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing config and bit-stability tests**

```cpp
REQUIRE(load_minimal_config().control_learning.mode == ControlLearningMode::Disabled);
REQUIRE(run_ticks(ControlLearningMode::Disabled).outputs == baseline.outputs);
REQUIRE(run_ticks(ControlLearningMode::Shadow).outputs == baseline.outputs);
REQUIRE(run_ticks(ControlLearningMode::Shadow).target_plans == baseline.target_plans);
```

- [ ] **Step 2: Build and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime_config_tests cod_native_runtime_telemetry_tests
```

- [ ] **Step 3: Add only the public stage controls**

```cpp
enum class ControlLearningMode { Disabled, Shadow, RolloutShadow };

struct ControlLearningConfig {
    bool enabled = false;
    ControlLearningMode mode = ControlLearningMode::Disabled;
    bool telemetry_enabled = false;
};
```

Unknown modes fail configuration loading. `enabled=false` forces Disabled regardless of `mode`. Delay/RLS constants remain versioned internal defaults.

- [ ] **Step 4: Wire causal ordering in `RuntimeLoop`**

Required order:

```text
consume fresh committed capture
build current controller output without learner input
attempt ViGEm delivery
record successful final delivered output at completion timestamp
submit only newly committed capture observation to learner
compute shadow estimate for telemetry
make estimate visible no earlier than the next tick
```

Do not call learner methods when Disabled. Shadow may update learner state, but controller receives no pointer/reference to its estimate.

- [ ] **Step 5: Emit side-by-side estimator evidence**

Telemetry must retain current `AimResponseEstimator`, current left `ControlResponseEstimator`, and new learner values so replay can compare them. Include best/selected delay, switch-pending, right/left/joint confidence, excitation, residual, pending realized/scheduled, sample quality, update outcome, and reason bits.

- [ ] **Step 6: Prove output and owner invariants**

Hash every delivered output, TargetPlan, AutoFire decision, recoil component, and fuser decision for an identical replay in Disabled and Shadow. The hashes must match exactly.

- [ ] **Step 7: Run focused and controller tests**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_runtime_config_tests cod_native_runtime_telemetry_tests cod_native_controller_tests
ctest --test-dir native/vision_native/build -C Release -R "RuntimeConfig|RuntimeTelemetry|NativeController" --output-on-failure
```

- [ ] **Step 8: Commit**

```powershell
git add native/controller_native/runtime_config* native/runtime_app/runtime_loop* native/runtime_app/runtime_telemetry* native/runtime_app/telemetry_schema.h config.native.example.toml native/vision_native/CMakeLists.txt
git commit -m "feat: run causal response learner in shadow"
```

### Task 9: Add real-log causal replay and G2 acceptance automation

**Files:**
- Create: `native/controller_native/causal_response_log_replay.h`
- Create: `native/controller_native/causal_response_log_replay.cpp`
- Create: `native/controller_native/causal_response_log_replay_tests.cpp`
- Create: `native/controller_native/cod_native_causal_response_replay.cpp`
- Create: `scripts/benchmarks/run-causal-response-acceptance.ps1`
- Create: `docs/benchmarks/causal-response-shadow.md`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing causal replay tests**

Tests must reject missing revision/config/engine/schema/crop identity, out-of-order timestamps, future-frame access, and viewport mismatch. They must retain rejected-window ratios and per-reason counts.

- [ ] **Step 2: Build and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_causal_response_replay_tests
```

- [ ] **Step 3: Implement streaming replay**

Merge controller and frame records by monotonic event time. At time `t`, the learner may receive only controls already delivered and observations already committed. Hindsight outcomes are attached later through a separate evaluator and serialized under `hindsight`, never under `causal`.

- [ ] **Step 4: Compare three identities**

Every artifact compares:

```text
fixed fallback response/delay
current AimResponseEstimator + left ControlResponseEstimator
new causal learner
```

Report response residual, delay separation, pending error, unidentifiable-window ratio, accepted/rejected counts, confidence calibration, change-detection delay, false resets, and worst episodes.

- [ ] **Step 5: Add Windows Release microbenchmarks**

Measure separately:

```text
controller history push p50/p95/p99
fresh vision observe+estimate p50/p95/p99
telemetry enqueue p50/p95/p99
```

G2 fails if added controller p95 is `>= 10 us`, p99 is `>= 25 us`, or fresh-vision observe+estimate p95 is `>= 0.15 ms`.

- [ ] **Step 6: Create the acceptance script**

The script must build focused targets, run fixed seeds, run M1-M10, replay every explicitly supplied real session, fingerprint revision/config/engine/schema/crop, and write one whole-session directory under `artifacts/benchmarks/causal-response/<timestamp>-<revision>/`.

- [ ] **Step 7: Run acceptance once without claiming G2 success prematurely**

```powershell
& scripts/benchmarks/run-causal-response-acceptance.ps1 -BuildDir native/vision_native/build -Seeds 1337,7331,20260722
```

Expected: synthetic and mutation gates pass. If no valid real log is supplied, artifact status is `synthetic_pass_real_replay_pending`, not `accepted`.

- [ ] **Step 8: Commit**

```powershell
git add native/controller_native/causal_response_log_replay* native/controller_native/cod_native_causal_response_replay.cpp scripts/benchmarks/run-causal-response-acceptance.ps1 docs/benchmarks/causal-response-shadow.md native/vision_native/CMakeLists.txt artifacts/benchmarks/causal-response
git commit -m "bench: validate causal learner against replay"
```

### Task 10: Audit and freeze the no-double-compensation ownership boundary

**Files:**
- Create: `docs/project/CAUSAL_RESPONSE_EGO_MOTION_OWNERSHIP_AUDIT.md`
- Modify: `native/controller_native/causal_response_synthetic_benchmark_tests.cpp`
- Modify: `native/controller_native/sustained_aimlab_counterfactual_tests.cpp`

- [ ] **Step 1: Trace current production ownership with source evidence**

Document where `camera_attributed_velocity`, output components, tracker ego projection, TargetPlan response hints, predicted terminal error, and BodyLock inertia enter the current chain. Record exact symbol/file references and whether each value contains realized control motion, scheduled control debt, target inertia, or a mixture.

- [ ] **Step 2: Choose one future G4 boundary based on evidence**

The audit must select exactly one:

```text
A: learner parameterizes tracker ego projection; rollout uses scheduled debt only
B: tracker excludes control pending; controller rollout owns realized + scheduled debt
```

If current code cannot prove either boundary, keep G3 shadow evaluation diagnostic and mark G4 blocked. Do not infer ownership from variable names.

- [ ] **Step 3: Add double-compensation mutation assertions**

The closed-loop plant must show that applying tracker ego projection and pending-realized correction twice increases terminal/post-cross burden. This makes future accidental A+B integration fail deterministically.

- [ ] **Step 4: Review the audit before G3 implementation**

No G3 rollout implementation proceeds until the audit names the immutable state snapshot and the exact pending components it may read.

- [ ] **Step 5: Commit**

```powershell
git add docs/project/CAUSAL_RESPONSE_EGO_MOTION_OWNERSHIP_AUDIT.md native/controller_native/causal_response_synthetic_benchmark_tests.cpp native/controller_native/sustained_aimlab_counterfactual_tests.cpp
git commit -m "docs: freeze causal ego-motion ownership"
```

### Task 11: Implement the pure G3 short-horizon shadow evaluator

**Files:**
- Create: `native/control_learning/short_horizon_rollout.h`
- Create: `native/control_learning/short_horizon_rollout.cpp`
- Create: `native/control_learning/short_horizon_rollout_tests.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/controller_native/sustained_aimlab_counterfactual.cpp`
- Modify: `native/controller_native/cod_native_causal_response_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing purity and causality tests**

```cpp
const auto fuser_before = fuser.snapshot();
const auto controller_before = controller.snapshot();
const auto result = rollout.evaluate(snapshot, estimate, candidates);
REQUIRE(fuser.snapshot() == fuser_before);
REQUIRE(controller.snapshot() == controller_before);
REQUIRE(result.used_latest_timestamp_ns <= snapshot.decision_at_ns);
```

Also cover low-confidence fallback, no-target, identity transition, maneuver, useful crossing, harmful overshoot, manual escape, and deterministic tie-breaking.

- [ ] **Step 2: Build and prove RED**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_short_horizon_rollout_tests
```

- [ ] **Step 3: Define an immutable evaluation snapshot**

The snapshot contains only current `TargetPlan`, current shaped AI proposal, manual/fusion summary, selected response/delay/pending estimate, and the ownership-audit-approved pending components. It contains no pointer to `TargetCoordinator`, `AimDynamicsShaper`, `VectorIntentFuser`, AutoFire, recoil, or mutable controller state.

- [ ] **Step 4: Evaluate a small fixed action lattice**

```cpp
constexpr std::array<float, 4> kScales{0.0f, 0.70f, 0.85f, 1.00f};
```

Use 80-150 ms ADS and 120-250 ms BodyLock horizons, capped at eight simulated vision ticks. Cost terms are cumulative error, terminal error, pending burden, reverse correction, jerk, user-fight, incorrect interruption, and handoff residual. Keep ADS and BodyLock weights separate.

- [ ] **Step 5: Model useful crossing separately from harmful overshoot**

Do not penalize every center crossing. Combine radial closing velocity, target velocity/acceleration, reversal evidence, pending motion, future cumulative error, and terminal residual. A crossing with continuing target inertia may be beneficial; a crossing after target deceleration/reversal with large same-direction pending debt is harmful.

- [ ] **Step 6: Integrate RolloutShadow without output access**

When `mode=RolloutShadow`, write candidate costs, best causal label, confidence/gate reason, and the actual unchanged output. Later committed frames attach 40/80/160/250 ms outcomes to the decision ring. The evaluator result is not passed to controller code.

- [ ] **Step 7: Measure causal ranking, not oracle headroom**

Report top-1 candidate agreement, pairwise ranking correlation, predicted-vs-actual regret, harmful-release rate, candidate oscillation, and per-cohort worst episodes. Keep hindsight headroom in a separate JSON object.

- [ ] **Step 8: Run G3 acceptance**

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_short_horizon_rollout_tests cod_native_causal_response_benchmark cod_native_controller_tests
ctest --test-dir native/vision_native/build -C Release -R "ShortHorizonRollout|NativeController" --output-on-failure
& scripts/benchmarks/run-causal-response-acceptance.ps1 -BuildDir native/vision_native/build -Mode RolloutShadow -Seeds 1337,7331,20260722
```

G3 passes only if ranking is reproducible and positively correlated with delayed outcomes in both ordinary and mixed-error cohorts, harmful-release rate does not increase, and Disabled/Shadow/RolloutShadow output hashes are identical.

- [ ] **Step 9: Commit**

```powershell
git add native/control_learning/short_horizon_rollout* native/runtime_app/runtime_loop* native/runtime_app/telemetry_* native/controller_native/sustained_aimlab_counterfactual.cpp native/controller_native/cod_native_causal_response_benchmark.cpp native/vision_native/CMakeLists.txt artifacts/benchmarks/causal-response
git commit -m "feat: evaluate short-horizon actions in shadow"
```

### Task 12: Final verification, acceptance report, and G4 decision boundary

**Files:**
- Create: `docs/project/CAUSAL_RESPONSE_SHADOW_ACCEPTANCE_20260722.md`
- Modify: `docs/benchmarks/causal-response-shadow.md`
- Modify: `.agent-context/` only after an approved SyncSet

- [ ] **Step 1: Run the complete fixed-seed suite twice**

```powershell
& scripts/benchmarks/run-causal-response-acceptance.ps1 -BuildDir native/vision_native/build -Mode RolloutShadow -Seeds 1337,7331,20260722
& scripts/benchmarks/run-causal-response-acceptance.ps1 -BuildDir native/vision_native/build -Mode RolloutShadow -Seeds 1337,7331,20260722
```

Require identical decisions and metrics except measured CPU duration fields.

- [ ] **Step 2: Run full native regression tests**

```powershell
cmake --build native/vision_native/build --config Release
ctest --test-dir native/vision_native/build -C Release --output-on-failure
```

Expected: zero failures. Any unrelated pre-existing failure must be identified by name and reproduced at the plan baseline before proceeding.

- [ ] **Step 3: Verify runtime invariants**

Check:

```text
default config is Disabled
Disabled allocates no learner and makes no learner calls
Shadow and RolloutShadow do not change output hashes
application restart clears all learner state
no weapon/FOV/sensitivity fields exist in learner schema
no new vision inference or GPU allocation exists
no non-finite state appears in synthetic or replay
AutoFire and recoil decisions remain byte-identical
```

- [ ] **Step 4: Write the acceptance report with honest status**

Record revision, dirty state, compiler/build family, config hash, engine hash, crop, telemetry schema, seeds, artifact paths, per-gate results, rejected variants, real-log coverage, CPU p50/p95/p99, unidentifiable ratio, and remaining risks.

Allowed final statuses:

```text
G2_REJECTED
G2_ACCEPTED_G3_PENDING
G3_REJECTED
G3_SHADOW_ACCEPTED_G4_NOT_AUTHORIZED
```

- [ ] **Step 5: Keep G4 outside this implementation plan**

Only if the status is `G3_SHADOW_ACCEPTED_G4_NOT_AUTHORIZED`, write a separate G4 design. Its initial action bound may be `[0.80, 1.00] * base` with no free reverse, but the exact bound must come from retained replay/live evidence. G4 must insert at one audited owner boundary and include instant configuration rollback.

- [ ] **Step 6: Propose Agent Context Sync**

Present a SyncSet covering handoff, session log, a decision record for ego-motion ownership, inferred items, sensitive exclusions, and reviewer findings. Do not modify `.agent-context/` until the user approves that SyncSet.

- [ ] **Step 7: Commit**

```powershell
git add docs/project/CAUSAL_RESPONSE_SHADOW_ACCEPTANCE_20260722.md docs/benchmarks/causal-response-shadow.md
git commit -m "docs: record causal response shadow acceptance"
```

## 5. Recommended execution batches

Use these review checkpoints even when executing inline:

1. **Batch A — evidence foundation:** Tasks 1-4. Stop if G0 provenance/replay or G1 mutations are weak.
2. **Batch B — learner core:** Tasks 5-7. Stop if response/delay/pending cannot be calibrated in closed loop.
3. **Batch C — runtime shadow:** Tasks 8-9. Stop if output is not bit-stable, CPU budget fails, or real logs are mostly unidentifiable.
4. **Batch D — planning evidence:** Tasks 10-12. Stop if ownership is ambiguous or causal ranking does not predict delayed outcomes.

Each batch leaves the current application usable. Tasks 1-7 do not require enabling runtime learning. Task 8 defaults Disabled. Tasks 9-12 add evidence and shadow computation only.

## 6. Why this order is required

- The current repository already has delivered-output timestamps and response-window infrastructure, so G0 extends it instead of creating a parallel recorder.
- A closed-loop benchmark comes before the learner because the research package's retained synthetic fixture is too idealized and can hide gain-delay confounding.
- The four reference P1 defects are converted into direct tests before porting math: hard-reject interval clearing, separate sample/update semantics, target-local left response, and selected-delay confidence.
- Pending is reconstructed from immutable history so it cannot drift or become a hidden shared brake.
- The ego-motion ownership audit precedes rollout so G3 cannot accidentally compensate motion already projected by tracker.
- G4 is excluded because a shadow improvement is evidence of headroom, not authorization to change live aim behavior.

## 7. Plan self-review result

- **Spec coverage:** Covers all permanent constraints, G0-G3, response/delay/pending, left-stick intent, dynamic ROI coordinate audit, closed-loop synthetic benchmark, real replay, mutations, CPU gates, and the future G4 boundary.
- **Placeholders:** No TBD/TODO or unnamed error-handling steps remain.
- **Type consistency:** `CommittedCaptureObservation`, `DeliveredControlSample`, sample-quality/update-outcome, learner estimate, and rollout snapshot have one owner and consistent naming across tasks.
- **Scope:** G0-G3 is one sequential workstream. G4 live adjustment and G5 tail-value learning are intentionally separate future designs.
