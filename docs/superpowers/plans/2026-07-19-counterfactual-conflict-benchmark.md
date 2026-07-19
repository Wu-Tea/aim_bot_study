# Counterfactual Conflict Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic, benchmark-only counterfactual replay that measures local manual/AI mix regret and downstream correction burden without changing the production runtime binary or existing sustained AimLab score semantics.

**Architecture:** Extend the sustained AimLab simulator with an optional trace observer, detect fixed scenario anchors and merged dynamic conflict episodes, then reconstruct controller state by replaying from the script start with a fresh controller factory. A compile-time-only native-controller mix transform lets benchmark branches substitute the delivered mix before controller feedback state is recorded; the macro is defined only for benchmark/test targets, so the live runtime has no added interface or per-tick branch.

**Tech Stack:** C++20, existing native controller pipeline, deterministic sustained AimLab simulator, CMake/MSBuild, PowerShell verification scripts, JSON artifact writer.

---

## File Structure

- Create `native/controller_native/sustained_aimlab_trace.h`: fixed-anchor, conflict-episode, and analysis-budget value types built on simulator trace frames.
- Create `native/controller_native/sustained_aimlab_trace.cpp`: fixed-anchor generation, dynamic conflict detection, episode merging, and deterministic budget selection.
- Create `native/controller_native/sustained_aimlab_trace_tests.cpp`: trace, anchor, detector, merge, and budget unit tests.
- Create `native/controller_native/sustained_aimlab_counterfactual.h`: branch policy, replay request, branch result, oracle result, and analyzer interfaces.
- Create `native/controller_native/sustained_aimlab_counterfactual.cpp`: replay verification, branch execution, causal/hindsight selection, local classification, and aggregate metrics.
- Create `native/controller_native/sustained_aimlab_counterfactual_tests.cpp`: deterministic replay, causal boundary, oracle ordering, and future-burden fixtures.
- Modify `native/controller_native/sustained_aimlab_simulator.h`: add optional trace observation and a controller-factory overload without changing existing callers.
- Modify `native/controller_native/sustained_aimlab_simulator.cpp`: emit pre/post plant truth and controller components to the trace observer.
- Modify `native/controller_native/sustained_aimlab_simulator_tests.cpp`: prove tracing is observational and deterministic.
- Modify `native/controller_native/native_gamepad_controller.h`: declare a mix transform only under `COD_BENCHMARK_MIX_OVERRIDE`.
- Modify `native/controller_native/native_gamepad_controller.cpp`: apply the benchmark transform before response/tracker feedback is persisted, only under the same macro.
- Modify `native/controller_native/native_gamepad_controller_tests.cpp`: prove the transform changes delivered mix and feedback consistently in the test-only build.
- Modify `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`: create replayable native controller factories, run quick/full analysis, and write the new JSON section.
- Modify `native/vision_native/CMakeLists.txt`: build the two focused test targets and scope the benchmark macro to benchmark/test targets only.
- Modify `scripts/verify/compare_sustained_aimlab.ps1`: compare causal regret, future burden, and coverage while retaining existing fields.
- Modify `docs/benchmarks/sustained-aimlab.md`: document modes, metrics, oracle boundary, and commands.

### Task 1: Add an observational simulator trace

**Files:**
- Modify: `native/controller_native/sustained_aimlab_simulator.h`
- Modify: `native/controller_native/sustained_aimlab_simulator.cpp`
- Modify: `native/controller_native/sustained_aimlab_simulator_tests.cpp`

- [ ] **Step 1: Write failing tests for deterministic, observational tracing**

Add a test that runs the same stationary script with and without a trace observer, then asserts identical `BenchmarkResult` fields and exact trace truth:

```cpp
void test_trace_is_deterministic_and_observational() {
    const ScenarioScript script = stationary_script(40, {20.0, 10.0});
    const BenchmarkResult plain = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    std::vector<SimulationTraceFrame> trace;
    const BenchmarkResult observed = run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    require(plain.acquire_points == observed.acquire_points,
            "trace observer must not change acquisition score");
    require(plain.tracking_points == observed.tracking_points,
            "trace observer must not change tracking score");
    require(trace.size() == 40, "trace must contain one frame per tick");
    require(trace.front().true_error_before_px.x == 20.0,
            "trace must expose pre-control ground truth");
    require(trace.front().absolute_ms == 0,
            "trace timestamps must begin at zero");
}
```

- [ ] **Step 2: Build and run the simulator test to verify failure**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_simulator_tests
```

Expected: compilation fails because `SimulationTraceFrame` and the observer overload do not exist.

- [ ] **Step 3: Define the trace types and optional observer**

Define the trace frame in `sustained_aimlab_simulator.h` immediately after
`ControllerStepResult`, where all of its value members are complete:

```cpp
namespace controller_native::sustained_aimlab {

struct SimulationTraceFrame {
    int absolute_ms = 0;
    int target_elapsed_ms = -1;
    bool target_active = false;
    bool fresh_vision = false;
    std::uint64_t target_id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    Vec2d true_error_before_px;
    Vec2d true_error_after_px;
    Vec2d target_velocity_px_per_second;
    ControllerObservation input;
    ControllerStepResult output;
};

using SimulationTraceObserver =
    std::function<void(const SimulationTraceFrame&)>;

}  // namespace controller_native::sustained_aimlab
```

Extend the existing signature with a defaulted final argument:

```cpp
BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire,
    SimulationTraceObserver trace_observer = {});
```

Emit one frame per simulator tick after the plant applies the final stick. For
inactive ticks, record `target_active=false` and zero truth fields.

- [ ] **Step 4: Run focused tests and verify pass**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_simulator_tests
& b/Release/cod_native_sustained_aimlab_simulator_tests.exe
```

Expected: `cod_native_sustained_aimlab_simulator_tests PASS`.

- [ ] **Step 5: Commit the observational trace**

```powershell
git add native/controller_native/sustained_aimlab_simulator.h native/controller_native/sustained_aimlab_simulator.cpp native/controller_native/sustained_aimlab_simulator_tests.cpp
git commit -m "bench: record sustained aimlab simulation trace"
```

### Task 2: Generate fixed anchors and dynamic conflict episodes

**Files:**
- Modify: `native/controller_native/sustained_aimlab_trace.h`
- Create: `native/controller_native/sustained_aimlab_trace.cpp`
- Create: `native/controller_native/sustained_aimlab_trace_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing anchor and conflict tests**

Create tests covering script-owned reversals and merged manual/AI opposition:

```cpp
void test_fixed_reverse_anchor_comes_from_script() {
    ScenarioScript script;
    TargetScript target;
    target.id = 9;
    target.motion = MotionProfile::Reverse;
    target.maneuver_at_ms = 120;
    script.targets.push_back(target);
    const auto anchors = generate_fixed_anchors(script);
    require(anchors.size() == 1, "reverse script must create one anchor");
    require(anchors.front().kind == AnchorKind::TargetReverse,
            "anchor must preserve maneuver kind");
    require(anchors.front().target_elapsed_ms == 120,
            "anchor must preserve maneuver time");
}

void test_opposition_frames_merge_into_one_episode() {
    std::vector<SimulationTraceFrame> trace(12);
    for (int ms = 0; ms < 12; ++ms) {
        trace[ms].absolute_ms = ms;
        trace[ms].target_active = true;
        trace[ms].input.manual_stick = {0.20, 0.0};
        trace[ms].output.shaped_assist_stick = {-0.18, 0.0};
    }
    const auto episodes = detect_conflict_episodes(trace, ConflictConfig{});
    require(episodes.size() == 1, "adjacent conflict frames must merge");
    require(episodes.front().start_ms == 0 && episodes.front().end_ms == 11,
            "merged episode must preserve inclusive bounds");
}
```

Also add fixtures for near-zero cancellation, 10-20 px stall, circle exit,
continued old-direction commitment, and same-direction destructive stacking.

- [ ] **Step 2: Add the focused test target and verify failure**

Add `cod_native_sustained_aimlab_trace_tests` to CMake with
`sustained_aimlab_trace_tests.cpp`, `sustained_aimlab_trace.cpp`, and
`sustained_aimlab_scenario.cpp`.

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_trace_tests
```

Expected: compilation fails because anchor and detector APIs are absent.

- [ ] **Step 3: Implement versioned anchors, detectors, merging, and budgets**

Add these public types and functions:

```cpp
enum class AnchorKind : std::uint8_t {
    TargetReverse,
    TargetStop,
    JumpApex,
    FallTransition,
    ObservationLoss,
    ObservationRecovery,
    AdsSettled,
    AdsBodylockHandoff,
};

enum class ConflictKind : std::uint8_t {
    ManualAiOpposition,
    NearZeroCancellation,
    StallRing,
    DirectionReversal,
    CircleExit,
    FalseInterruption,
    WrongWayCommit,
    DestructiveStack,
};

struct EvaluationPoint {
    int absolute_ms = 0;
    int target_elapsed_ms = 0;
    std::uint64_t target_id = 0;
    AnchorKind kind = AnchorKind::TargetReverse;
};

struct ConflictEpisode {
    ConflictKind kind = ConflictKind::ManualAiOpposition;
    int start_ms = 0;
    int end_ms = 0;
    double severity = 0.0;
};

struct ConflictConfig {
    double minimum_manual = 0.03;
    double minimum_ai = 0.03;
    double opposing_cosine = -0.15;
    double cancellation_output = 0.04;
    int merge_gap_ms = 4;
};

std::vector<EvaluationPoint> generate_fixed_anchors(
    const ScenarioScript& script,
    const std::vector<SimulationTraceFrame>& trace = {});
std::vector<ConflictEpisode> detect_conflict_episodes(
    const std::vector<SimulationTraceFrame>& trace,
    const ConflictConfig& config);
std::vector<ConflictEpisode> select_episode_budget(
    std::vector<ConflictEpisode> episodes,
    std::size_t per_kind_limit);
```

Use dot-product cosine for vector opposition, stable severity-descending then
timestamp-ascending ordering, and deterministic adjacent-interval merging.

- [ ] **Step 4: Run trace tests twice to prove determinism**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_trace_tests
& b/Release/cod_native_sustained_aimlab_trace_tests.exe
& b/Release/cod_native_sustained_aimlab_trace_tests.exe
```

Expected: both executions print `cod_native_sustained_aimlab_trace_tests PASS`.

- [ ] **Step 5: Commit anchor and conflict detection**

```powershell
git add native/controller_native/sustained_aimlab_trace.h native/controller_native/sustained_aimlab_trace.cpp native/controller_native/sustained_aimlab_trace_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "bench: detect sustained aim conflict episodes"
```

### Task 3: Add a benchmark-only delivered-mix seam

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/native_gamepad_controller_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write a failing native-controller consistency test**

Compile `cod_native_controller_tests` with `COD_BENCHMARK_MIX_OVERRIDE` and add:

```cpp
void test_benchmark_mix_override_updates_delivered_feedback() {
    NativeGamepadController controller(test_config(), deterministic_clock());
    controller.set_benchmark_mix_transform(
        [](float manual_x, float manual_y, float, float,
           const NativeControllerOutputComponents&) {
            return pipeline_contract::Vec2f{manual_x, manual_y};
        });
    PhysicalGamepadState physical{};
    physical.right_x = 0.21f;
    physical.right_y = -0.13f;
    const GamepadOutputState output = controller.build_output(physical);
    require_near(output.right_x, 0.21f, 1e-6f,
                 "override must deliver manual X");
    require_near(controller.last_output_components().before_recoil_stick.x,
                 0.21f, 1e-6f,
                 "feedback state must record overridden X");
    require_near(controller.last_tracker_motion_output().right_x,
                 output.right_x, 1e-6f,
                 "tracker motion feedback must match delivered output");
}
```

- [ ] **Step 2: Verify the test fails to compile**

Run:

```powershell
cmake --build b --config Release --target cod_native_controller_tests
```

Expected: compilation fails because `set_benchmark_mix_transform` is absent.

- [ ] **Step 3: Implement the macro-scoped seam before feedback persistence**

Under `#if defined(COD_BENCHMARK_MIX_OVERRIDE)`, add:

```cpp
using BenchmarkMixTransform = std::function<pipeline_contract::Vec2f(
    float, float, float, float,
    const NativeControllerOutputComponents&)>;
void set_benchmark_mix_transform(BenchmarkMixTransform transform);
BenchmarkMixTransform benchmark_mix_transform_;
```

In `build_output`, after normal manual retention plus shaped-assist composition
and before `components.post_ai_stick`, auto-fire inputs, aim-response accumulation,
`before_recoil_stick`, and `last_output_components_`, apply:

```cpp
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
if (benchmark_mix_transform_) {
    const auto replacement = benchmark_mix_transform_(
        physical.right_x, physical.right_y,
        output.right_x, output.right_y, components);
    output.right_x = clamp_unit(replacement.x);
    output.right_y = clamp_unit(replacement.y);
}
#endif
```

Define the macro only for `cod_native_controller_tests` and
`cod_native_sustained_aimlab_benchmark`. Confirm `cod_native_runtime` has no such
compile definition.

- [ ] **Step 4: Run controller and pipeline contract verification**

Run:

```powershell
cmake --build b --config Release --target cod_native_controller_tests cod_native_runtime
& b/Release/cod_native_controller_tests.exe
& scripts/verify/native_pipeline_contract.bat
```

Expected: controller tests print PASS, the pipeline contract prints PASS, and the
runtime builds without the benchmark macro.

- [ ] **Step 5: Commit the isolated seam**

```powershell
git add native/controller_native/native_gamepad_controller.h native/controller_native/native_gamepad_controller.cpp native/controller_native/native_gamepad_controller_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "test: expose benchmark-only aim mix transform"
```

### Task 4: Implement deterministic replay and branch policies

**Files:**
- Create: `native/controller_native/sustained_aimlab_counterfactual.h`
- Create: `native/controller_native/sustained_aimlab_counterfactual.cpp`
- Create: `native/controller_native/sustained_aimlab_counterfactual_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing replay identity and branch tests**

Use a factory that creates a fresh stateful test controller and accepts a branch
schedule. Assert reference replay equality before the branch and a lower error for
the helpful branch:

```cpp
void test_replay_matches_reference_before_branch() {
    const ScenarioScript script = replay_fixture();
    const auto factory = proportional_factory();
    const ReplayReference reference = record_reference(
        script, ManualProfile::Mixed, BenchmarkCohort::AdsAcquire, factory);
    ReplayRequest request;
    request.branch_at_ms = 80;
    request.substitution_ms = 40;
    request.horizon_ms = 160;
    request.policy = BranchPolicy::ActualMix;
    const BranchResult branch = replay_branch(reference, request, factory);
    require(branch.prebranch_verified,
            "actual replay must match every prebranch frame");
    require(branch.first_divergence_ms == -1,
            "actual mix must not diverge from reference");
}

void test_manual_only_branch_can_beat_harmful_ai() {
    const ReplayReference reference = opposing_fixture_reference();
    const BranchResult actual = replay_branch(
        reference, request_for(BranchPolicy::ActualMix), opposing_factory());
    const BranchResult manual = replay_branch(
        reference, request_for(BranchPolicy::ManualOnly), opposing_factory());
    require(manual.error_area_px_ms < actual.error_area_px_ms,
            "helpful manual-only branch must beat harmful mix");
}
```

- [ ] **Step 2: Add the focused target and verify failure**

Build the new test target from counterfactual, trace, simulator, scorer, and
scenario sources.

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_counterfactual_tests
```

Expected: compilation fails because replay interfaces are absent.

- [ ] **Step 3: Implement factory-based replay and finite branch policies**

Define:

```cpp
enum class BranchPolicy : std::uint8_t {
    ActualMix,
    ManualOnly,
    AiOnly,
    ManualPlusAi25,
    ManualPlusAi50,
    ManualPlusAi75,
};

struct BranchSchedule {
    int start_ms = 0;
    int duration_ms = 0;
    BranchPolicy policy = BranchPolicy::ActualMix;
};

using ReplayControllerFactory =
    std::function<ControllerStep(const BranchSchedule&)>;

struct ReplayRequest {
    int branch_at_ms = 0;
    int substitution_ms = 40;
    int horizon_ms = 160;
    BranchPolicy policy = BranchPolicy::ActualMix;
};

struct ReplayReference {
    ScenarioScript script;
    ManualProfile manual_profile = ManualProfile::Pure;
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire;
    std::vector<SimulationTraceFrame> trace;
};

struct BranchResult {
    BranchPolicy policy = BranchPolicy::ActualMix;
    bool prebranch_verified = false;
    int first_divergence_ms = -1;
    double error_area_px_ms = 0.0;
    double final_error_px = 0.0;
    double path_px = 0.0;
    int correction_reversals = 0;
    int time_to_acquire_ms = -1;
    int settle_ms = -1;
    int false_interrupt_ms = 0;
    int reacquire_delay_ms = 0;
    std::vector<SimulationTraceFrame> trace;
};
```

`record_reference` runs with an inactive `ActualMix` schedule. `replay_branch`
creates a fresh callback from the factory, re-runs from time zero, verifies every
prebranch `ControllerStepResult` and truth state within `1e-6`, and analyzes only
the requested post-branch horizon. Reject invalid times and non-finite output.

- [ ] **Step 4: Run replay tests twice**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_counterfactual_tests
& b/Release/cod_native_sustained_aimlab_counterfactual_tests.exe
& b/Release/cod_native_sustained_aimlab_counterfactual_tests.exe
```

Expected: both executions print
`cod_native_sustained_aimlab_counterfactual_tests PASS`.

- [ ] **Step 5: Commit deterministic replay**

```powershell
git add native/controller_native/sustained_aimlab_counterfactual.h native/controller_native/sustained_aimlab_counterfactual.cpp native/controller_native/sustained_aimlab_counterfactual_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "bench: replay sustained aim counterfactual branches"
```

### Task 5: Score local regret, future burden, and both oracles

**Files:**
- Modify: `native/controller_native/sustained_aimlab_counterfactual.h`
- Modify: `native/controller_native/sustained_aimlab_counterfactual.cpp`
- Modify: `native/controller_native/sustained_aimlab_counterfactual_tests.cpp`

- [ ] **Step 1: Write failing classification and oracle tests**

Add deterministic fixtures for manual-correct, AI-correct, both-harmful,
destructive-stack, and jump/fall local-benefit/global-harm:

```cpp
void test_jump_up_action_can_help_locally_but_hurt_globally() {
    const CounterfactualEpisode result = analyze_episode(
        jump_then_fall_reference(), jump_anchor(), jump_factory(), full_budget());
    require(result.actual.instant_progress_px > 0.0,
            "upward action must initially close jump error");
    require(result.actual.future_burden_px_ms > 0.0,
            "continued upward action must add fall correction burden");
    require(result.actual.classification ==
                OutcomeClass::LocalHelpfulGlobalHarmful,
            "fixture must expose local/global disagreement");
}

void test_causal_oracle_cannot_read_future_and_hindsight_is_lower_bound() {
    const CounterfactualEpisode result = analyze_episode(
        hidden_reverse_reference(), reverse_anchor(), causal_factory(), full_budget());
    require(result.causal_oracle.policy != result.hindsight_oracle.policy,
            "hidden future may separate causal and hindsight choices");
    require(result.hindsight_oracle.error_area_px_ms <=
                result.causal_oracle.error_area_px_ms,
            "hindsight must be a lower bound over eligible candidates");
}
```

- [ ] **Step 2: Run the focused test and verify failure**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_counterfactual_tests
```

Expected: compilation fails because episode scoring and oracle types are absent.

- [ ] **Step 3: Implement primitive metrics and explicit oracle selection**

Add:

```cpp
enum class OutcomeClass : std::uint8_t {
    LocalHelpfulGlobalHelpful,
    LocalHelpfulGlobalHarmful,
    LocalHarmfulGlobalHelpful,
    LocalHarmfulGlobalHarmful,
};

struct CounterfactualMetrics {
    double instant_progress_px = 0.0;
    double regret_40_px_ms = 0.0;
    double regret_80_px_ms = 0.0;
    double regret_160_px_ms = 0.0;
    double future_burden_px_ms = 0.0;
    int future_settle_delay_ms = 0;
    double extra_path_px = 0.0;
    int correction_reversals = 0;
    int manual_helped_but_suppressed_ms = 0;
    int ai_helped_but_suppressed_ms = 0;
    int both_harmful_ms = 0;
    int destructive_stack_ms = 0;
    int wrong_way_commit_ms = 0;
    int false_interrupt_ms = 0;
    int reacquire_delay_ms = 0;
    OutcomeClass classification =
        OutcomeClass::LocalHarmfulGlobalHarmful;
};

struct CounterfactualEpisode {
    int branch_at_ms = 0;
    std::vector<BranchResult> candidates;
    BranchResult causal_oracle;
    BranchResult hindsight_oracle;
    CounterfactualMetrics actual;
};
```

Use trapezoidal integration of target-center distance for `error_area_px_ms`.
Compute each regret against the minimum eligible candidate at that horizon.
Select the causal policy using only a `CausalView` containing trace frames through
`branch_at_ms`; make future script access impossible from the selector signature.
Estimate target velocity from the two most recent fresh observations, extrapolate
that constant velocity for 160 ms, and evaluate each candidate's first delivered
vector with the configured camera-response and slowdown model. Choose the minimum
predicted integrated-distance candidate, breaking ties by lower stick magnitude
and then enum order. This deliberately simple causal oracle is a reproducible
baseline, not a production planner. Select hindsight only after every realized
branch has completed. Treat a 1% increase over
the best future area as globally harmful and positive immediate closing as locally
helpful; record the thresholds in the artifact schema.

For stable-horizon analysis, continue no longer than 500 ms. Declare a branch
stable after 30 consecutive milliseconds inside the target radius with absolute
radial closing speed at or below 25 px/s and, for the BodyLock cohort, continuous
BodyLock mode. Record acquisition, false interruption, reacquisition, path, and
reversal values directly from the branch trace. Derive the five conflict-duration
counters by comparing per-millisecond manual-only, AI-only, actual, and best
candidate costs; do not infer them from vector direction alone.

- [ ] **Step 4: Run focused tests**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_counterfactual_tests
& b/Release/cod_native_sustained_aimlab_counterfactual_tests.exe
```

Expected: PASS with all four classifications and oracle-boundary fixtures.

- [ ] **Step 5: Commit scoring and oracle logic**

```powershell
git add native/controller_native/sustained_aimlab_counterfactual.h native/controller_native/sustained_aimlab_counterfactual.cpp native/controller_native/sustained_aimlab_counterfactual_tests.cpp
git commit -m "bench: score aim mix regret and future burden"
```

### Task 6: Integrate native-controller factories and JSON output

**Files:**
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add a failing smoke assertion for the JSON contract**

Extend the existing smoke verification or add a focused report test that parses
the generated JSON text and requires:

```cpp
require(json.find("\"counterfactual_conflict\"") != std::string::npos,
        "report must contain counterfactual section");
require(json.find("\"causal_oracle\"") != std::string::npos,
        "report must separate causal oracle");
require(json.find("\"hindsight_oracle\"") != std::string::npos,
        "report must separate hindsight oracle");
require(json.find("\"skipped_episodes\"") != std::string::npos,
        "report must disclose analysis coverage");
```

- [ ] **Step 2: Build and run smoke to verify failure**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_benchmark
& b/Release/cod_native_sustained_aimlab_benchmark.exe --config config.toml --duration-ms 1200 --seed 1337 --profile mixed --cohort ads --smoke --counterfactual quick --output runs/native_perf/counterfactual_smoke.json
```

Expected: argument parsing fails because `--counterfactual` is unknown.

- [ ] **Step 3: Build a fresh native factory per replay**

Refactor the current `run_native` adapter construction into:

```cpp
ReplayControllerFactory make_native_factory(
    const GamepadConfig& config,
    ManualProfile profile,
    BenchmarkCohort cohort) {
    return [=](const BranchSchedule& schedule) {
        auto state = std::make_shared<NativeReplayAdapter>(
            config, profile, cohort, schedule);
        return [state](const ControllerObservation& input) {
            return state->step(input);
        };
    };
}
```

`NativeReplayAdapter` owns its `NativeGamepadController`, deterministic 1 ms clock,
vision sequence, current simulator millisecond, and branch schedule. Install this
benchmark-only transform from its constructor:

```cpp
controller_.set_benchmark_mix_transform(
    [this](float manual_x, float manual_y, float mixed_x, float mixed_y,
           const NativeControllerOutputComponents& components) {
        const bool active = now_ms_ >= schedule_.start_ms &&
            now_ms_ < schedule_.start_ms + schedule_.duration_ms;
        if (!active) return pipeline_contract::Vec2f{mixed_x, mixed_y};
        switch (schedule_.policy) {
        case BranchPolicy::ActualMix: return pipeline_contract::Vec2f{mixed_x, mixed_y};
        case BranchPolicy::ManualOnly: return pipeline_contract::Vec2f{manual_x, manual_y};
        case BranchPolicy::AiOnly: return components.shaped_assist_stick;
        case BranchPolicy::ManualPlusAi25:
            return pipeline_contract::Vec2f{
                manual_x + components.shaped_assist_stick.x * 0.25f,
                manual_y + components.shaped_assist_stick.y * 0.25f};
        case BranchPolicy::ManualPlusAi50:
            return pipeline_contract::Vec2f{
                manual_x + components.shaped_assist_stick.x * 0.50f,
                manual_y + components.shaped_assist_stick.y * 0.50f};
        case BranchPolicy::ManualPlusAi75:
            return pipeline_contract::Vec2f{
                manual_x + components.shaped_assist_stick.x * 0.75f,
                manual_y + components.shaped_assist_stick.y * 0.75f};
        }
        return pipeline_contract::Vec2f{mixed_x, mixed_y};
    });
```

Here `mixed_x` and `mixed_y` are the normal production mix supplied to the
benchmark transform. The transform returns those values unchanged outside the
scheduled interval.

- [ ] **Step 4: Add quick/full CLI modes and independent JSON section**

Accept `--counterfactual off|quick|full`, defaulting to `off` until a new baseline
is recorded. Use per-kind limits `2` for quick and `12` for full. Write schema
version, candidate-set version, thresholds, budgets, detected/analyzed/skipped
counts, fixed-anchor aggregates, dynamic-episode aggregates, causal gap, hindsight
headroom, and bounded worst-episode details under `counterfactual_conflict`.

- [ ] **Step 5: Run smoke twice and compare normalized output**

Run the smoke command twice with different output paths, then normalize away only
the output path and revision dirtiness metadata. Expected: counterfactual sections
are identical and all replay checks report true.

- [ ] **Step 6: Verify off mode preserves legacy core output**

Run one smoke with `--counterfactual off` and compare its `runs[*]` core fields to
the pre-change artifact. Expected: every existing field is unchanged after JSON
normalization; the new top-level section reports mode `off` with zero analysis.

- [ ] **Step 7: Commit native integration**

```powershell
git add native/controller_native/cod_native_sustained_aimlab_benchmark.cpp native/vision_native/CMakeLists.txt
git commit -m "bench: report native counterfactual conflict metrics"
```

### Task 7: Add comparison reporting and operator documentation

**Files:**
- Modify: `scripts/verify/compare_sustained_aimlab.ps1`
- Modify: `docs/benchmarks/sustained-aimlab.md`

- [ ] **Step 1: Add comparison columns without a composite score**

Extend each comparison row with:

```powershell
causal_error_area_gap_delta_px_ms =
    $run.counterfactual.causal_error_area_gap_px_ms -
    $old.counterfactual.causal_error_area_gap_px_ms
future_burden_delta_px_ms =
    $run.counterfactual.future_burden_px_ms -
    $old.counterfactual.future_burden_px_ms
future_settle_delay_delta_ms =
    $run.counterfactual.future_settle_delay_ms -
    $old.counterfactual.future_settle_delay_ms
analyzed_episode_delta =
    $run.counterfactual.analyzed_episodes -
    $old.counterfactual.analyzed_episodes
skipped_episode_delta =
    $run.counterfactual.skipped_episodes -
    $old.counterfactual.skipped_episodes
```

Fail comparison if schema version, candidate-set version, mode, seed/script hash,
or budget differs. Do not sum the columns into one score.

- [ ] **Step 2: Document exact quick and full commands**

Update the benchmark guide with:

```powershell
# Fast regression
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --profile both --cohort both `
  --counterfactual quick --output runs/native_perf/counterfactual-quick.json

# Baseline/parameter analysis
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort both --counterfactual full `
  --output runs/native_perf/counterfactual-full.json
```

Explain that causal regret is the optimization baseline, hindsight is headroom,
and live feel remains the final acceptance check.

- [ ] **Step 3: Run comparison on two identical smoke artifacts**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/verify/compare_sustained_aimlab.ps1 -Baseline runs/native_perf/counterfactual_smoke_a.json -Candidate runs/native_perf/counterfactual_smoke_b.json
```

Expected: all metric deltas are zero.

- [ ] **Step 4: Commit reporting and documentation**

```powershell
git add scripts/verify/compare_sustained_aimlab.ps1 docs/benchmarks/sustained-aimlab.md
git commit -m "docs: report counterfactual aim benchmark deltas"
```

### Task 8: Run full verification and record the first diagnostic artifact

**Files:**
- Create: `docs/project/COUNTERFACTUAL_CONFLICT_BENCHMARK_ACCEPTANCE_20260719.md`
- Create: `runs/native_perf/sustained_aimlab_counterfactual_20260719.json`

- [ ] **Step 1: Build all affected targets**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_trace_tests cod_native_sustained_aimlab_counterfactual_tests cod_native_sustained_aimlab_simulator_tests cod_native_sustained_aimlab_score_tests cod_native_sustained_aimlab_benchmark cod_native_controller_tests cod_native_runtime
```

Expected: build succeeds with no compiler errors.

- [ ] **Step 2: Run focused and regression executables**

Run:

```powershell
& b/Release/cod_native_sustained_aimlab_trace_tests.exe
& b/Release/cod_native_sustained_aimlab_counterfactual_tests.exe
& b/Release/cod_native_sustained_aimlab_simulator_tests.exe
& b/Release/cod_native_sustained_aimlab_score_tests.exe
& b/Release/cod_native_controller_tests.exe
& scripts/verify/native_pipeline_contract.bat
```

Expected: every executable and contract reports PASS.

- [ ] **Step 3: Run the three-seed full benchmark**

Run:

```powershell
& b/Release/cod_native_sustained_aimlab_benchmark.exe `
  --config config.toml --duration-ms 60000 `
  --seed 1337 --seed 20260718 --seed 424242 `
  --profile both --cohort both --counterfactual full `
  --revision (git rev-parse HEAD) `
  --output runs/native_perf/sustained_aimlab_counterfactual_20260719.json
```

Expected: benchmark prints PASS and writes all 12 seed/profile/cohort runs with
nonzero fixed-anchor coverage, explicit dynamic detected/analyzed/skipped counts,
separate causal and hindsight results, and successful replay checks.

- [ ] **Step 4: Record evidence without claiming controller improvement**

Create the acceptance note containing the exact revision, config fingerprint,
candidate-set/schema versions, commands, test results, total analysis time, per-run
coverage, worst local-regret episodes, worst future-burden episodes, and the
existing acquisition/tracking/braking metrics. State explicitly that this artifact
establishes measurement capability and does not itself prove improved live feel.

- [ ] **Step 5: Check repository integrity**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; only the intended acceptance note and artifact
remain uncommitted.

- [ ] **Step 6: Commit the verified baseline**

```powershell
git add docs/project/COUNTERFACTUAL_CONFLICT_BENCHMARK_ACCEPTANCE_20260719.md
git add -f runs/native_perf/sustained_aimlab_counterfactual_20260719.json
git commit -m "bench: baseline counterfactual aim conflicts"
```

## Completion Boundary

This plan is complete when Slice 1 produces deterministic native-controller
counterfactual metrics and a recorded three-seed artifact while the production
runtime remains unchanged. Multi-target intended-route planning is deliberately
left for the separately specified Slice 2 after Slice 1 evidence shows which
selection and handoff failures dominate.
