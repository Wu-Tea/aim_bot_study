# Sustained AimLab Full-Speed Left-Strafe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic full-speed left-stick strafing to sustained AimLab and produce paired no-strafe/strafe scores for three new seeds without changing production controller policy.

**Architecture:** Store one seeded four-phase strafe schedule on each target, but select whether it executes as a run option so paired variants share one script hash. The simulator passes full-scale `left_x` to the native adapter and independently evolves hidden player velocity that shifts ground-truth horizontal error. CLI `both` expands every seed/profile/cohort into stable off/on pairs and records audit metrics.

**Tech Stack:** C++20, existing native controller benchmark library, CMake/MSVC Release build, PowerShell verification, JSON benchmark artifacts.

---

### Task 1: Deterministic Strafe Scenario Data

**Files:**
- Modify: `native/controller_native/sustained_aimlab_types.h`
- Modify: `native/controller_native/sustained_aimlab_scenario.cpp`
- Test: `native/controller_native/sustained_aimlab_scenario_tests.cpp`

- [ ] **Step 1: Write failing deterministic schedule tests**

Add tests that generate the same new seed twice and require equal schedules,
full-scale active directions, ordered phases, exactly one sign reversal, and
speed/time constants inside the approved ranges:

```cpp
void test_full_speed_strafe_schedule_is_seeded_and_bounded() {
    BenchmarkConfig config;
    config.duration_ms = 3'000;
    const auto first = generate_script(2026072301u, config);
    const auto second = generate_script(2026072301u, config);
    require(first.targets.front().player_strafe ==
                second.targets.front().player_strafe,
            "same seed must reproduce player strafe");
    for (const auto& target : first.targets) {
        const auto& strafe = target.player_strafe;
        require(strafe.initial_direction == -1 ||
                strafe.initial_direction == 1,
                "active strafe direction must be full scale");
        require(0 <= strafe.onset_ms &&
                strafe.onset_ms < strafe.reverse_ms &&
                strafe.reverse_ms < strafe.release_ms,
                "strafe phases must be ordered");
        require(strafe.top_speed_px_per_second >= 125.0 &&
                strafe.top_speed_px_per_second <= 232.0,
                "player speed must cover approved weapon mobility range");
        require(strafe.time_constant_ms >= 100.0 &&
                strafe.time_constant_ms <= 180.0,
                "player inertia must remain in approved range");
    }
}
```

- [ ] **Step 2: Run the scenario tests and verify RED**

Run:

```powershell
cmake --build b --config Release --target cod_native_sustained_aimlab_scenario_tests
```

Expected: compilation fails because `TargetScript::player_strafe` does not exist.

- [ ] **Step 3: Add the schedule data and independent seeded generator**

Add:

```cpp
struct PlayerStrafeScript {
    int initial_direction = 1;
    int onset_ms = 0;
    int reverse_ms = 1;
    int release_ms = 2;
    double top_speed_px_per_second = 125.0;
    double time_constant_ms = 100.0;
    bool operator==(const PlayerStrafeScript&) const = default;
};

enum class PlayerStrafeMode : std::uint8_t {
    Off,
    FullReversal,
};
```

Store `PlayerStrafeScript player_strafe;` on `TargetScript`. Generate it from a
separate `std::mt19937(seed ^ 0xA17E57AFu)` so target RNG draws do not change.
Hash all schedule fields as part of `ScenarioScript`.

- [ ] **Step 4: Run scenario tests and verify GREEN**

Run the scenario test target and expect `PASS`.

- [ ] **Step 5: Commit scenario data**

```powershell
git add native/controller_native/sustained_aimlab_types.h native/controller_native/sustained_aimlab_scenario.cpp native/controller_native/sustained_aimlab_scenario_tests.cpp
git commit -m "bench: generate deterministic AimLab left strafe"
```

### Task 2: Player-Motion Plant and Audit Metrics

**Files:**
- Modify: `native/controller_native/sustained_aimlab_simulator.h`
- Modify: `native/controller_native/sustained_aimlab_simulator.cpp`
- Modify: `native/controller_native/sustained_aimlab_score.h`
- Test: `native/controller_native/sustained_aimlab_simulator_tests.cpp`

- [ ] **Step 1: Write failing plant and invariance tests**

Add a stationary target fixture with a known `+1 -> -1 -> 0` schedule. Require:

```cpp
const auto off = run_simulation(
    script, ManualProfile::Pure, capture, BenchmarkCohort::AdsAcquire,
    {}, PlayerStrafeMode::Off);
const auto on = run_simulation(
    script, ManualProfile::Pure, capture, BenchmarkCohort::AdsAcquire,
    {}, PlayerStrafeMode::FullReversal);
require(off.script_hash == on.script_hash,
        "paired strafe variants must share target identity");
require(seen_left_x == 1.0,
        "active left input must be full scale");
require(on.max_abs_player_speed_px_per_second > 0.0,
        "strafe plant must realize player velocity");
require(first_motion_error_x < initial_error_x,
        "positive player motion must shift target left");
require(speed_after_release > 0.0 && speed_after_release < speed_before_release,
        "release must decay velocity instead of stopping instantly");
```

Add a default-call regression assertion showing the old overload equals explicit
`PlayerStrafeMode::Off`.

- [ ] **Step 2: Run simulator tests and verify RED**

Build the simulator test target. Expected: compilation fails because the mode,
left-stick observation, and audit metrics do not exist.

- [ ] **Step 3: Implement minimal player-motion simulation**

Extend `ControllerObservation` with `double left_x`. Extend trace frames with
requested left input and realized player speed. Add the final optional parameter:

```cpp
BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire,
    SimulationTraceObserver trace_observer = {},
    PlayerStrafeMode player_strafe_mode = PlayerStrafeMode::Off);
```

For ADS use `target_elapsed_ms` as the strafe clock. For BodyLock use
`tracking_ticks`, so warm-up remains neutral. Evolve:

```cpp
const double desired = left_x * schedule.top_speed_px_per_second;
const double alpha = 1.0 - std::exp(
    -0.001 / (schedule.time_constant_ms / 1000.0));
player_velocity_x += alpha * (desired - player_velocity_x);
error.x -= player_velocity_x * 0.001;
```

Populate per-run active milliseconds, reversal count, maximum `left_x`, sampled
speed bounds, and maximum realized speed.

- [ ] **Step 4: Run simulator tests and verify GREEN**

Build and execute the simulator tests. Expect `PASS`.

- [ ] **Step 5: Commit the plant**

```powershell
git add native/controller_native/sustained_aimlab_simulator.h native/controller_native/sustained_aimlab_simulator.cpp native/controller_native/sustained_aimlab_score.h native/controller_native/sustained_aimlab_simulator_tests.cpp
git commit -m "bench: simulate full-speed player strafe"
```

### Task 3: Deliver Physical Left Stick Through Native Adapter

**Files:**
- Modify: `native/controller_native/native_benchmark_controller_adapter.h`
- Modify: `native/controller_native/native_benchmark_controller_adapter.cpp`
- Test: `native/controller_native/sustained_aimlab_counterfactual_tests.cpp`

- [ ] **Step 1: Write a failing adapter delivery test**

Extend the benchmark-only `AssistedModeCoverage` with
`last_physical_left_x` and `max_abs_physical_left_x`. Create a replay input with
`left_x = -1.0`, step the native adapter, and require:

```cpp
auto coverage = std::make_shared<AssistedModeCoverage>();
NativeReplayAdapter adapter(
    config, BranchSchedule{}, BenchmarkCohort::BodyLockFollow,
    BenchmarkIntentFusionMode::CausalVector, coverage);
ControllerObservation input;
input.target_present = true;
input.target_id = 1;
input.left_x = -1.0;
(void)adapter.step(input);
require(coverage->last_physical_left_x == -1.0f,
        "adapter must deliver requested physical left stick");
require(coverage->max_abs_physical_left_x == 1.0f,
        "adapter must retain full-scale left input");
```

- [ ] **Step 2: Run the counterfactual/adapter test and verify RED**

Build the selected test target. Expected: assertion failure because adapter
leaves `physical_.left_x` at zero.

- [ ] **Step 3: Map the left stick**

In `NativeReplayAdapter::step` add:

```cpp
physical_.left_x = static_cast<float>(
    std::clamp(input.left_x, -1.0, 1.0));
if (coverage_) {
    coverage_->last_physical_left_x = physical_.left_x;
    coverage_->max_abs_physical_left_x = std::max(
        coverage_->max_abs_physical_left_x,
        std::fabs(physical_.left_x));
}
```

Leave `left_y` neutral.

- [ ] **Step 4: Run adapter and existing counterfactual tests**

Expect the new delivery test plus pre-existing replay identity tests to pass.

- [ ] **Step 5: Commit adapter delivery**

```powershell
git add native/controller_native/native_benchmark_controller_adapter.h native/controller_native/native_benchmark_controller_adapter.cpp native/controller_native/sustained_aimlab_counterfactual_tests.cpp
git commit -m "bench: feed AimLab left stick to native controller"
```

### Task 4: Preserve Strafe Mode Through Replay

**Files:**
- Modify: `native/controller_native/sustained_aimlab_counterfactual.h`
- Modify: `native/controller_native/sustained_aimlab_counterfactual.cpp`
- Modify: `native/controller_native/sustained_aimlab_learning.cpp`
- Test: `native/controller_native/sustained_aimlab_counterfactual_tests.cpp`

- [ ] **Step 1: Write failing replay identity test**

Record a `FullReversal` reference, replay a branch, and require every pre-branch
frame to retain identical `left_x`, realized player speed, and true error.

- [ ] **Step 2: Run test and verify RED**

Expected: replay diverges because reference and branch currently call the
simulator without a stored strafe mode.

- [ ] **Step 3: Store and forward mode**

Add `PlayerStrafeMode player_strafe_mode` to `ReplayReference`; accept it in
`record_reference`; forward it from reference recording into every replay call.
Keep learning experiments explicitly `Off` until they opt into a paired mode.

- [ ] **Step 4: Run replay, trace, learning, and simulator tests**

Build and execute all four test targets; expect `PASS`.

- [ ] **Step 5: Commit replay propagation**

```powershell
git add native/controller_native/sustained_aimlab_counterfactual.h native/controller_native/sustained_aimlab_counterfactual.cpp native/controller_native/sustained_aimlab_learning.cpp native/controller_native/sustained_aimlab_counterfactual_tests.cpp
git commit -m "bench: preserve player strafe in AimLab replay"
```

### Task 5: CLI, JSON Provenance, and Paired Execution

**Files:**
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`
- Create: `scripts/verify/run_sustained_aimlab_left_strafe.ps1`

- [ ] **Step 1: Add a failing CTest CLI contract**

Add smoke tests requiring `--left-strafe off` and `--left-strafe both` to parse.
Add a `WILL_FAIL` test for `--left-strafe invalid`. Before implementation,
`off`/`both` fail as unknown arguments.

- [ ] **Step 2: Run CTest and verify RED**

Run:

```powershell
ctest --test-dir b -C Release -R "SustainedAimLab.*LeftStrafe" --output-on-failure
```

Expected: valid-mode tests fail with `unknown or incomplete argument`.

- [ ] **Step 3: Implement CLI and report schema**

Add `left_strafe = "off"` to `CliOptions`, validate
`off|full-reversal|both`, and expand modes in stable `off` then
`full_reversal` order. Call:

```cpp
record_reference(
    script, profile, cohort, factory, player_strafe_mode);
```

Increment report schema to `sustained-aimlab-v2`. Add run fields:

```json
{
  "left_strafe": "full_reversal",
  "left_strafe_active_ms": 1234,
  "left_strafe_reversals": 8,
  "max_abs_left_x": 1.0,
  "min_sampled_player_top_speed_px_per_second": 125.0,
  "max_sampled_player_top_speed_px_per_second": 232.0,
  "max_abs_player_speed_px_per_second": 221.0
}
```

The verification script uses seeds `2026072301`, `2026072302`, and
`2026072303`, both profiles, both cohorts, `--left-strafe both`, the current
config fingerprint, and a non-overwriting timestamped output.

- [ ] **Step 4: Run CLI CTests and verify GREEN**

Expect all valid and invalid-mode contract tests to pass.

- [ ] **Step 5: Commit CLI and runner**

```powershell
git add native/controller_native/cod_native_sustained_aimlab_benchmark.cpp native/vision_native/CMakeLists.txt scripts/verify/run_sustained_aimlab_left_strafe.ps1
git commit -m "bench: run paired AimLab left strafe scores"
```

### Task 6: Full Verification and New-Seed Baseline Comparison

**Files:**
- Create: `artifacts/benchmarks/sustained_aimlab/left-strafe-20260723.json`
- Create: `docs/project/SUSTAINED_AIMLAB_LEFT_STRAFE_ACCEPTANCE_20260723.md`

- [ ] **Step 1: Build all affected targets**

Build the benchmark plus scenario, simulator, trace, counterfactual, learning,
and score test targets in Release. Expected: build exits `0`.

- [ ] **Step 2: Run the complete affected CTest group**

Run CTest for sustained AimLab and native adapter tests with
`--output-on-failure`. Expected: zero failures.

- [ ] **Step 3: Generate the paired 60-second artifact**

Run the new verification script against the current config with the three new
seeds. Expect 24 runs:

```text
3 seeds * 2 manual profiles * 2 cohorts * 2 strafe modes = 24
```

- [ ] **Step 4: Validate pairing and calculate deltas**

Parse the JSON and assert every off/on pair has equal seed, profile, cohort, and
script hash. Aggregate both absolute scores and paired deltas for all primary
and defect metrics.

- [ ] **Step 5: Write the acceptance report**

Record the revision, dirty state, config fingerprint, seeds, exact command,
aggregate off/on scores, ADS/BodyLock breakdown, pure/mixed breakdown, worst
paired regressions, and limitations. Do not interpret movement-caused score loss
as authorization to tune the controller.

- [ ] **Step 6: Run final artifact/report consistency checks**

Require 24 runs, 12 valid pairs, full-scale `max_abs_left_x`, non-zero player
speed only in strafe runs, and all report numbers to match the artifact.

- [ ] **Step 7: Commit retained evidence**

```powershell
git add artifacts/benchmarks/sustained_aimlab/left-strafe-20260723.json docs/project/SUSTAINED_AIMLAB_LEFT_STRAFE_ACCEPTANCE_20260723.md
git commit -m "bench: retain AimLab left strafe baseline"
```
