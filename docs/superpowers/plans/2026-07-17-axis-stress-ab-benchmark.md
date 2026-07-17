# Axis Stress A/B Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic practical and destructive axis-stress suites, then run the identical benchmark against `990946c`, `0710d11`, and `a92ad73`.

**Architecture:** Extend the existing partial-occlusion scenario data with explicit stress parameters and pure deterministic samplers. Keep target truth, submitted vision observations, and manual-error envelopes separate in the harness. Preserve the two historical suites byte-for-byte, emit two additional reports, and compare artifacts with a standalone PowerShell script.

**Tech Stack:** C++20 controller benchmark and tests, CMake/MSBuild Release builds, PowerShell JSON comparison, Git worktrees.

---

## File map

- Modify `native/controller_native/partial_occlusion_benchmark.h`: stress enums, case parameters, audit metrics, and pure sampler declarations.
- Modify `native/controller_native/partial_occlusion_benchmark.cpp`: practical/destructive scenario definitions, deterministic samplers, manual profiles, metric JSON serialization.
- Modify `native/controller_native/partial_occlusion_benchmark_tests.cpp`: scenario-strength, axis-isolation, truth/observation separation, and JSON-contract tests.
- Modify `native/controller_native/cod_native_partial_occlusion_benchmark.cpp`: apply truth velocity disturbance and observation noise separately; collect error-window audit metrics; execute four suites.
- Create `scripts/compare_axis_stress_ab.ps1`: compare matching scenario/case metrics across the three artifacts and emit Markdown plus JSON summaries.

### Task 1: Define and test stress scenario contracts

**Files:**
- Modify: `native/controller_native/partial_occlusion_benchmark_tests.cpp`
- Modify: `native/controller_native/partial_occlusion_benchmark.h`
- Modify: `native/controller_native/partial_occlusion_benchmark.cpp`

- [ ] **Step 1: Write failing schedule tests**

Add tests that build `ScenarioKind::PracticalStress` and `ScenarioKind::DestructiveStress` and assert:

```cpp
expect_true(practical.name == "partial_occlusion_practical_stress",
            "practical stress name");
expect_true(destructive.name == "partial_occlusion_destructive_stress",
            "destructive stress name");
expect_true(practical.cases.size() == 4 && destructive.cases.size() == 4,
            "each stress tier must expose four cases");
expect_true(practical.cases[0].manual_magnitude_cap >= 0.70,
            "practical manual error strength");
expect_true(destructive.cases[0].manual_magnitude_cap >= 0.95,
            "destructive manual error strength");
expect_true(destructive.cases[0].error_hold_ms > practical.cases[0].error_hold_ms,
            "destructive error must last longer");
```

Also assert that both suites contain `WrongX`, `WrongY`, `WrongBoth`, and `MixedAxes`, while the existing Combat/HumanErrors definitions retain their current case counts and parameters.

- [ ] **Step 2: Run the test and verify RED**

Run:

```powershell
cmake --build D:\codex-build\pob --config Release --target cod_native_partial_occlusion_benchmark_tests
D:\codex-build\pob\Release\cod_native_partial_occlusion_benchmark_tests.exe
```

Expected: compile failure because the new enum values and scenario fields do not exist.

- [ ] **Step 3: Add the minimal scenario data model**

Add:

```cpp
enum class ScenarioKind { Combat, HumanErrors, PracticalStress, DestructiveStress };
enum class HumanErrorKind {
    None, StaleDirection, WrongX, WrongY, CrossingInertia, WrongBoth, MixedAxes
};
enum class StressTier { None, Practical, Destructive };

struct ScenarioCase {
    // existing fields remain unchanged
    StressTier stress_tier = StressTier::None;
    double truth_motion_amplitude_x_px_per_sec = 0.0;
    double truth_motion_amplitude_y_px_per_sec = 0.0;
    double observation_jitter_x_px = 0.0;
    double observation_jitter_y_px = 0.0;
    double observation_jump_px = 0.0;
};
```

Build four deterministic cases per stress tier. Practical cases use `manual_magnitude_cap=0.70` and `error_hold_ms=180`; destructive cases use `0.95` and `320`. Set `error_onset_ms=70` so the initial confirmation happens with observed vision, and use an observation gap that overlaps the latter part of at least one destructive error window.

- [ ] **Step 4: Extend manual profiles for dual-axis cases**

Implement `WrongBoth` by reversing both ideal axes. Implement `MixedAxes` so X is wrong while Y stays ideal during the first half of the hold, then both axes are wrong during the second half. Keep the existing attack/release interpolation so neither case introduces a one-tick manual jump.

- [ ] **Step 5: Run tests and verify GREEN**

Expected: partial-occlusion unit tests pass and existing schedule assertions remain unchanged.

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native/partial_occlusion_benchmark.h native/controller_native/partial_occlusion_benchmark.cpp native/controller_native/partial_occlusion_benchmark_tests.cpp
git commit -m "test: define practical and destructive axis stress suites"
```

### Task 2: Add deterministic truth and observation samplers

**Files:**
- Modify: `native/controller_native/partial_occlusion_benchmark_tests.cpp`
- Modify: `native/controller_native/partial_occlusion_benchmark.h`
- Modify: `native/controller_native/partial_occlusion_benchmark.cpp`

- [ ] **Step 1: Write failing sampler tests**

Declare the wished-for API in tests:

```cpp
const auto truth = sample_truth_velocity_disturbance(value, 1337, 95);
const auto vision = sample_observation_disturbance(value, 1337, 95);
expect_true(std::hypot(truth.x, truth.y) > 0.0,
            "stress truth must contain non-linear motion");
expect_true(std::hypot(vision.x, vision.y) > 0.0,
            "stress vision must contain measurement noise");
```

Verify same seed/tick equality, different seed inequality, practical bounds (`<=20 px` observation offset), destructive transient reach (`>=40 px` at a known generated tick), and zero output for all baseline cases. Compute `true_error` once, add observation disturbance to a copy, and assert the original truth value is unchanged.

- [ ] **Step 2: Run and verify RED**

Expected: compile failure because sampler functions and `DisturbanceSample` are absent.

- [ ] **Step 3: Implement pure deterministic samplers**

Add:

```cpp
struct DisturbanceSample { double x = 0.0; double y = 0.0; };

DisturbanceSample sample_truth_velocity_disturbance(
    const ScenarioCase&, std::uint32_t seed, int elapsed_ms) noexcept;
DisturbanceSample sample_observation_disturbance(
    const ScenarioCase&, std::uint32_t seed, int elapsed_ms) noexcept;
```

Use sums of fixed-frequency sine components whose phases derive from `seed` and `case.index`. Add deterministic reversal windows to truth velocity. Add sparse, short triangular pulses for observation jumps. Clamp practical observation magnitude to `20 px` and destructive magnitude to `55 px`. Do not allocate or keep mutable random state.

- [ ] **Step 4: Run tests and verify GREEN**

Expected: deterministic sampler tests and all prior partial-occlusion tests pass.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/partial_occlusion_benchmark.h native/controller_native/partial_occlusion_benchmark.cpp native/controller_native/partial_occlusion_benchmark_tests.cpp
git commit -m "feat: generate deterministic target and vision stress"
```

### Task 3: Collect auditable stress metrics in the production-path harness

**Files:**
- Modify: `native/controller_native/partial_occlusion_benchmark_tests.cpp`
- Modify: `native/controller_native/partial_occlusion_benchmark.h`
- Modify: `native/controller_native/partial_occlusion_benchmark.cpp`
- Modify: `native/controller_native/cod_native_partial_occlusion_benchmark.cpp`

- [ ] **Step 1: Write failing JSON contract test**

Populate new metric fields with recognizable values and assert JSON keys exist:

```cpp
report.metrics.error_window_mean_px = 31.25;
report.metrics.error_window_p95_px = 48.50;
report.metrics.error_window_peak_px = 72.00;
report.metrics.error_window_recovery_ms = 93.00;
report.metrics.max_observation_offset_px = 20.00;
report.metrics.peak_manual_error_x = 0.70;
report.metrics.peak_manual_error_y = 0.65;
report.metrics.manual_error_active_frames = 180;
```

Assert each exact JSON property is serialized.

- [ ] **Step 2: Run and verify RED**

Expected: compile failure because audit metrics are absent.

- [ ] **Step 3: Add metric fields and serialization**

Extend `PartialOcclusionMetrics` with the fields above. Serialize them in `write_metrics_json`, take maxima for peak fields, sum active frames, and combine error-window samples through `CaseTrace` rather than averaging already-averaged case metrics.

- [ ] **Step 4: Apply stress in the harness**

At every 1 kHz tick, add `sample_truth_velocity_disturbance` to target velocity before updating truth. At every submitted 100 Hz observation, add `sample_observation_disturbance` only to the `true_error` copy passed to `observed_state`. Record its magnitude in `max_observation_offset_px`.

Track the injected-error window from `error_onset_ms` through `error_hold_ms`, plus recovery until radial error remains under `20 px` for `20 ms`. Record manual peaks from the actual physical right-stick values and keep X/Y separate.

- [ ] **Step 5: Execute all four suites**

Add PracticalStress and DestructiveStress to the `reports` vector. Append the case index to dual-axis names if needed so every case name is unique and stable.

- [ ] **Step 6: Run unit and executable verification**

Run the unit target, then:

```powershell
D:\codex-build\pob\Release\cod_native_partial_occlusion_benchmark.exe `
  --config D:\work\AI\yolo-study-001\config.toml `
  --output D:\work\AI\yolo-study-001\runs\benchmarks\axis_stress_current_seed1337.json `
  --seed 1337
```

Expected: four named reports; practical manual peaks near `0.70`; destructive peaks near `0.95`; destructive observation offset and error-window metrics exceed practical values; the two baseline reports match the pre-task artifact byte-for-byte at their scenario-object level.

- [ ] **Step 7: Commit**

```powershell
git add native/controller_native/partial_occlusion_benchmark.h native/controller_native/partial_occlusion_benchmark.cpp native/controller_native/partial_occlusion_benchmark_tests.cpp native/controller_native/cod_native_partial_occlusion_benchmark.cpp
git commit -m "feat: benchmark high-jitter axis input failures"
```

### Task 4: Add deterministic three-version comparison

**Files:**
- Create: `scripts/compare_axis_stress_ab.ps1`
- Create: `tests/test_compare_axis_stress_ab.ps1`

- [ ] **Step 1: Write a failing script test**

Create three minimal fixture JSON objects in `$TestDrive`, invoke the comparison script, and assert the Markdown contains both required comparisons and per-case mean/P95/delta values:

```powershell
$markdown | Should -Match '0710d11 -> a92ad73'
$markdown | Should -Match '990946c -> a92ad73'
$markdown | Should -Match 'wrong_x'
$summary.comparisons.Count | Should -Be 2
```

- [ ] **Step 2: Run and verify RED**

Run:

```powershell
Invoke-Pester tests/test_compare_axis_stress_ab.ps1 -Output Detailed
```

Expected: failure because `scripts/compare_axis_stress_ab.ps1` does not exist.

- [ ] **Step 3: Implement the comparison script**

Accept `-PreAxisPath`, `-PreCoastRisePath`, `-CurrentPath`, `-MarkdownOutput`, and `-JsonOutput`. Match scenarios and cases by name. Emit absolute and percentage changes for mean error, P95 error, peak error, error-window mean/P95/recovery, output delta, spikes, X/Y interventions, and X/Y overshoot. Print `n/a` rather than dividing by zero.

- [ ] **Step 4: Run and verify GREEN**

Expected: Pester passes and both outputs parse successfully.

- [ ] **Step 5: Commit**

```powershell
git add scripts/compare_axis_stress_ab.ps1 tests/test_compare_axis_stress_ab.ps1
git commit -m "tool: compare axis stress benchmark revisions"
```

### Task 5: Run identical A/B builds and report evidence

**Files:**
- Create artifacts under `runs/benchmarks/` (ignored runtime output)
- Update: `docs/benchmarks/axis-stress-ab-seed1337.md`

- [ ] **Step 1: Record the benchmark-only commits**

Use `git log -- native/controller_native/partial_occlusion_benchmark.cpp` and record the Task 1-4 commit hashes. Do not include `a92ad73` when applying the harness to old revisions.

- [ ] **Step 2: Create two temporary worktrees**

Create isolated worktrees at `990946c` and `0710d11`, verify their resolved paths stay within `.worktrees/`, then cherry-pick only the benchmark and comparison commits. Resolve compatibility differences without changing generated scenarios, formulas, or controller production files.

- [ ] **Step 3: Configure and build each revision separately**

Use distinct build directories under `D:\codex-build\axis-stress-990946c`, `D:\codex-build\axis-stress-0710d11`, and the current `D:\codex-build\pob`. Build `cod_native_partial_occlusion_benchmark_tests` and `cod_native_partial_occlusion_benchmark` in Release.

- [ ] **Step 4: Verify tests on all three revisions**

Expected: the partial-occlusion tests pass on all three builds.

- [ ] **Step 5: Run each benchmark twice with seed 1337**

Write version-labelled A/B artifacts and calculate SHA-256. Each pair from the same revision must match exactly.

- [ ] **Step 6: Generate the comparison report**

Run `scripts/compare_axis_stress_ab.ps1` against the three primary artifacts and write:

- `runs/benchmarks/axis_stress_ab_seed1337.json`
- `docs/benchmarks/axis-stress-ab-seed1337.md`

- [ ] **Step 7: Run full current-branch verification**

Run a full Release build, all `*tests.exe`, existing ADS/gamepad benchmark checks, and `git diff --check`. Confirm the two historical partial-occlusion scenario objects still match their previous values.

- [ ] **Step 8: Commit the evidence report**

```powershell
git add docs/benchmarks/axis-stress-ab-seed1337.md
git commit -m "docs: record axis stress A/B results"
```

- [ ] **Step 9: Remove temporary worktrees safely**

Verify each resolved path is a direct child of the repository's `.worktrees` directory and has a clean status, then remove it with `git worktree remove`. Do not delete benchmark artifacts from `runs/benchmarks/`.
