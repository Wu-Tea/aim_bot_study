# Per-Axis Intent Arbitration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one X/Y-independent manual/AI arbiter that reduces wrong-direction overshoot while preserving normal ADS force, unaffected-axis behavior, BodyLock smoothness, and per-axis manual escape.

**Architecture:** First make the partial-occlusion benchmark produce realistic temporal input errors and per-case metrics. Then pass existing axis confidence through the active target-plan path, implement a pure `AxisIntentArbiter`, and place it between mode controllers and the dynamics shaper. Remove duplicate manual attenuation from ADS, BodyLock, and the shaper so arbitration occurs exactly once.

**Tech Stack:** C++17, CMake/Visual Studio 2022, native controller pipeline, deterministic JSON benchmarks.

---

### Task 1: Realistic per-case partial-occlusion benchmark

**Files:**
- Modify: `native/controller_native/partial_occlusion_benchmark.h`
- Modify: `native/controller_native/partial_occlusion_benchmark.cpp`
- Modify: `native/controller_native/partial_occlusion_benchmark_tests.cpp`
- Modify: `native/controller_native/cod_native_partial_occlusion_benchmark.cpp`

- [ ] **Step 1: Add failing schedule and report tests**

Add tests proving that stale input holds a historical vector, wrong-X preserves correct Y,
wrong-Y preserves correct X, crossing inertia decays rather than snapping, and JSON contains
one report per case:

```cpp
void test_error_profiles_are_axis_isolated_and_temporal() {
    const auto scenario = build_scenario(ScenarioKind::HumanErrors, 1337);
    const auto wrong_x = sample_manual_profile(scenario.cases[1], {30.0, -20.0}, 0.25, 40);
    require(wrong_x.x < 0.0 && wrong_x.y < 0.0,
            "wrong-X must reverse X while preserving the correct Y sign");
    const auto late = sample_manual_profile(scenario.cases[1], {30.0, -20.0}, 0.25, 180);
    require(std::fabs(late.x) < std::fabs(wrong_x.x),
            "wrong-X error must decay into correction");
}

void test_json_contains_per_case_metrics() {
    ScenarioReport report;
    report.name = "partial_occlusion_human_errors";
    report.cases.push_back(CaseReport{"wrong_x", {}, {}});
    const auto json = render_report_json({}, {report});
    require(json.find("\"cases\"") != std::string::npos, "case array");
    require(json.find("\"wrong_x\"") != std::string::npos, "case name");
}
```

- [ ] **Step 2: Build and verify the new tests fail**

Run:

```powershell
cmake --build D:\codex-build\pob --config Release --target cod_native_partial_occlusion_benchmark_tests
D:\codex-build\pob\Release\cod_native_partial_occlusion_benchmark_tests.exe
```

Expected: compile failure for the new profile/report interfaces or test failure because the
current profiles mirror the ideal vector and reports are aggregate-only.

- [ ] **Step 3: Implement deterministic temporal profiles and case reports**

Add pure helpers and report types:

```cpp
struct ManualProfileSample {
    double x = 0.0;
    double y = 0.0;
};

struct CaseReport {
    std::string name;
    PartialOcclusionMetrics metrics{};
    PartialOcclusionScore score{};
};

ManualProfileSample sample_manual_profile(
    const ScenarioCase& value,
    ManualProfileSample historical,
    ManualProfileSample ideal,
    int elapsed_in_error_ms) noexcept;
```

Use linear/smoothstep decay from historical or wrong-axis input into ideal input. Preserve
the unaffected axis exactly for wrong-X/wrong-Y. Change `StaleDirection` from current-vector
inversion to historical-vector hold and decay. Aggregate case traces independently before
adding them to the scenario total.

- [ ] **Step 4: Run focused tests and record the corrected pre-runtime baseline**

Run the test executable, then:

```powershell
D:\codex-build\pob\Release\cod_native_partial_occlusion_benchmark.exe `
  --config D:\work\AI\yolo-study-001\config.toml `
  --seed 1337 `
  --output D:\work\AI\yolo-study-001\runs\benchmarks\axis_arbiter_prechange_seed1337.json
```

Expected: tests pass; JSON contains two scenario totals and named per-case raw metrics/scores.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/partial_occlusion_benchmark.* `
  native/controller_native/partial_occlusion_benchmark_tests.cpp `
  native/controller_native/cod_native_partial_occlusion_benchmark.cpp
git commit -m "test: model temporal per-axis input errors"
```

### Task 2: Make intent confidence truly per-axis

**Files:**
- Modify: `native/controller_native/intent_filter_tests.cpp`
- Modify: `native/controller_native/ads_acquisition_controller_tests.cpp`
- Modify: `native/controller_native/bodylock_follow_controller_tests.cpp`
- Modify: `native/controller_native/aim_dynamics_shaper_tests.cpp`
- Modify: `native/controller_native/ads_acquisition_controller.cpp`
- Modify: `native/controller_native/bodylock_follow_controller.cpp`
- Modify: `native/controller_native/aim_dynamics_shaper.cpp`

- [ ] **Step 1: Write failing cross-axis isolation tests**

Create an intent with strong X confidence and zero Y confidence. Verify X manual input cannot
attenuate Y output, then mirror the test for Y:

```cpp
pipeline_contract::IntentState intent{};
intent.filtered_right = {0.8f, 0.0f};
intent.right_x.confidence = 1.0f;
intent.right_y.confidence = 0.0f;
intent.right_confidence = 1.0f;
const auto output = controller.compute(plan_with_xy_error(), intent, 0.001f);
require_near(output.y, neutral_output.y, 1e-5f,
             "X confidence must not attenuate Y assist");
```

- [ ] **Step 2: Run tests and verify cross-axis failure**

Build and run the focused ADS, BodyLock, intent-filter, and dynamics test executables.
Expected: at least the cross-axis controller/shaper cases fail because they use shared
`right_confidence`.

- [ ] **Step 3: Replace shared confidence consumers with axis confidence**

Pass `intent.right_x.confidence` to X calculations and `intent.right_y.confidence` to Y.
Keep aggregate `right_confidence` only for compatibility consumers that make whole-stick
decisions. Do not change force curves in this task.

- [ ] **Step 4: Run focused tests**

Expected: cross-axis tests and all existing focused tests pass.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/intent_filter_tests.cpp `
  native/controller_native/ads_acquisition_controller* `
  native/controller_native/bodylock_follow_controller* `
  native/controller_native/aim_dynamics_shaper*
git commit -m "fix: isolate controller intent confidence by axis"
```

### Task 3: Implement the pure per-axis arbiter

**Files:**
- Create: `native/controller_native/axis_intent_arbiter.h`
- Create: `native/controller_native/axis_intent_arbiter.cpp`
- Create: `native/controller_native/axis_intent_arbiter_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add failing unit tests for the policy boundary**

Cover drift, helpful residual allocation, probable wrong-way compensation, observed stable
crossing limit, coasting no-manual-limit, geometry jump no-manual-limit, per-axis escape,
target-change reset, and smooth risk release:

```cpp
void test_stable_observed_crossing_limits_only_wrong_way_axis();
void test_coasting_never_limits_manual_output();
void test_geometry_jump_disables_final_output_limit();
void test_manual_escape_bypasses_one_axis_only();
void test_helpful_manual_reduces_stacking_without_removing_far_ads_floor();
void test_target_change_clears_crossing_evidence();
```

- [ ] **Step 2: Build and verify failure**

Expected: compile failure because `AxisIntentArbiter` does not exist.

- [ ] **Step 3: Implement the minimal pure API**

```cpp
enum class AxisDecisionReason : unsigned char {
    Neutral,
    HelpfulResidual,
    ProbableWrongWay,
    CrossingLimit,
    EvidenceAmbiguous,
    ManualEscape,
};

struct AxisIntentInput {
    float error = 0.0f;
    float error_rate = 0.0f;
    float requested_assist = 0.0f;
    float manual = 0.0f;
    float manual_confidence = 0.0f;
    float reliability = 0.0f;
    float target_innovation_px = 0.0f;
    float normalized_size_change = 0.0f;
    float manual_escape_threshold = 0.45f;
    std::uint64_t target_id = 0;
    pipeline_contract::TargetLifecycle lifecycle = pipeline_contract::TargetLifecycle::None;
    pipeline_contract::ControlMode mode = pipeline_contract::ControlMode::Manual;
};

struct AxisDecision {
    float assist_output = 0.0f;
    float assist_scale = 1.0f;
    float wrong_way_budget = 1.0f;
    float divergence_risk = 0.0f;
    AxisDecisionReason reason = AxisDecisionReason::Neutral;
};
```

Store only previous error, smoothed risk, target id, and initialization state per axis.
Use continuous fast-attack/slow-release risk. Never limit final manual output for
`Coasting`, target/geometry ambiguity, or manual escape. Return bounded assist; expose the
wrong-way budget for the mixer integration in Task 4.

- [ ] **Step 4: Run pure tests until all pass**

Run `cod_native_axis_intent_arbiter_tests.exe` and verify all policy-boundary cases pass.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/axis_intent_arbiter* native/vision_native/CMakeLists.txt
git commit -m "feat: add per-axis intent arbiter"
```

### Task 4: Integrate once and remove duplicate attenuation

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/ads_acquisition_controller.cpp`
- Modify: `native/controller_native/bodylock_follow_controller.cpp`
- Modify: `native/controller_native/aim_dynamics_shaper.h`
- Modify: `native/controller_native/aim_dynamics_shaper.cpp`
- Modify: `native/controller_native/output_mixer.h`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add failing controller integration tests**

Add deterministic tests through `NativeGamepadController` for X-wrong/Y-helpful,
Y-wrong/X-helpful, crossing inertia, geometry jump, coasting, and strong escape. Assert:

```cpp
require(std::fabs(candidate.components.shaped_assist_stick.y - baseline_y) < 0.02f,
        "X error must not weaken Y acquisition");
require(candidate.max_wrong_way_x < baseline.max_wrong_way_x,
        "crossing arbiter must reduce wrong-way X output");
require_near(escape.final_x, escape.manual_x, 0.02f,
             "strong X escape must bypass final limiting");
```

- [ ] **Step 2: Run and verify integration tests fail**

Expected: current shared confidence/double attenuation or absent crossing arbiter violates at
least one new assertion.

- [ ] **Step 3: Wire one arbiter between controller request and shaper**

In the active target-plan path:

```cpp
auto requested = mode_controller.compute(plan, intent, dt);
const auto arbitrated = axis_intent_arbiter_.apply(requested, intent, plan, dt);
const auto shaped = dynamics_shaper_.shape(arbitrated.assist, plan, dt);
output.right_x = clamp_unit(physical.right_x + shaped.x);
output.right_y = clamp_unit(physical.right_y + shaped.y);
```

Apply a final wrong-way budget only when the arbiter explicitly permits it. The budget may
reduce wrong-direction net output toward a small nonzero cap; it must not reverse output.
Reset the arbiter with controller reset, ADS epoch change, manual mode, and target change.

- [ ] **Step 4: Remove active-path duplicate attenuation**

Remove manual-opposition scaling from ADS and BodyLock axis functions. Remove cooperative
and opposing manual scaling from `AimDynamicsShaper`; retain lifecycle coast handling and
slew. Update configs/tests so retired internal constants are not exposed as active behavior.

- [ ] **Step 5: Add component diagnostics**

Record X/Y assist scale, risk, budget, reason, and escape bypass in
`NativeControllerOutputComponents`. Keep them in memory/benchmark output; do not enable new
always-on file logging.

- [ ] **Step 6: Run focused and controller integration tests**

Expected: intent, ADS, BodyLock, shaper, arbiter, controller integration, AutoFire, and
output validation tests pass.

- [ ] **Step 7: Commit**

```powershell
git add native/controller_native native/vision_native/CMakeLists.txt
git commit -m "feat: arbitrate manual and assist independently by axis"
```

### Task 5: Benchmark comparison and full verification

**Files:**
- Modify: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`
- Output: `runs/benchmarks/axis_arbiter_candidate_seed1337.json` (ignored runtime artifact)

- [ ] **Step 1: Build all affected Release targets**

Build the partial-occlusion benchmark/tests, arbiter tests, focused controller tests,
`cod_native_controller_tests`, and runtime in the short build directory.

- [ ] **Step 2: Run the candidate benchmark twice**

Use current `config.toml`, seed 1337, and compare hashes of both candidate JSON files.
Expected: identical hashes.

- [ ] **Step 3: Compare pre-change and candidate per-case metrics**

Report normal combat and each classic error case. Prioritize acquisition time, settle
overshoot X/Y, recovery, output delta/spikes, mode changes, and unaffected-axis error.
Reject the candidate if normal far-error force materially falls or axis isolation fails.

- [ ] **Step 4: Run full verification**

Run all focused executables and the native pipeline contract using the short build path.
Run `git diff --check`. If the existing pipeline script still expects the stale test label,
report that infrastructure mismatch separately; do not hide it or change behavior to satisfy
a text-only assertion.

- [ ] **Step 5: Update benchmark documentation and commit**

Document seed, effective config, artifact paths, raw changes, accepted tradeoffs, and any
remaining live-test limitation.

```powershell
git add docs/project/NATIVE_CONTROLLER_BENCHMARKS.md
git commit -m "docs: record per-axis arbitration benchmark"
```

