# Sustained AimLab Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and freeze a deterministic 60-second closed-loop benchmark that rewards fast ADS acquisition and sustained BodyLock center tracking before any production control-policy changes.

**Architecture:** Split the tool into deterministic scenario generation, an additive scorer, a generic 1kHz closed-loop simulator, and a thin `NativeGamepadController` adapter/CLI. Synthetic observations arrive at seeded 80-100Hz intervals; the virtual plant applies a 500px/stick/s camera response and the approved `1.00 -> 0.50 -> 0.40` aim-slowdown curve. The production controller already accepts an injected clock, so Phase 1 requires no production behavior seam.

**Tech Stack:** C++17, CMake/MSVC, existing native controller pipeline, deterministic `<random>` generation, hand-written JSON output consistent with current native benchmarks, PowerShell baseline runner.

---

## Scope rule

This plan implements Phase 1 only: benchmark, scorer verification, production-controller baseline, and regression verification. It must not modify `AxisIntentArbiter`, `BodylockFollowController`, `AimDynamicsShaper`, controller gains, or production config semantics. Vector arbitration and braking receive a separate plan only after this baseline is frozen.

Repository `AGENT.md` overrides the generic frequent-commit advice: keep the slice uncommitted while tasks are incomplete and create one cohesive benchmark commit after all tests and baseline artifacts pass.

## File map

- Create `native/controller_native/sustained_aimlab_types.h`: value types, enums, configuration, per-target record, and aggregate result shared by generator, simulator, scorer, and CLI.
- Create `native/controller_native/sustained_aimlab_scenario.h` and `.cpp`: seeded target lifecycle/motion generation, observation cadence/noise, manual-input profiles, target integration, and aim-slowdown multiplier.
- Create `native/controller_native/sustained_aimlab_score.h` and `.cpp`: additive acquisition/tracking/smoothness scoring plus ADS, overshoot, undertrack, interruption, and false-stop event detection.
- Create `native/controller_native/sustained_aimlab_simulator.h` and `.cpp`: 1ms closed-loop runner with a callback-based controller adapter and virtual camera plant.
- Create `native/controller_native/sustained_aimlab_scenario_tests.cpp`: determinism, bounds, timing, maneuver, and slowdown tests.
- Create `native/controller_native/sustained_aimlab_score_tests.cpp`: known-controller ordering and defect-detector tests.
- Create `native/controller_native/sustained_aimlab_simulator_tests.cpp`: full fake-controller closed-loop invariants and identical-script A/B tests.
- Create `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`: CLI, runtime-config loading, `NativeGamepadController` adapter, console summary, validation, and atomic JSON output.
- Create `scripts/run_sustained_aimlab_baseline.ps1`: configure/build, capture revision/config fingerprint, run committed seeds, and write the official baseline artifact.
- Create `artifacts/benchmarks/sustained_aimlab/baseline-20260718.json`: immutable current-controller baseline.
- Modify `native/vision_native/CMakeLists.txt`: add three focused test executables, the production benchmark executable, source dependencies, and CTest entries.
- Modify `artifacts/benchmarks/README.md`: document the new artifact family and reproduction command.

### Task 1: Deterministic scenario and slowdown model

**Files:**
- Create: `native/controller_native/sustained_aimlab_types.h`
- Create: `native/controller_native/sustained_aimlab_scenario.h`
- Create: `native/controller_native/sustained_aimlab_scenario.cpp`
- Create: `native/controller_native/sustained_aimlab_scenario_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add the scenario test target with failing behavioral tests**

Define tests for identical-seed equality, different-seed inequality, 60-second default duration, 250-330ms acquisition deadlines, 1000ms tracking windows, 50ms gaps, legal 24px target bounds, 10.0-12.5ms observation intervals, bounded +/-0.75px observation noise, and slowdown anchor points.

The slowdown assertions must be exact within `1e-6`:

```cpp
expect_near(aim_slowdown_multiplier(28.0), 1.0, 1e-6);
expect_near(aim_slowdown_multiplier(24.0), 0.5, 1e-6);
expect_near(aim_slowdown_multiplier(0.0), 0.4, 1e-6);
expect_true(aim_slowdown_multiplier(25.5) < 1.0);
expect_true(aim_slowdown_multiplier(25.5) > 0.5);
```

Add a CMake target:

```cmake
add_executable(cod_native_sustained_aimlab_scenario_tests
    ../controller_native/sustained_aimlab_scenario_tests.cpp
    ../controller_native/sustained_aimlab_scenario.cpp
)
target_include_directories(cod_native_sustained_aimlab_scenario_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME NativeSustainedAimlabScenarioTests
    COMMAND cod_native_sustained_aimlab_scenario_tests)
```

- [ ] **Step 2: Build to verify the test fails**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_scenario_tests
```

Expected: compilation fails because `sustained_aimlab_scenario.h` and its API do not exist.

- [ ] **Step 3: Define the shared scenario types and public API**

Use these stable interfaces:

```cpp
namespace controller_native::sustained_aimlab {

struct Vec2d { double x = 0.0; double y = 0.0; };

enum class MotionProfile {
    ConstantHorizontal, ConstantVertical, ConstantDiagonal,
    Accelerate, Reverse, JumpFall, Stop
};

enum class ManualProfile { Pure, Mixed };

struct BenchmarkConfig {
    int duration_ms = 60'000;
    int tick_ms = 1;
    int tracking_window_ms = 1'000;
    int inter_target_gap_ms = 50;
    int min_acquire_deadline_ms = 250;
    int max_acquire_deadline_ms = 330;
    double target_radius_px = 24.0;
    double slowdown_transition_px = 3.0;
    double slowdown_edge_multiplier = 0.50;
    double slowdown_center_multiplier = 0.40;
    double camera_response_px_per_stick_second = 500.0;
    int frame_width_px = 640;
    int frame_height_px = 512;
};

struct TargetScript {
    std::uint64_t id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    Vec2d initial_error_px;
    Vec2d initial_velocity_px_per_second;
    Vec2d acceleration_px_per_second_squared;
    int maneuver_at_ms = -1;
    int acquire_deadline_ms = 250;
    std::vector<int> observation_at_ms;
    std::vector<Vec2d> observation_noise_px;
};

struct ScenarioScript {
    std::uint32_t seed = 0;
    BenchmarkConfig config;
    std::vector<TargetScript> targets;
    std::uint64_t hash = 0;
};

ScenarioScript generate_script(std::uint32_t seed, const BenchmarkConfig& config);
void advance_target(TargetScript const&, int target_elapsed_ms, double dt_seconds,
                    Vec2d& position_error_px, Vec2d& velocity_px_per_second);
double aim_slowdown_multiplier(double distance_px,
                               const BenchmarkConfig& config = {});
const char* to_string(MotionProfile profile) noexcept;
}
```

- [ ] **Step 4: Implement deterministic generation and motion**

Use `std::mt19937` only through explicitly constructed distributions. Generate enough target scripts to cover the worst-case number of 250ms misses plus 50ms gaps during the requested duration. Sample initial polar distance in `[48, 140]`, clamp center coordinates to `[24, 616] x [24, 488]`, and cycle motion profiles after a seeded initial offset so every official seed contains all profiles. During target integration, reflect the target velocity component at `[24, 616]` and `[24, 488]` and clamp the center back inside the bound; never silently despawn or clip a moving circle.

Implement the approved smoothstep slowdown:

```cpp
double smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double aim_slowdown_multiplier(double distance, const BenchmarkConfig& c) {
    if (distance >= c.target_radius_px + c.slowdown_transition_px) return 1.0;
    if (distance >= c.target_radius_px) {
        const double inward = (c.target_radius_px + c.slowdown_transition_px - distance)
            / c.slowdown_transition_px;
        return 1.0 + (c.slowdown_edge_multiplier - 1.0) * smoothstep(inward);
    }
    const double penetration = 1.0 - distance / c.target_radius_px;
    return c.slowdown_edge_multiplier
        + (c.slowdown_center_multiplier - c.slowdown_edge_multiplier)
        * smoothstep(penetration);
}
```

Hash every generated scalar in a fixed order with 64-bit FNV-1a; never hash raw struct bytes or locale-formatted text.

- [ ] **Step 5: Build and run scenario tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_scenario_tests
& native/vision_native/build/Release/cod_native_sustained_aimlab_scenario_tests.exe
```

Expected: `cod_native_sustained_aimlab_scenario_tests PASS`.

### Task 2: Additive scorer and defect classification

**Files:**
- Create: `native/controller_native/sustained_aimlab_score.h`
- Create: `native/controller_native/sustained_aimlab_score.cpp`
- Create: `native/controller_native/sustained_aimlab_score_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing score-ordering and event tests**

Fixtures must prove:

```text
early acquisition > late acquisition > miss
center tracking > 12px tracking > 22px tracking > outside
clean first pass > deliberate overshoot then recovery
useful tracking > smooth zero output
one overshoot trace produces exactly one over event
one lag trace produces exactly one undertrack event
valid manual escape produces zero false interruptions
stable demanded zero output for 20ms produces one false stop
real target stop followed by stale output produces one stale-output event
```

- [ ] **Step 2: Build to verify scorer tests fail**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_score_tests
```

Expected: compilation fails because the score API does not exist.

- [ ] **Step 3: Define frame, per-target, and aggregate score APIs**

```cpp
struct ScoreFrame {
    int absolute_ms = 0;
    int target_elapsed_ms = 0;
    bool in_tracking_window = false;
    bool target_observed = true;
    bool tracker_reliable = true;
    bool manual_escape = false;
    bool bodylock_mode = false;
    std::uint64_t target_id = 0;
    Vec2d error_px;
    Vec2d target_velocity_px_per_second;
    Vec2d manual_stick;
    Vec2d requested_assist_stick;
    Vec2d shaped_assist_stick;
    Vec2d final_stick;
};

struct TargetResult {
    std::uint64_t id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    int deadline_ms = 0;
    bool acquired = false;
    bool first_pass_success = false;
    int first_entry_ms = -1;
    double acquire_points = 0.0;
    double tracking_points = 0.0;
    double smooth_bonus = 0.0;
    int over_events = 0;
    int undertrack_events = 0;
    int false_mode_exit_events = 0;
    int assist_dropout_events = 0;
    int false_stop_events = 0;
    int stale_output_after_stop_events = 0;
    int circle_exit_events = 0;
    double max_error_px = 0.0;
    std::vector<double> tracking_errors_px;
};

class TargetScorer {
public:
    TargetScorer(TargetScript script, BenchmarkConfig config);
    void add_frame(const ScoreFrame& frame);
    void mark_acquired(int entry_ms);
    TargetResult finish();
};

struct BenchmarkResult {
    std::uint32_t seed = 0;
    std::uint64_t script_hash = 0;
    double acquire_points = 0.0;
    double tracking_points = 0.0;
    double smooth_bonus = 0.0;
    int targets_spawned = 0;
    int targets_acquired = 0;
    int targets_missed = 0;
    int over_events = 0;
    int undertrack_events = 0;
    int false_interruption_events = 0;
    int false_stop_events = 0;
    int stale_output_after_stop_events = 0;
    double mean_error_px = 0.0;
    double p95_error_px = 0.0;
    double p95_output_delta = 0.0;
    double p95_jerk = 0.0;
    std::vector<TargetResult> targets;
};

BenchmarkResult aggregate(std::uint32_t seed, std::uint64_t script_hash,
                          std::vector<TargetResult> targets);
```

- [ ] **Step 4: Implement the approved formulas and latches**

Use acquisition and tracking formulas verbatim from the spec. Smoothness uses output delta magnitude and contributes only inside the circle while distance is non-worsening.

Implement latched detectors with explicit release thresholds:

- overshoot arms inside `R/3`, fires after opposite-side crossing plus growth beyond `2R/3` within 150ms, and releases inside `R/3`;
- undertrack fires after at least 40ms with speed `>=40px/s`, projected lag `>R/2`, and no closure, then releases below `R/4`;
- false stop fires after 20ms with demand active and correct-direction final projection `<0.05`, then releases after projection reaches `0.08` or demand ends;
- false mode exit and assist dropout require observed/reliable same-target evidence and no manual escape;
- stale output after a true stop/loss uses magnitude `>=0.20` for 20ms.

- [ ] **Step 5: Run scorer tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_score_tests
& native/vision_native/build/Release/cod_native_sustained_aimlab_score_tests.exe
```

Expected: `cod_native_sustained_aimlab_score_tests PASS`.

### Task 3: Generic 1kHz closed-loop simulator

**Files:**
- Create: `native/controller_native/sustained_aimlab_simulator.h`
- Create: `native/controller_native/sustained_aimlab_simulator.cpp`
- Create: `native/controller_native/sustained_aimlab_simulator_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Write failing fake-controller loop tests**

Create callback fixtures for a perfect controller, a delayed controller, an always-zero controller, an overshooting controller, and a deterministic noisy controller. Assert identical seed/script hashes, exact 60,000 ticks, deadline despawn, 1000 tracking ticks after acquisition, correct camera X/Y signs, and the score ordering defined in Task 2.

- [ ] **Step 2: Build to verify simulator tests fail**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_simulator_tests
```

Expected: compilation fails because the simulator API does not exist.

- [ ] **Step 3: Define the controller callback and runner API**

```cpp
struct ControllerObservation {
    int now_ms = 0;
    bool fresh_vision = false;
    std::uint64_t frame_id = 0;
    std::uint64_t target_id = 0;
    Vec2d observed_error_px;
    Vec2d manual_stick;
};

struct ControllerStepResult {
    Vec2d final_stick;
    Vec2d requested_assist_stick;
    Vec2d shaped_assist_stick;
    bool bodylock_mode = false;
    bool target_observed = false;
    bool tracker_reliable = false;
};

using ControllerStep = std::function<ControllerStepResult(
    const ControllerObservation&)>;

BenchmarkResult run_simulation(const ScenarioScript& script,
                               ManualProfile manual_profile,
                               ControllerStep controller_step);
```

- [ ] **Step 4: Implement the 1ms state machine and virtual plant**

Keep the reticle at screen center and integrate target-relative error:

```cpp
const double gain = script.config.camera_response_px_per_stick_second
    * aim_slowdown_multiplier(length(error), script.config);
error.x += target_velocity.x * 0.001 - output.final_stick.x * gain * 0.001;
error.y += target_velocity.y * 0.001 + output.final_stick.y * gain * 0.001;
```

Submit fresh noisy observations only at the script's observation times. Generate mixed manual input from the script and current error without giving the controller future target state. Transition to tracking on first circle entry; miss at deadline; track exactly 1000 ticks; apply the 50ms gap; and terminate at exactly 60,000 ticks.

- [ ] **Step 5: Run simulator tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_simulator_tests
& native/vision_native/build/Release/cod_native_sustained_aimlab_simulator_tests.exe
```

Expected: `cod_native_sustained_aimlab_simulator_tests PASS`.

### Task 4: Wire the real NativeGamepadController

**Files:**
- Create: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add a failing production-adapter smoke test mode**

The executable must support `--smoke --duration-ms 2000 --seed 1337` and fail if it runs a non-1ms tick, produces non-finite output, loses the script hash, or never enters ADS mode while targets are present.

- [ ] **Step 2: Add the production target and build to expose missing integration**

Add `cod_native_sustained_aimlab_benchmark` with the new scenario/scorer/simulator sources, `runtime_config.cpp`, `native_gamepad_controller.cpp`, `tracking_native/tracker_authority.cpp`, `auto_fire_gate.cpp`, the recoil sources, `weapon_recognizer.cpp`, `output_mixer.cpp`, and `controller_pipeline.cpp` used by `cod_native_controller_tests`. Add the new target to the existing `COD_TARGET_PLAN_RUNTIME_SOURCES` foreach list so it receives `intent_filter.cpp`, `control_response_estimator.cpp`, `target_coordinator.cpp`, `ads_acquisition_controller.cpp`, `bodylock_follow_controller.cpp`, `aim_dynamics_shaper.cpp`, and `axis_intent_arbiter.cpp`. Link `vision_native_core`, `gdi32`, and `windowsapp`; use the same include directories and Windows compile definitions as `cod_native_partial_occlusion_benchmark`.

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_benchmark
```

Expected before the adapter is complete: compilation or smoke validation fails.

- [ ] **Step 3: Implement the production adapter**

Load the existing runtime config, copy its gamepad section, disable recoil only in that benchmark-local copy, inject simulation time, hold ADS physically, submit snapshots at fresh-observation ticks, and expose output components. AutoFire remains configured normally, but cannot affect aim because physical fire is false and benchmark recoil is disabled:

```cpp
double now_seconds = 0.0;
RuntimeConfig runtime = load_runtime_config(options.config_path);
GamepadRuntimeConfig config = runtime.gamepad;
config.recoil.enabled = false;
NativeGamepadController controller(config, [&] { return now_seconds; });

PhysicalGamepadState physical{};
physical.connected = true;
physical.left_trigger = 1.0f;

ControllerStep adapter = [&](const ControllerObservation& in) {
    now_seconds = in.now_ms / 1000.0;
    physical.right_x = static_cast<float>(in.manual_stick.x);
    physical.right_y = static_cast<float>(in.manual_stick.y);
    if (in.fresh_vision) {
        ControllerVisionSnapshot snapshot{};
        snapshot.frame_updated = true;
        snapshot.frame_id = in.frame_id;
        snapshot.selected_observation_id = in.target_id;
        snapshot.capture_time_seconds = now_seconds;
        snapshot.ready_time_seconds = now_seconds;
        snapshot.state.screen_center_x = 320.0f;
        snapshot.state.screen_center_y = 256.0f;
        pipeline_contract::VisionCandidateSnapshot candidate{};
        candidate.id = in.target_id;
        candidate.valid = true;
        candidate.has_aim_point = true;
        candidate.aim_point_px = {
            static_cast<float>(320.0 + in.observed_error_px.x),
            static_cast<float>(256.0 + in.observed_error_px.y),
        };
        candidate.confidence = 0.95f;
        candidate.suggested_authority_state =
            common_native::TargetAuthorityState::StrongAssist;
        snapshot.candidates.push_back(candidate);
        controller.submit_vision_snapshot(snapshot);
    }
    const auto out = controller.build_output(physical);
    const auto& c = controller.last_output_components();
    return ControllerStepResult{
        {out.right_x, out.right_y},
        {c.requested_assist_stick.x, c.requested_assist_stick.y},
        {c.shaped_assist_stick.x, c.shaped_assist_stick.y},
        controller.last_ai_aim_mode() == "body_lock",
        controller.last_frame_vision_state().current_observed_target_present,
        controller.last_frame_vision_state().has_target,
    };
};
```

Do not introduce a benchmark-only path inside the controller.

- [ ] **Step 4: Build and run the production smoke test**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_benchmark
& native/vision_native/build/Release/cod_native_sustained_aimlab_benchmark.exe --config config.toml --smoke --duration-ms 2000 --seed 1337
```

Expected: exit code 0, finite non-negative scores, `ticks=2000`, and `PASS`.

### Task 5: CLI validation and atomic JSON output

**Files:**
- Modify: `native/controller_native/cod_native_sustained_aimlab_benchmark.cpp`
- Create: `scripts/run_sustained_aimlab_baseline.ps1`
- Modify: `artifacts/benchmarks/README.md`

- [ ] **Step 1: Add failing CLI/JSON self-tests**

Add `--self-test-io` assertions for repeated seeds, unknown arguments, missing config, invalid duration, output-parent creation, non-finite rejection, partial-file cleanup, JSON schema/version fields, config fingerprint, revision/dirty metadata, and per-target records.

- [ ] **Step 2: Run the self-test to verify failure**

Run:

```powershell
& native/vision_native/build/Release/cod_native_sustained_aimlab_benchmark.exe --self-test-io
```

Expected: non-zero until validation and writer are implemented.

- [ ] **Step 3: Implement CLI and JSON contract**

Support:

```text
--config PATH
--output PATH
--seed N                 repeatable
--duration-ms N          smoke/developer override
--profile pure|mixed|both
--revision TEXT
--dirty true|false
--smoke
--self-test-io
```

Default official seeds are `1337`, `20260718`, and `424242`. Write to `<output>.partial`, flush and close it, then rename to the final path only after every run passes invariants. Reject a baseline/candidate comparison if script hashes differ. Use 64-bit FNV-1a over config bytes and call the field `config_fingerprint_fnv1a64`.

- [ ] **Step 4: Implement the baseline PowerShell wrapper**

The script must resolve the repository root, collect `git rev-parse HEAD` and `git status --porcelain` without printing file contents, build the target, and run:

```powershell
& $exe `
  --config $Config `
  --output $Output `
  --seed 1337 `
  --seed 20260718 `
  --seed 424242 `
  --profile both `
  --revision $revision `
  --dirty $dirty
```

Default output is `artifacts/benchmarks/sustained_aimlab/baseline-20260718.json`.

- [ ] **Step 5: Run IO self-tests and a 2-second JSON smoke run**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target cod_native_sustained_aimlab_benchmark
& native/vision_native/build/Release/cod_native_sustained_aimlab_benchmark.exe --self-test-io
& native/vision_native/build/Release/cod_native_sustained_aimlab_benchmark.exe --config config.toml --output .tmp/sustained-aimlab-smoke.json --duration-ms 2000 --seed 1337 --profile both --smoke
```

Expected: both commands pass; JSON contains two profile results with the same script hash and no `.partial` file remains.

### Task 6: Freeze the current baseline and verify the complete slice

**Files:**
- Create: `artifacts/benchmarks/sustained_aimlab/baseline-20260718.json`
- Modify: `artifacts/benchmarks/README.md`

- [ ] **Step 1: Run all new focused tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target `
  cod_native_sustained_aimlab_scenario_tests `
  cod_native_sustained_aimlab_score_tests `
  cod_native_sustained_aimlab_simulator_tests `
  cod_native_sustained_aimlab_benchmark
& native/vision_native/build/Release/cod_native_sustained_aimlab_scenario_tests.exe
& native/vision_native/build/Release/cod_native_sustained_aimlab_score_tests.exe
& native/vision_native/build/Release/cod_native_sustained_aimlab_simulator_tests.exe
& native/vision_native/build/Release/cod_native_sustained_aimlab_benchmark.exe --self-test-io
```

Expected: every executable prints `PASS` and returns 0.

- [ ] **Step 2: Run the official three-seed, two-profile baseline**

Run:

```powershell
& scripts/run_sustained_aimlab_baseline.ps1 -Config config.toml
```

Expected: six 60-second simulated runs complete, the artifact contains three pure and three mixed results, all scores are finite and non-negative, every pair shares its seed's script hash, and git/config metadata is present.

- [ ] **Step 3: Run existing regression benchmarks and tests**

Run:

```powershell
cmake --build native/vision_native/build --config Release --target `
  cod_native_controller_tests `
  cod_native_aimlab_benchmark_tests `
  cod_native_gamepad_benchmark
& native/vision_native/build/Release/cod_native_controller_tests.exe
& native/vision_native/build/Release/cod_native_aimlab_benchmark_tests.exe
& native/vision_native/build/Release/cod_native_gamepad_benchmark.exe --self-test
```

Expected: existing controller and AimLab tests pass. Record any already-known gamepad benchmark soft failure separately; do not weaken it or alter production gains in this phase.

- [ ] **Step 4: Validate artifact and workspace diff**

Run:

```powershell
git diff --check
git status --short
```

Expected: only the cohesive benchmark implementation, tests, script, README, plan/spec context if intentionally included, and baseline artifact are changed; no runtime binary, generated build output, log, or unrelated user file is staged.

- [ ] **Step 5: Create one cohesive reviewable commit**

Run:

```powershell
git add -- native/controller_native/sustained_aimlab_types.h `
  native/controller_native/sustained_aimlab_scenario.h `
  native/controller_native/sustained_aimlab_scenario.cpp `
  native/controller_native/sustained_aimlab_scenario_tests.cpp `
  native/controller_native/sustained_aimlab_score.h `
  native/controller_native/sustained_aimlab_score.cpp `
  native/controller_native/sustained_aimlab_score_tests.cpp `
  native/controller_native/sustained_aimlab_simulator.h `
  native/controller_native/sustained_aimlab_simulator.cpp `
  native/controller_native/sustained_aimlab_simulator_tests.cpp `
  native/controller_native/cod_native_sustained_aimlab_benchmark.cpp `
  native/vision_native/CMakeLists.txt `
  scripts/run_sustained_aimlab_baseline.ps1 `
  artifacts/benchmarks/README.md `
  artifacts/benchmarks/sustained_aimlab/baseline-20260718.json `
  docs/superpowers/plans/2026-07-18-sustained-aimlab-benchmark.md
git diff --cached --check
git commit -m "bench: add sustained aimlab controller baseline"
```

Expected: one commit containing the complete test tool and frozen baseline.

## Plan self-review

- Spec coverage: scenario timing, random motion, circular scoring, strong native slowdown, additive points, ADS correction, BodyLock defect metrics, pure/mixed profiles, deterministic JSON, and baseline freezing each map to a task above.
- Scope: production control policy is explicitly excluded; the existing injected clock avoids a controller behavior change.
- Type consistency: `BenchmarkConfig`, `ScenarioScript`, `ScoreFrame`, `TargetResult`, `BenchmarkResult`, `ControllerObservation`, and `ControllerStepResult` are introduced once and reused consistently.
- Placeholder scan: the plan contains no TBD/TODO steps; numeric thresholds and commands are explicit.
