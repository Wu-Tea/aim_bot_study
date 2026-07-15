# BodyLock Relative-Motion Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make sustained BodyLock account for player left-stick movement, consume each vision observation exactly once, coast/release/reacquire smoothly, and hand ADS acquisition into BodyLock without transition overshoot.

**Architecture:** Refactor the existing `BodyLockMotionPolicy` into a fresh-observation relative-motion estimator, keeping it estimate-only. `NativeAiAim` remains the single BodyLock desired-assist planner and explicitly separates position feedback from trusted motion feed-forward; `NativeAimAssistDynamics` becomes the single delivered-assist envelope, including ADS handoff seeding and lifecycle release. ADS-only brakes remain isolated and recoil remains the final independent output component.

**Tech Stack:** C++17, native gamepad controller/tracker pipeline, CMake with Visual Studio 2022, deterministic native controller benchmarks and unit executables.

---

## File Responsibility Map

- `native/controller_native/bodylock_policy.h/.cpp`: estimate fresh-frame body-relative velocity, left-stick mobility gain/confidence, projected lead, and diagnostic state; never mutate stick output.
- `native/controller_native/ai_aim.h/.cpp`: compute one BodyLock desired-assist vector from position feedback plus motion feed-forward; own continuous near-target terminal cap and unified manual priority.
- `native/controller_native/aim_assist_dynamics.h/.cpp`: own delivered BodyLock AI continuity, ADS handoff seed, coast, release, reacquire, step and jerk limits.
- `native/controller_native/bodylock_lifecycle.h/.cpp`: emit lifecycle state/reason and targeted reset intent; do no stick math.
- `native/controller_native/ads_completion_gate.h/.cpp`: require fresh terminal-approach evidence as well as centered frames before completing ADS acquisition.
- `native/controller_native/native_gamepad_controller.h/.cpp`: pass left intent/fresh identity data, keep ADS and BodyLock authority separate, record the last post-brake pre-recoil ADS AI seed, and preserve stage ordering.
- `native/controller_native/runtime_config.h/.cpp`: retain legacy BodyLock key compatibility and add no more than the three approved lifecycle timing fields if code-level constants cannot meet tests.
- `native/controller_native/controller_behavior_tests.cpp`: integrated RED/GREEN ownership, handoff, manual, recoil, and smoothness tests.
- `native/controller_native/bodylock_lifecycle_tests.cpp`: same-track continuity and explicit release semantics.
- `native/controller_native/ads_completion_gate_tests.cpp`: centered-but-closing RED/GREEN terminal eligibility.
- `native/controller_native/left_stick_motion_defect_benchmark.h/.cpp`: 1000 Hz controller and 50/80/100/160 Hz vision matrix, adaptive-motion and lifecycle/handoff scoring.
- `native/controller_native/left_stick_motion_defect_benchmark_tests.cpp`: benchmark contract, determinism, and desired-gate tests.
- `native/vision_native/CMakeLists.txt`: include the refactored policy sources in all affected test/benchmark targets; add no duplicate runtime implementation.

Initialize these variables once. Run build/test commands from
`$repo/native/vision_native`; run Git commands with `git -C $repo`:

```powershell
$repo = (git rev-parse --show-toplevel)
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
Set-Location (Join-Path $repo 'native/vision_native')
```

## Task 1: Require Terminal Evidence Before ADS Completion

**Files:**
- Modify: `native/controller_native/ads_completion_gate.h`
- Modify: `native/controller_native/ads_completion_gate.cpp`
- Modify: `native/controller_native/ads_completion_gate_tests.cpp`

- [ ] **Step 1: Write a failing ADS completion test for AI-driven closing velocity**

Extend `AdsCompletionGateInput` with the exact fields shown below and add:

```cpp
void test_centered_frames_do_not_complete_while_ai_is_closing_fast() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    auto input = sample(1, 0.000, 7.0f, 0.0f);
    input.terminal_approach_valid = true;
    input.closing_speed_px_per_sec = 420.0f;
    input.position_closing_assist = 0.36f;
    require(gate.update(input).active, "first centered frame starts acquisition");
    input.vision_sequence = 2;
    input.now_seconds = 0.012;
    require(gate.update(input).centered_fresh_frames == 0,
            "fast AI closing must not count as settled completion evidence");
}
```

- [ ] **Step 2: Build and verify the ADS test is RED for the missing fields/behavior**

Run:

```powershell
& $cmake --build --preset modern-release --target cod_native_ads_completion_gate_tests -- /m
```

Expected: compilation fails because terminal-approach input is not defined, or the new assertion fails because centered frames still count while closing fast.

- [ ] **Step 3: Implement the minimal terminal eligibility gate**

Add to `AdsCompletionGateInput`:

```cpp
bool terminal_approach_valid = true;
float closing_speed_px_per_sec = 0.0f;
float position_closing_assist = 0.0f;
```

A distinct centered frame counts only when terminal evidence is valid and the
50 ms projected crossing plus position-closing assist remain inside the 2 px
budget. Update the existing test helper to mark ordinary settled samples valid
with zero closing values. The compatibility default remains safe/settled until
Task 4 wires explicit controller evidence; Task 4 then assigns every field from
the prior completed pre-recoil tick and fresh target motion.

- [ ] **Step 4: Run the focused gate test GREEN**

Run:

```powershell
& $cmake --build --preset modern-release --target cod_native_ads_completion_gate_tests -- /m
& 'build-modern/Release/cod_native_ads_completion_gate_tests.exe'
```

Expected: all ADS completion tests pass, including centered-but-closing rejection.

- [ ] **Step 5: Commit the terminal gate**

```powershell
git -C $repo add native/controller_native/ads_completion_gate.h native/controller_native/ads_completion_gate.cpp native/controller_native/ads_completion_gate_tests.cpp
git -C $repo commit -m "fix: require terminal evidence before ads completion"
```

## Task 2: Make BodyLock Motion Observation Fresh-Frame Correct

**Files:**
- Modify: `native/controller_native/bodylock_policy.h`
- Modify: `native/controller_native/bodylock_policy.cpp`
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Add a failing duplicate-sequence motion test**

Drive `NativeAiAim` at 1000 Hz with one fresh box every 10 ms and nine duplicate ticks between frames. Assert these diagnostics:

```cpp
require_true(ai.bodylock_motion_samples() == fresh_sequence_count,
    "one vision sequence must produce one motion update");
require_true(ai.bodylock_duplicate_samples() == duplicate_tick_count,
    "duplicate controller ticks must be ignored, not measured as zero velocity");
require_true(max_abs_velocity_px_per_sec < 1200.0f,
    "fresh-frame velocity must not divide a 10 ms displacement by a 1 ms tick");
```

- [ ] **Step 2: Verify RED against the current every-tick observer**

```powershell
& $cmake --build --preset modern-release --target cod_native_controller_tests -- /m
& 'build-modern/Release/cod_native_controller_tests.exe'
```

Confirm sample count or velocity-spike assertion fails for the expected
duplicate-frame reason.

- [ ] **Step 3: Extend the observation contract**

Add these fields:

```cpp
struct BodyLockMotionObservation {
    // existing geometry fields
    bool fresh_observation = false;
    std::uint64_t vision_sequence = 0;
    std::uint64_t selected_track_id = 0;
    float left_x = 0.0f;
};
```

Add to `NativeAiAimInput`:

```cpp
bool fresh_observation = false;
std::uint64_t vision_sequence = 0;
std::uint64_t selected_track_id = 0;
float left_x = 0.0f;
```

Populate all fields from `NativeControllerVisionState` and `PhysicalGamepadState` in `apply_ai_aim`.

- [ ] **Step 4: Implement exactly-once timestamp handling**

In `BodyLockMotionPolicy::observe`, return without changing velocity/confidence when input is not fresh or sequence equals `last_consumed_sequence_`. Use `observed_at_seconds` as the measurement timestamp. Accept only `0.004 <= dt <= 0.040`; an out-of-range sample reseeds position without creating velocity. A selected-track change clears target-motion reference but retains only a low-confidence mobility prior.

- [ ] **Step 5: Run focused and integrated tests GREEN**

```powershell
& $cmake --build --preset modern-release --target cod_native_controller_tests cod_native_lstick_benchmark_tests -- /m
& 'build-modern/Release/cod_native_controller_tests.exe'
& 'build-modern/Release/cod_native_lstick_benchmark_tests.exe'
```

Expected: duplicate-sequence test passes; existing BodyLock motion-lead, vertical-tail, and benchmark contract tests remain green.

- [ ] **Step 6: Commit fresh-frame correctness**

```powershell
git -C $repo add native/controller_native/bodylock_policy.* native/controller_native/ai_aim.* native/controller_native/native_gamepad_controller.cpp native/controller_native/controller_behavior_tests.cpp
git -C $repo commit -m "fix: update bodylock motion on fresh vision only"
```

## Task 3: Add In-Memory Relative-Motion Learning and Left-Intent Prediction

**Files:**
- Modify: `native/controller_native/bodylock_policy.h`
- Modify: `native/controller_native/bodylock_policy.cpp`
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark_tests.cpp`

- [ ] **Step 1: Write RED tests for cold, warm, synchronized, opposite, and mismatched motion**

Add four independent tests:

```cpp
require_true(cold.first_nonzero_feedforward_ms <= 80.0);
require_true(warm.reversal_response_ms <= 20.0);
require_near(synchronized.motion_feedforward_x, 0.0f, 0.02f);
require_true(std::fabs(opposite.motion_feedforward_x) >
             std::fabs(same_direction.motion_feedforward_x));
require_true(mismatch.rejected_on_next_informative_left_event);
```

Reset the controller and assert the mobility prior returns to `Cold`; no learned
gain may survive a process/controller reset or be written to disk.

Also change the intent-invariance desired assertion so two controllers receiving identical vision/right input but different left intent must produce `max_ai_trace_delta > 1e-4` after warm-up.

- [ ] **Step 2: Run the tests and verify RED because left intent is ignored**

```powershell
& $cmake --build --preset modern-release --target cod_native_controller_tests cod_native_lstick_benchmark_tests -- /m
& 'build-modern/Release/cod_native_controller_tests.exe'
& 'build-modern/Release/cod_native_lstick_benchmark_tests.exe'
```

Expected failures: no warm reversal response, no learned gain state, and
`max_ai_trace_delta == 0`.

- [ ] **Step 3: Add estimator state and diagnostics**

Add fixed scalar state to `BodyLockMotionPolicy`:

```cpp
enum class RelativeMotionState { Cold, Validating, Warm, Rejected };

struct RelativeMotionEstimate {
    float measured_rate_body_per_sec = 0.0f;
    float predicted_rate_body_per_sec = 0.0f;
    float strafe_gain = 0.0f;
    float confidence = 0.0f;
    float lead_x_px = 0.0f;
    RelativeMotionState state = RelativeMotionState::Cold;
};
```

Keep all values bounded and finite. No allocation, persistence, weapon identifier, or vision invocation is permitted.

- [ ] **Step 4: Implement bounded online learning**

For each eligible fresh frame, compute body-height-normalized rate. Fit the one-scalar mobility gain only when `left_x` has a meaningful onset, release, or reversal, using the adjacent rate/input differences. Steady-left frames never fit a separate target-drift model; the tracker remains the owner of target motion. Bound compatible gain samples with an EMA and reject a materially incompatible prior immediately on the high-information input event. ADS release decays confidence, while track switch clears per-target motion history and retains the mobility prior at low confidence.

- [ ] **Step 5: Predict between fresh frames without writing stick output**

Return measured relative rate as the cold feed-forward source. When the prior is warm, adjust projected rate immediately for left onset/reversal:

```cpp
predicted_rate = measured_rate - strafe_gain * (left_x - left_at_last_observation);
lead_x_px = clamp(predicted_rate * body_height_px * lead_seconds,
                  -lead_max_px, lead_max_px);
```

The observer returns only `RelativeMotionEstimate`. `NativeAiAim` consumes its lead through the existing pixels-to-stick mapping and BodyLock force cap.

- [ ] **Step 6: Verify GREEN and commit**

```powershell
& $cmake --build --preset modern-release --target cod_native_controller_tests cod_native_lstick_benchmark_tests cod_native_lstick_benchmark -- /m
& 'build-modern/Release/cod_native_controller_tests.exe'
& 'build-modern/Release/cod_native_lstick_benchmark_tests.exe'
& 'build-modern/Release/cod_native_left_stick_motion_benchmark.exe'
```

Expected: left intent changes the AI trace, synchronized motion stays near zero,
no AI/manual opposition frames are introduced, and normal benchmark fixture
validation remains green.

```powershell
git -C $repo add native/controller_native/bodylock_policy.* native/controller_native/ai_aim.* native/controller_native/controller_behavior_tests.cpp native/controller_native/left_stick_motion_defect_benchmark_tests.cpp
git -C $repo commit -m "feat: learn bodylock relative strafe motion in memory"
```

## Task 4: Establish a Single Planner and Smooth ADS-to-BodyLock Handoff

**Files:**
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/aim_assist_dynamics.h`
- Modify: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/controller_native/ads_completion_gate.h`
- Modify: `native/controller_native/ads_completion_gate.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/ads_completion_gate_tests.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [x] **Step 1: Write a failing integrated ADS-to-BodyLock handoff test**

Add a closed-loop controller test with zero manual/recoil that feeds fresh errors
`20, 11, 7, 5, 3` px, records `before_recoil_stick.x`, crosses into BodyLock,
and asserts:

```cpp
require_true(first_bodylock_tick_delta <= 0.07f,
    "ADS-to-BodyLock must preserve the delivered AI envelope");
require_true(max_transition_overshoot_px <= 2.0f,
    "stationary-target handoff must not create transition overshoot");
require_true(position_feedback_is_non_increasing,
    "closing position feedback must decay until terminal approach is safe");
```

- [x] **Step 2: Run the controller test and verify RED at the mode boundary**

```powershell
& $cmake --build --preset modern-release --target cod_native_controller_tests -- /m
& 'build-modern/Release/cod_native_controller_tests.exe'
```

Expected: failure reports a handoff delta/overshoot because ADS dynamics history
is reset and the actual post-brake ADS output is not transferred.

- [x] **Step 3: Add a delivered ADS seed contract to the envelope**

Add:

```cpp
void observe_pre_recoil_output(
    common_native::Vec2f manual,
    common_native::Vec2f pre_recoil,
    bool ads_snap_active,
    std::uint64_t selected_track_id);
```

Store `pre_recoil - manual` only for ADS snap and the current selected track. Call it after ADS near/carry brakes and before recoil. Never include manual or recoil in the seed.

- [x] **Step 4: Separate BodyLock position feedback from motion feed-forward**

Inside `NativeAiAim::compute`, calculate:

```cpp
common_native::Vec2f plan_bodylock_position(common_native::Vec2f error_px) const;
common_native::Vec2f cap_bodylock_terminal_position(
    common_native::Vec2f position_assist,
    common_native::Vec2f lock_error_px) const;

const common_native::Vec2f position_assist = plan_bodylock_position(lock_error);
const common_native::Vec2f error_with_lead{
    lock_error.x + trusted_motion_lead.x,
    lock_error.y + trusted_motion_lead.y};
const common_native::Vec2f combined_assist =
    plan_bodylock_position(error_with_lead);
const common_native::Vec2f motion_feedforward{
    combined_assist.x - position_assist.x,
    combined_assist.y - position_assist.y};
const common_native::Vec2f capped_position =
    cap_bodylock_terminal_position(position_assist, lock_error);
const common_native::Vec2f desired_assist{
    capped_position.x + motion_feedforward.x,
    capped_position.y + motion_feedforward.y};
```

`terminal_cap` is continuous, uses the existing reticle-speed/projection horizon, and may only reduce the position component. The combined result still passes the existing BodyLock max-force cap.

- [x] **Step 5: Make `NativeAimAssistDynamics` the sole delivered BodyLock owner**

Remove `apply_body_lock_smoothing` calls and BodyLock stick-history writes from `NativeAiAim`. On the first BodyLock tick for the same ADS-selected track, seed `previous_assist_` from the actual post-brake ADS AI. Apply per-1 ms BodyLock limits:

```cpp
constexpr float kStepCapPerMs = 0.035f;
constexpr float kJerkCapPerMs = 0.018f;
```

Sign reversals target zero first. ADS mode itself remains passthrough; manual and recoil are not shaped.

- [x] **Step 6: Replace the old overshoot-permission test with ownership plus handoff tests**

Keep the assertion that `AdsCarryBrakePolicy` cannot mutate a BodyLock tick. Replace the expectation that it must allow overshoot with two tests: moving-target feed-forward survives terminal proximity, and stationary-target position feedback cannot create more than 2 px handoff overshoot.

- [x] **Step 7: Run GREEN verification and commit**

```powershell
& $cmake --build --preset modern-release --target cod_native_ads_completion_gate_tests cod_native_controller_tests -- /m
& 'build-modern/Release/cod_native_ads_completion_gate_tests.exe'
& 'build-modern/Release/cod_native_controller_tests.exe'
```

Expected: terminal eligibility, seed continuity, moving-target preservation, and `<= 0.07` transition delta pass.

```powershell
git -C $repo add native/controller_native/ai_aim.* native/controller_native/aim_assist_dynamics.* native/controller_native/native_gamepad_controller.* native/controller_native/controller_behavior_tests.cpp
git -C $repo commit -m "fix: hand ads acquisition smoothly to bodylock"
```

## Task 5: Make Lifecycle Coast and Release Explicit Without Generic Resets

**Files:**
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/bodylock_lifecycle.h`
- Modify: `native/controller_native/bodylock_lifecycle.cpp`
- Modify: `native/controller_native/bodylock_lifecycle_tests.cpp`
- Modify: `native/controller_native/aim_assist_dynamics.h`
- Modify: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [x] **Step 1: Add RED lifecycle tests**

Add tests proving:

```cpp
gap.bodylock_available = false;
gap.authority = AssistAuthorityState::Continuity;
require(lifecycle.update(gap).state == BodylockLifecycleState::Coast,
        "same-track continuity may coast without current body geometry");

loss.authority = AssistAuthorityState::Reject;
require(!lifecycle.update(loss).reset_assist_history,
        "identity loss requests envelope release, not a generic hard reset");
```

Add integrated 1000 Hz assertions that Coast preserves bounded AI for at most 96 ms, expired identity creates no new plan, and delivered AI reaches zero within 40 ms with no tick delta above `0.07`.
Add a 40 ms invalid-geometry fixture that resumes the same track without a cold
envelope reset, plus a valid reacquisition fixture that returns useful assist
within 60 ms.

- [x] **Step 2: Verify RED against the immediate Yield/reset path**

```powershell
& $cmake --build --preset modern-release --target cod_native_bodylock_lifecycle_tests cod_native_controller_tests -- /m
& 'build-modern/Release/cod_native_bodylock_lifecycle_tests.exe'
& 'build-modern/Release/cod_native_controller_tests.exe'
```

Expected: continuity with missing geometry yields immediately and reject asks
for generic reset.

- [x] **Step 3: Reorder lifecycle authority decisions**

Handle same-track `Continuity` before `bodylock_available == false`. On Reject/TrackOnly/track switch, emit `Yield` plus a targeted observer-reset/release reason but keep `reset_assist_history == false`; only full controller reset clears both observer and envelope immediately.

- [x] **Step 4: Separate ADS evidence from BodyLock continuity in the controller**

Compute ADS snap eligibility from `current_observed_target_present`. Compute BodyLock planner authority from lifecycle ownership (`Warm`, `Tracking`, `Coast`) and selected-track identity. Do not reuse the ADS current-evidence boolean as `NativeAiAim`'s BodyLock gate.

- [x] **Step 5: Implement envelope Coast and Release**

During Coast with no new desired plan, decay the last BodyLock desired/delivered AI monotonically. During Yield/Inactive after prior BodyLock ownership, shape the AI component toward zero; do not reset history until both axes reach zero. A deliberate opposing manual input may immediately remove opposing AI, but manual remains unmodified.

- [x] **Step 6: Verify GREEN and commit**

```powershell
& $cmake --build --preset modern-release --target cod_native_bodylock_lifecycle_tests cod_native_controller_tests -- /m
& 'build-modern/Release/cod_native_bodylock_lifecycle_tests.exe'
& 'build-modern/Release/cod_native_controller_tests.exe'
```

```powershell
git -C $repo add native/controller_native/bodylock_lifecycle.* native/controller_native/bodylock_lifecycle_tests.cpp native/controller_native/aim_assist_dynamics.* native/controller_native/native_gamepad_controller.cpp native/controller_native/controller_behavior_tests.cpp
git -C $repo commit -m "fix: release bodylock authority through one envelope"
```

## Task 6: Remove Redundant BodyLock Output Mutations and Preserve Config Compatibility

**Files:**
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/body_lock_short_plan_policy.cpp`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Add a RED output-ownership stage test**

Instrument the existing stage trace/output components and assert that a BodyLock tick can change desired AI only in `ai_aim` and delivered AI only in `aim_assist_dynamics`. `output_validation`, `ads_near_target_brake`, `ads_carry_brake`, and `body_lock_short_plan` must report zero BodyLock delta.

- [ ] **Step 2: Add config compatibility tests before refactoring**

Load current `config.toml` and `testdata/legacy_full_config.toml`. Assert:

```cpp
require_near(config.ai_aim.body_lock_smoothing, 0.14f, 1e-6f);
require_near(config.ai_aim.body_lock_manual_escape_input_threshold, 0.45f, 1e-6f);
require_near(config.ai_aim.body_lock_manual_escape_preservation, 0.55f, 1e-6f);
```

The fields remain parser aliases but may not activate separate runtime state machines.

- [ ] **Step 3: Verify ownership RED and config parsing GREEN**

```powershell
& $cmake --build --preset modern-release --target cod_native_controller_tests cod_native_runtime_config_tests -- /m
& 'build-modern/Release/cod_native_controller_tests.exe'
& 'build-modern/Release/cod_native_runtime_config_tests.exe'
```

Expected: ownership fails on redundant BodyLock mutations; both config fixtures
parse.

- [ ] **Step 4: Consolidate planner transformations**

Replace the discrete zero-cross hold with a continuous soft deadband. Merge stabilization/vertical-tail force multiplication into one bounded confidence/motion scale. Use one continuous manual-priority function for BodyLock AI; remove calls to redundant takeover/overlap/escape zeroing after the canonical result is formed.

- [ ] **Step 5: Remove dead BodyLock short-plan ownership**

Keep `BodyLockShortPlanPolicy` ADS-only or remove its unreachable `last_mode() == "body_lock"` branch. It must not run after the BodyLock planner. Preserve its covered ADS behavior.

- [ ] **Step 6: Map legacy keys to canonical behavior and verify GREEN**

`body_lock_smoothing` configures envelope damping. Manual escape aliases configure the one manual-priority curve. Keep unknown-key validation unchanged.

```powershell
& $cmake --build --preset modern-release --target cod_native_runtime_config_tests cod_native_controller_tests -- /m
& 'build-modern/Release/cod_native_runtime_config_tests.exe'
& 'build-modern/Release/cod_native_controller_tests.exe'
```

```powershell
git -C $repo add native/controller_native/ai_aim.* native/controller_native/body_lock_short_plan_policy.cpp native/controller_native/runtime_config.cpp native/controller_native/runtime_config_tests.cpp native/controller_native/controller_behavior_tests.cpp
git -C $repo commit -m "refactor: keep one bodylock planner and envelope"
```

## Task 7: Upgrade the Benchmark to Live Frequencies and Close Desired Gates

**Files:**
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.h`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark.cpp`
- Modify: `native/controller_native/left_stick_motion_defect_benchmark_tests.cpp`
- Modify: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

- [ ] **Step 1: Write RED benchmark contract tests for the frequency matrix**

Require schema 3 with `controller_hz == 1000`, primary vision rates 80 Hz and
100 Hz, stress rates 50 Hz and 160 Hz, and per-run
`fresh_sequences_consumed == delivered_vision_sequences`.

- [ ] **Step 2: Add desired-gate assertions before changing the fixture**

Require the committed acceptance thresholds:

```cpp
require(report.intent_invariance.desired_gate_pass);
require(primary.fast_mean_improvement_ratio >= 0.20);
require(primary.fast_p95_improvement_ratio >= 0.20);
require(primary.same_direction_regression_ratio <= 0.05);
require(primary.max_lifecycle_ai_delta <= 0.07);
require(primary.large_sign_flip_count == 0);
require(report.production_chain.short_gap_coast_pass);
require(report.production_chain.long_loss_release_pass);
require(report.production_chain.reacquire_bumpless_pass);
require(report.production_chain.no_blind_candidate_follow_pass);
require(report.ads_handoff.max_transition_overshoot_px <= 2.0);
```

- [ ] **Step 3: Run and verify RED against the current 100/50 Hz schema-2 fixture**

```powershell
& $cmake --build --preset modern-release --target cod_native_lstick_benchmark_tests -- /m
& 'build-modern/Release/cod_native_lstick_benchmark_tests.exe'
```

Expected failure identifies schema/frequency/desired gates, not fixture
corruption.

- [ ] **Step 4: Implement 1000 Hz controller and vision-rate matrix**

Use 1 ms controller ticks for 3.6 seconds per closed-loop scenario. Generate vision sequences using an accumulator so 80 Hz and 160 Hz remain deterministic without integer tick truncation. Preserve capture timestamp and delayed delivery. Score AI component separately from manual and recoil.

- [ ] **Step 5: Split production-chain semantics and add ADS handoff report**

Replace the single drift-gap failure with short same-track Coast, long identity Release, bumpless Reacquire, and no-blind-candidate-follow booleans/metrics. Add stationary and moving-relative-motion ADS handoff traces.

- [ ] **Step 6: Tune only estimator/planner/envelope internal constants from failing metrics**

Change one constant family at a time, rerun the benchmark, and keep adjustments only when they improve the failing metric without violating same-direction, smoothness, manual, recoil, or small/far authority guards. Do not introduce weapon data or another vision operation.

- [ ] **Step 7: Verify normal and fixed gates GREEN and commit**

```powershell
& $cmake --build --preset modern-release --target cod_native_lstick_benchmark cod_native_lstick_benchmark_tests -- /m
& 'build-modern/Release/cod_native_lstick_benchmark_tests.exe'
& 'build-modern/Release/cod_native_left_stick_motion_benchmark.exe' --output '../../artifacts/benchmarks/native_gamepad/left-stick-relative-motion-fixed-20260715.json'
& 'build-modern/Release/cod_native_left_stick_motion_benchmark.exe' --require-fixed
```

Expected: tests pass, normal benchmark exits 0, and `--require-fixed` now exits 0.

```powershell
git -C $repo add native/controller_native/left_stick_motion_defect_benchmark.* native/controller_native/left_stick_motion_defect_benchmark_tests.cpp docs/project/NATIVE_CONTROLLER_BENCHMARKS.md
git -C $repo commit -m "test: gate live-rate bodylock relative motion"
```

## Task 8: Full Verification and Scope Audit

**Files:**
- Verify only; modify a source file only if a new RED regression test first demonstrates a defect.

- [ ] **Step 1: Build all affected targets from current sources**

```powershell
& $cmake --build --preset modern-release --target cod_native_ads_completion_gate_tests cod_native_bodylock_lifecycle_tests cod_native_runtime_config_tests cod_native_controller_tests cod_native_benchmark_metrics_tests cod_native_lstick_benchmark_tests cod_native_lstick_benchmark cod_native_gamepad_benchmark -- /m
```

- [ ] **Step 2: Run focused and regression executables**

```powershell
& 'build-modern/Release/cod_native_ads_completion_gate_tests.exe'
& 'build-modern/Release/cod_native_bodylock_lifecycle_tests.exe'
& 'build-modern/Release/cod_native_runtime_config_tests.exe'
& 'build-modern/Release/cod_native_controller_tests.exe'
& 'build-modern/Release/cod_native_benchmark_metrics_tests.exe'
& 'build-modern/Release/cod_native_lstick_benchmark_tests.exe'
& 'build-modern/Release/cod_native_gamepad_benchmark.exe' --self-test
& 'build-modern/Release/cod_native_left_stick_motion_benchmark.exe' --require-fixed
```

Expected: every command exits 0 with no failed assertion.

- [ ] **Step 3: Run CTest and static repository checks**

```powershell
& $cmake --build --preset modern-release --target ALL_BUILD -- /m
& $ctest --test-dir build-modern -C Release --output-on-failure
git -C $repo diff --check
git -C $repo status --short
```

Expected: build and CTest exit 0, `git diff --check` is silent, and status contains only intentional implementation/documentation changes or is clean after commits.

- [ ] **Step 4: Audit the approved boundaries**

Confirm from output components and diff:

- observer performs no stick write;
- BodyLock desired AI has one planner owner;
- BodyLock delivered AI has one envelope owner;
- ADS brakes do not mutate BodyLock after handoff;
- manual right stick is not smoothed;
- recoil remains final and independently attributed;
- no weapon table, persisted learned state, extra vision pass, or small/far authority increase exists;
- hot path has O(1) fixed state and no allocation/log formatting.

- [ ] **Step 5: Commit final documentation or regression-only corrections**

```powershell
git -C $repo add docs/project/NATIVE_CONTROLLER_BENCHMARKS.md native/controller_native
git -C $repo commit -m "docs: record bodylock relative motion verification"
```

Skip this commit when Step 4 produces no new changes.
