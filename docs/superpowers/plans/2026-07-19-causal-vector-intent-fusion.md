# Causal Vector Intent Fusion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace independent X/Y manual attenuation with one benchmark-proven causal two-dimensional user/AI fuser without weakening manual escape, ADS Brake boundaries, or output smoothness.

**Architecture:** A focused `VectorIntentFuser` evaluates six fixed manual/AI mixes against the causal `TargetPlan` horizon, selects a candidate with ownership and smoothness costs, and slews two scalar weights. It first runs behind a benchmark-only legacy/vector switch; production wiring and axis-arbiter deletion occur only after the fixed acceptance gates pass.

**Tech Stack:** C++20, CMake/MSBuild, native controller pipeline, deterministic sustained AimLab benchmark, PowerShell verification scripts.

---

## File map

- Create `native/controller_native/vector_intent_fuser.h`: public input, decision, diagnostics, tuning constants, and bounded state.
- Create `native/controller_native/vector_intent_fuser.cpp`: candidate generation, causal prediction, cost selection, fallback, and weight transition.
- Create `native/controller_native/vector_intent_fuser_tests.cpp`: pure component regression tests.
- Modify `native/controller_native/native_gamepad_controller.h/.cpp`: temporary benchmark experiment switch, then the single production fusion point.
- Modify `native/controller_native/output_mixer.h`: vector-fusion diagnostics; remove retired axis fields after acceptance.
- Modify `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`: `--intent-fusion legacy|vector`, JSON identity, selection counters, and comparison runs.
- Modify `native/controller_native/runtime_config.h/.cpp` and `native/controller_native/runtime_config_tests.cpp`: final two-key configuration and retired-key removal.
- Modify `native/vision_native/CMakeLists.txt`: focused test target and production/benchmark sources.
- Modify `scripts/verify/compare_sustained_aimlab.ps1`: reject mismatched fusion identities and print fusion deltas.
- Modify `docs/benchmarks/sustained-aimlab.md`: reproducible experiment and acceptance commands.
- Delete `native/controller_native/axis_intent_arbiter.h/.cpp/.tests.cpp` only after the vector candidate passes all gates.

Before running build steps in this Windows worktree, define the configured CMake executable once:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
```

### Task 1: Component contract and vector geometry

**Files:**
- Create: `native/controller_native/vector_intent_fuser.h`
- Create: `native/controller_native/vector_intent_fuser.cpp`
- Create: `native/controller_native/vector_intent_fuser_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing geometry and candidate tests**

Define fixtures using `pipeline_contract::TargetPlan` and assert aligned, opposing, and orthogonal vectors remain two-dimensional decisions:

```cpp
void test_aligned_input_keeps_existing_mix();
void test_opposing_small_manual_can_choose_ai_supported();
void test_orthogonal_manual_is_not_reduced_by_axis_projection();
void test_candidate_outputs_match_version_one_scales();
```

The candidate-scale test must assert the six exact `(manual_weight, ai_weight)` pairs from the design.

- [ ] **Step 2: Add and run the focused test target to prove RED**

Add `cod_native_vector_intent_fuser_tests` with the new test and implementation source to `native/vision_native/CMakeLists.txt`.

Run:

```powershell
& $cmake --build b --config Release --target cod_native_vector_intent_fuser_tests
```

Expected: build failure because `vector_intent_fuser.h` and its types do not exist.

- [ ] **Step 3: Add the minimal public contract**

Define these stable concepts in the header:

```cpp
enum class FusionCandidate : unsigned char {
    ExistingMix, ManualSupported, AiSupported,
    ManualOnly, AiOnly, ReducedMix,
};

struct VectorIntentFusionConfig {
    float manual_escape_threshold = 0.45f;
    float weight_transition_ms = 24.0f;
};

struct VectorIntentFusionInput {
    pipeline_contract::Vec2f manual_stick{};
    pipeline_contract::Vec2f shaped_ai_stick{};
    pipeline_contract::TargetPlan plan{};
};

struct VectorIntentFusionDecision {
    pipeline_contract::Vec2f fused_stick{};
    FusionCandidate candidate = FusionCandidate::ManualOnly;
    float target_manual_weight = 1.0f;
    float target_ai_weight = 0.0f;
    float applied_manual_weight = 1.0f;
    float applied_ai_weight = 0.0f;
    float winner_margin = 0.0f;
    bool fallback = true;
    bool manual_escape = false;
};

class VectorIntentFuser {
public:
    explicit VectorIntentFuser(VectorIntentFusionConfig config = {});
    VectorIntentFusionDecision update(
        const VectorIntentFusionInput& input, float dt_seconds) noexcept;
    void reset() noexcept;
};
```

- [ ] **Step 4: Implement fixed candidate generation and vector helpers**

Keep candidate scales in one `constexpr std::array`; implement only finite checks, vector add/scale/dot/length, and exact candidate outputs required by the tests. Do not add prediction or learning.

- [ ] **Step 5: Run focused tests to prove GREEN**

Run the test executable and expect `cod_native_vector_intent_fuser_tests PASS`.

- [ ] **Step 6: Commit the component contract**

```powershell
git add native/controller_native/vector_intent_fuser.* native/controller_native/vector_intent_fuser_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "feat: define causal vector intent fusion"
```

### Task 2: Causal multi-horizon scoring

**Files:**
- Modify: `native/controller_native/vector_intent_fuser.h`
- Modify: `native/controller_native/vector_intent_fuser.cpp`
- Modify: `native/controller_native/vector_intent_fuser_tests.cpp`

- [ ] **Step 1: Write failing cost-selection tests**

Add deterministic fixtures for:

```cpp
void test_short_local_gain_loses_to_lower_160ms_burden();
void test_left_motion_adjusted_error_rate_changes_winner();
void test_center_cross_continued_push_is_penalized();
void test_near_equal_cost_keeps_previous_candidate();
```

The first fixture must make `ExistingMix` best at 40 ms but `ManualSupported` best over 40/80/160 ms. The left-motion fixture sets `plan.error_rate_px_per_sec` to the already adjusted value and proves the fuser does not apply `left_x` a second time.

- [ ] **Step 2: Run tests and confirm the first new assertion fails**

Expected: the minimal Task 1 selector cannot distinguish future burden.

- [ ] **Step 3: Implement one causal predictor**

For each candidate and horizon, compute predicted error from `plan.horizon` when present; otherwise use bounded constant error rate. Apply right-stick camera response once:

```text
predicted_error(t) = causal_plan_error(t)
                   - candidate_output * response_scale * t
```

Use the existing controller coordinate convention and cover it with signed X/Y tests. Never read realized future trace data.

- [ ] **Step 4: Implement version-one cost and tie-break**

Return a breakdown containing integrated error, terminal error, cross/push penalty, reversal penalty, output-change penalty, and ownership loss. When total costs are within the fixed selection margin, prefer the previous candidate and then greater deliberate manual retention.

- [ ] **Step 5: Run focused tests and confirm PASS**

- [ ] **Step 6: Commit causal selection**

```powershell
git add native/controller_native/vector_intent_fuser.* native/controller_native/vector_intent_fuser_tests.cpp
git commit -m "feat: score causal intent fusion candidates"
```

### Task 3: Escape, fallback, and smooth weight state

**Files:**
- Modify: `native/controller_native/vector_intent_fuser.h`
- Modify: `native/controller_native/vector_intent_fuser.cpp`
- Modify: `native/controller_native/vector_intent_fuser_tests.cpp`

- [ ] **Step 1: Write failing safety tests**

Add tests for diagonal escape, missing target, target-ID change, `Reacquiring`, low reliability, low response confidence, non-finite plan/input, same-target mode handoff, and 24 ms weight transition.

The non-finite test must require exact physical manual output. The same-target ADS-to-BodyLock test must require continuous applied weights. The target-change test must preserve output continuity while preventing new manual attenuation.

- [ ] **Step 2: Run the focused test and confirm RED**

- [ ] **Step 3: Implement eligibility and fallback in one function**

Eligibility returns a reason enum. Ineligible decisions target manual weight `1.0` and AI weight `0.0`; non-finite computation immediately returns physical manual. A deliberate escape disallows AI-owned and manual-attenuating candidates.

- [ ] **Step 4: Implement the only persistent state**

Store current manual/AI weights, previous fused output, previous candidate, and target ID. Apply a bounded linear/exponential approach derived only from `weight_transition_ms`; add no hold timer.

- [ ] **Step 5: Run focused tests and confirm PASS**

- [ ] **Step 6: Commit safety and smoothness**

```powershell
git add native/controller_native/vector_intent_fuser.* native/controller_native/vector_intent_fuser_tests.cpp
git commit -m "feat: bound vector fusion ownership transitions"
```

### Task 4: Benchmark-only controller experiment

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/output_mixer.h`
- Modify: `native/controller_native/target_pipeline_integration_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write a failing integration test for single application**

Under the existing benchmark compile definition, select vector mode and assert:

- original right-stick intent reaches ADS/BodyLock computation;
- no per-axis confidence is cleared;
- the fuser receives physical manual and shaped AI once;
- final pre-brake output equals the reported fused output;
- BodyLock never reports ADS Brake active.

- [ ] **Step 2: Run `cod_native_controller_tests` and confirm RED**

- [ ] **Step 3: Add a temporary benchmark experiment mode**

Under `COD_BENCHMARK_MIX_OVERRIDE`, add `LegacyAxis` and `CausalVector` selection with `LegacyAxis` as the default. In vector mode bypass `AxisIntentArbiter`, pass original intent through the controllers/Dynamics, then call `VectorIntentFuser` once before the existing ADS Brake path.

- [ ] **Step 4: Add compact fusion diagnostics**

Report candidate, target/applied weights, winner margin, and fallback reason in `NativeControllerOutputComponents`. Do not add production per-tick file logging.

- [ ] **Step 5: Build and run controller plus pipeline contract tests**

Expected: controller tests and `native_pipeline_contract.ps1 -BuildDir b -SkipBuild -SkipBenchmark` pass in legacy default mode.

- [ ] **Step 6: Commit experiment wiring**

```powershell
git add native/controller_native/native_gamepad_controller.* native/controller_native/output_mixer.h native/controller_native/target_pipeline_integration_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "bench: expose causal vector fusion experiment"
```

### Task 5: Benchmark identity, metrics, and fixed comparison

**Files:**
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `scripts/verify/compare_sustained_aimlab.ps1`
- Modify: `docs/benchmarks/sustained-aimlab.md`

- [ ] **Step 1: Write failing CLI/schema checks**

Exercise `--intent-fusion legacy`, `--intent-fusion vector`, and an invalid value. Require top-level fusion mode/version and per-run candidate counts, fallback count, escape count, and mean applied weights.

- [ ] **Step 2: Run a 1200 ms smoke benchmark and confirm the missing option fails**

- [ ] **Step 3: Implement CLI and JSON identity**

The benchmark must set the controller experiment mode explicitly and serialize it. The comparison script must refuse artifacts with missing or unintended fusion identities.

- [ ] **Step 4: Add fusion metrics without changing existing scores**

Accumulate diagnostics from controller outputs; do not alter target generation or scoring definitions.

- [ ] **Step 5: Verify legacy identity preservation**

Run the fixed seeds in legacy mode and compare against `runs/native_perf/sustained_aimlab_counterfactual_20260719.json`. Expected: all existing core and counterfactual deltas are zero.

- [ ] **Step 6: Commit benchmark support**

```powershell
git add native/controller_native/cod_native_sustained_aimlab_benchmark.cpp scripts/verify/compare_sustained_aimlab.ps1 docs/benchmarks/sustained-aimlab.md
git commit -m "bench: compare causal vector intent fusion"
```

### Task 6: Tune versioned constants against hard gates

**Files:**
- Modify: `native/controller_native/vector_intent_fuser.cpp`
- Modify: `native/controller_native/vector_intent_fuser_tests.cpp`
- Create: `docs/project/CAUSAL_VECTOR_FUSION_ACCEPTANCE_20260719.md`

- [ ] **Step 1: Run the untuned full vector candidate**

Use the three fixed seeds, both profiles/cohorts, 60 seconds, counterfactual full, and save a revision/config-identified artifact.

- [ ] **Step 2: Classify failures by cost component**

For every failed gate, inspect candidate counts and worst episodes. Change only one versioned cost constant per experiment and record the hypothesis; do not expose cost weights in TOML.

- [ ] **Step 3: Add a regression fixture for each confirmed defect before changing the constant**

Each new fixture must fail under the old constant and pass under the proposed value.

- [ ] **Step 4: Re-run focused tests and the full vector benchmark**

Stop tuning if a change improves mixed totals by sacrificing a pure, escape, smoothness, lifecycle, or post-cross hard gate.

- [ ] **Step 5: Record the accepted result or rejection honestly**

The acceptance note lists every hard gate, baseline, candidate, delta, seed, revision, config fingerprint, and command. If any gate fails, production migration Tasks 7-8 remain blocked and legacy production stays unchanged.

- [ ] **Step 6: Commit only a gate-passing analytical candidate and evidence**

```powershell
git add native/controller_native/vector_intent_fuser.cpp native/controller_native/vector_intent_fuser_tests.cpp docs/project/CAUSAL_VECTOR_FUSION_ACCEPTANCE_20260719.md
git add -f runs/native_perf/sustained_aimlab_vector_fusion_20260719.json
git commit -m "bench: accept causal vector intent fusion"
```

### Task 7: Production configuration and migration

**Blocked by:** Task 6 passing every hard gate.

**Files:**
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: checked-in TOML examples located by `git ls-files '*.toml'`

- [ ] **Step 1: Write failing parser/default/range tests**

Require defaults `0.45` and `24`, parse `[gamepad.intent_fusion]`, reject/clamp invalid finite ranges according to current parser policy, and prove retired `wrong_way_manual_preservation_floor` is no longer applied.

- [ ] **Step 2: Run runtime-config tests and confirm RED**

- [ ] **Step 3: Add `GamepadIntentFusionConfig` and parser**

```cpp
struct GamepadIntentFusionConfig {
    float manual_escape_threshold = 0.45f;
    float weight_transition_ms = 24.0f;
};
```

Remove `GamepadIntentConfig::wrong_way_manual_preservation_floor`; move the effective escape value from the old BodyLock-specific key into the new section and update examples/benchmark readers in the same commit.

- [ ] **Step 4: Run config and benchmark smoke tests**

- [ ] **Step 5: Commit configuration migration**

```powershell
git add native/controller_native/runtime_config.* native/controller_native/runtime_config_tests.cpp '*.toml'
git commit -m "config: migrate vector intent fusion controls"
```

### Task 8: Replace legacy arbiter and verify production

**Blocked by:** Tasks 6 and 7.

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/output_mixer.h`
- Modify: `native/vision_native/CMakeLists.txt`
- Delete: `native/controller_native/axis_intent_arbiter.h`
- Delete: `native/controller_native/axis_intent_arbiter.cpp`
- Delete: `native/controller_native/axis_intent_arbiter_tests.cpp`
- Modify: affected controller/pipeline tests

- [ ] **Step 1: Make vector mode the production path and remove the experiment branch**

Construct `VectorIntentFuser` from runtime config. Remove the benchmark legacy/vector controller conditional after preserving legacy benchmark artifacts as historical evidence.

- [ ] **Step 2: Delete retired axis wiring and diagnostic fields**

Remove confidence-zeroing, per-axis retention multiplication, axis state resets, CMake target/source entries, and output component fields.

- [ ] **Step 3: Build every affected Release target**

Build vector fuser tests, controller tests, sustained simulator/trace/counterfactual/score tests, benchmark, and runtime.

- [ ] **Step 4: Run the complete affected test set and pipeline contract**

Expected: all executables exit zero and print PASS; runtime smoke initializes with ADS Brake still ADS-only.

- [ ] **Step 5: Re-run the accepted full benchmark twice**

Expected: fixed-seed artifacts are identical for core scores and fusion/counterfactual metrics.

- [ ] **Step 6: Check repository hygiene and commit production replacement**

Run `git diff --check`, inspect `git status --short`, and commit only intended source, tests, config, docs, and accepted artifact.

```powershell
git commit -m "feat: replace axis arbitration with causal vector fusion"
```

### Task 9: Bounded within-target residual learner

**Blocked by:** Accepted analytical fuser from Task 8.

**Files:**
- Create: `native/controller_native/fusion_residual_learner.h`
- Create: `native/controller_native/fusion_residual_learner.cpp`
- Create: `native/controller_native/fusion_residual_learner_tests.cpp`
- Modify: `native/controller_native/vector_intent_fuser.h/.cpp`
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing bounded-learning tests**

Require coarse context bucketing, zero output below the minimum sample count, slow EMA updates, decay after a material response-scale change, process-reset empty state, and a correction cap of 15% of analytical cost.

- [ ] **Step 2: Run the focused learner test and confirm RED**

- [ ] **Step 3: Define the learner-only cost interface**

```cpp
struct FusionLearningContext {
    unsigned char mode_band = 0;
    unsigned char error_band = 0;
    unsigned char closing_speed_band = 0;
    unsigned char motion_band = 0;
    unsigned char relationship_band = 0;
    unsigned char size_reliability_band = 0;
    unsigned char response_band = 0;
};

struct FusionLearningSample {
    FusionLearningContext context{};
    FusionCandidate candidate = FusionCandidate::ManualOnly;
    float predicted_cost = 0.0f;
    float realized_cost = 0.0f;
    float response_scale = 0.0f;
};

struct FusionResidualEstimate {
    float cost_residual = 0.0f;
    float confidence = 0.0f;
};

class FusionResidualLearner {
public:
    FusionResidualEstimate estimate(
        const FusionLearningContext&, FusionCandidate) const noexcept;
    void observe(const FusionLearningSample&) noexcept;
    void reset() noexcept;
};
```

The learner cannot return stick output or alter escape/fallback eligibility.

- [ ] **Step 4: Implement delayed 160-500 ms observation**

Update only after an eligible same-target outcome closes. Add the bounded residual to analytical candidate cost behind a benchmark mode `analytical+learning`; keep `analytical` independently selectable.

- [ ] **Step 5: Run analytical-only versus warm-learning long benchmarks**

Reject learning unless it improves mixed long-run causal regret/future burden while every analytical fuser guardrail remains satisfied.

- [ ] **Step 6: Commit only if the independent learning gate passes**

```powershell
git add native/controller_native/fusion_residual_learner* native/controller_native/vector_intent_fuser.* native/controller_native/cod_native_sustained_aimlab_benchmark.cpp native/vision_native/CMakeLists.txt
git commit -m "feat: learn bounded intent fusion residuals"
```

### Task 10: Preserve the next learning tracks

**Files:**
- Reference: `docs/superpowers/plans/2026-07-19-global-aim-policy-learning.md`
- Modify: `.agent-context/` only through the project's explicit context-sync workflow

- [ ] **Step 1: Confirm analytical fusion completion does not close residual/global learning**

The handoff must list bounded within-target residual learning and cross-target G0-G4 as pending tracks with their acceptance gates.

- [ ] **Step 2: Propose a context SyncSet**

Include the accepted fusion revision/artifact, rejected experiments, configuration identity, and the exact next plan path. Do not write project context without user confirmation.
