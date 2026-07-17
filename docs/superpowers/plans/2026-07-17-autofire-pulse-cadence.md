# AutoFire Pulse Cadence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore reliable AutoFire across 1000 Hz controller ticks, emit immediate 30 ms synthetic fire pulses every 100 ms, and guarantee that AutoFire never suppresses physical RB or RT.

**Architecture:** TargetCoordinator will treat the absence of a newly published Vision frame as time advancement rather than a processed miss, while a fresh no-target frame revokes fire immediately. AutoFireGate will own a monotonic-time pulse scheduler and return only synthetic fire state; NativeGamepadController will combine that state with physical fire using logical OR. Vision remains the source of strong-observed fire eligibility, and cue/weak/predicted continuity remains aim-only.

**Tech Stack:** C++17, CMake/MSBuild Release targets, TOML runtime configuration, native deterministic unit/integration tests, PowerShell verification.

---

## File Map

- `native/controller_native/runtime_config.h`: define `pulse_width_ms` and `pulse_period_ms` defaults in `GamepadAutoFireConfig`.
- `native/controller_native/runtime_config.cpp`: parse the new keys and validate their relationship.
- `native/controller_native/runtime_config_tests.cpp`: prove defaults, overrides, and invalid values.
- `config.toml`: set the accepted live profile to 30 ms / 100 ms.
- `config.native.example.toml`: document the same defaults.
- `native/controller_native/target_coordinator.cpp`: distinguish no-publication ticks from processed misses, preserve anonymous in-radius selector hold identity, clear fire on true misses, and remove the duplicate reliability gate.
- `native/controller_native/target_coordinator_tests.cpp`: cover the TargetPlan fire/hold lifecycle directly.
- `native/controller_native/auto_fire_gate.h`: add cadence state, pulse-start counter, and cadence-wait observability.
- `native/controller_native/auto_fire_gate.cpp`: implement the monotonic 30/100 ms synthetic pulse scheduler and remove physical-output mutation from the gate.
- `native/controller_native/auto_fire_gate_tests.cpp`: test pulse width, cadence, revocation, no catch-up, and takeover behavior.
- `native/controller_native/native_gamepad_controller.cpp`: keep physical RB/RT as the base output and OR synthetic fire onto only the configured channel.
- `native/controller_native/target_pipeline_integration_tests.cpp`: exercise 100 Hz Vision / 1000 Hz control for one second and physical-fire passthrough.
- `native/runtime_app/telemetry_schema.h`: add pulse-start and cadence-wait fields if the existing schema lacks a suitable field.
- `native/runtime_app/runtime_loop.cpp`: publish pulse scheduler telemetry from controller components.
- `native/runtime_app/runtime_telemetry.cpp`: serialize added pulse fields.
- `native/runtime_app/runtime_telemetry_tests.cpp`: verify telemetry serialization.
- `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`: record the new fire cadence regression command and fixed seed.

### Task 1: Add explicit pulse configuration

**Files:**
- Modify: `native/controller_native/runtime_config.h:154-161`
- Modify: `native/controller_native/runtime_config.cpp:183-185, 296-314`
- Modify: `native/controller_native/runtime_config_tests.cpp`
- Modify: `config.toml:48-54`
- Modify: `config.native.example.toml:47-53`

- [ ] **Step 1: Write failing configuration tests**

Add tests that write a temporary TOML file, call the existing
`load_runtime_config(path)` API, remove the file, and assert both defaults and
overrides:

```cpp
void test_auto_fire_pulse_defaults_and_overrides() {
    const auto defaults = controller_native::GamepadAutoFireConfig{};
    require_near(defaults.pulse_width_ms, 30.0f, 0.001f,
                 "default pulse width must be 30ms");
    require_near(defaults.pulse_period_ms, 100.0f, 0.001f,
                 "default pulse period must be 100ms");

    const auto path = std::filesystem::temp_directory_path() /
        "cod_native_auto_fire_pulse_config.toml";
    {
        std::ofstream output(path);
        output << "[gamepad.auto_fire]\n"
               << "pulse_width_ms = 40\n"
               << "pulse_period_ms = 120\n";
    }
    const auto config = controller_native::load_runtime_config(path);
    std::filesystem::remove(path);
    require_near(config.gamepad.auto_fire.pulse_width_ms, 40.0f, 0.001f,
                 "pulse width override was ignored");
    require_near(config.gamepad.auto_fire.pulse_period_ms, 120.0f, 0.001f,
                 "pulse period override was ignored");
}
```

Add invalid cases for zero width and width greater than period. Assert that
`load_runtime_config` throws `std::runtime_error` containing
`gamepad.auto_fire.pulse_width_ms` and the accepted relationship.

- [ ] **Step 2: Build and run the config test to verify RED**

Run:

```powershell
& $cmake --build D:\codex-build\autofire-pulse --config Release `
  --target cod_native_runtime_config_tests -- /m
D:\codex-build\autofire-pulse\Release\cod_native_runtime_config_tests.exe
```

Expected: compile failure because `GamepadAutoFireConfig` has no pulse fields, or the new assertions fail because keys are unrecognized.

- [ ] **Step 3: Implement the minimal configuration contract**

Add:

```cpp
struct GamepadAutoFireConfig {
    // existing fields...
    float pulse_width_ms = 30.0f;
    float pulse_period_ms = 100.0f;
};
```

Add both names to `auto_fire_keys`, parse them through `parse_float_value`, and
extend the existing `validate_runtime_config` function with:

```cpp
if (config.gamepad.auto_fire.pulse_width_ms <= 0.0f ||
    config.gamepad.auto_fire.pulse_period_ms <= 0.0f ||
    config.gamepad.auto_fire.pulse_width_ms >
        config.gamepad.auto_fire.pulse_period_ms)
    invalid("gamepad.auto_fire.pulse_width_ms",
            "0 < pulse_width_ms <= pulse_period_ms");
```

Synchronize both TOML files:

```toml
pulse_width_ms = 30
pulse_period_ms = 100
```

- [ ] **Step 4: Rebuild and verify GREEN**

Run the same build and executable. Expected: exit 0 and no unknown-key diagnostics.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/runtime_config.h `
  native/controller_native/runtime_config.cpp `
  native/controller_native/runtime_config_tests.cpp `
  config.toml config.native.example.toml
git commit -m "config: define autofire pulse cadence"
```

### Task 2: Repair TargetCoordinator observation and fire semantics

**Files:**
- Modify: `native/controller_native/target_coordinator.cpp:35-63, 88-175, 245-258`
- Modify: `native/controller_native/target_coordinator_tests.cpp`

- [ ] **Step 1: Write failing coordinator tests**

Add four focused tests using real `TargetCoordinator::update` calls:

```cpp
void test_nonfresh_empty_ticks_preserve_observed_fire_plan() {
    TargetCoordinator coordinator;
    auto observed = frame(1, 10.000, 77, 320.0f, 208.0f);
    observed.fire_requested = true;
    observed.observed_fire_eligible = true;
    auto plan = coordinator.update(observed, ads_intent(10.000), 10.000);
    require_true(plan.fire_authority, "strong observed frame must authorize fire");

    VisionObservationBatch no_publication{};
    no_publication.frame_width_px = 480.0f;
    no_publication.frame_height_px = 416.0f;
    no_publication.capture_fresh = false;
    for (int tick = 1; tick <= 9; ++tick) {
        plan = coordinator.update(no_publication, ads_intent(10.000 + tick * .001),
                                  10.000 + tick * .001);
        require_true(plan.lifecycle == TargetLifecycle::Observed,
                     "no-publication tick must not become a processed miss");
        require_true(plan.fire_requested && plan.fire_authority,
                     "frame gap must preserve live fire eligibility");
    }
}
```

```cpp
void test_fresh_processed_miss_revokes_fire_immediately() {
    // Establish a strong requested target, then submit capture_fresh=true/count=0.
    // Assert Coasting, fire_requested=false, fire_authority=false.
}

void test_anonymous_in_radius_hold_preserves_identity_and_fire_request() {
    // Establish source 77, then submit candidate source_id=0 at the same aim point
    // with strong observed fire eligibility. Assert the same TargetPlan target_id,
    // live lifecycle, and nonzero source ownership retained internally through the
    // following named-source frame.
}

void test_vision_fire_authority_is_not_rejected_by_size_weighted_reliability() {
    // Submit observed_fire_eligible=true with candidate reliability=0.65.
    // Assert fire_authority=true; Vision already made the eligibility decision.
}
```

- [ ] **Step 2: Build and run to verify RED**

```powershell
& $cmake --build D:\codex-build\autofire-pulse --config Release `
  --target cod_native_target_coordinator_tests -- /m
D:\codex-build\autofire-pulse\Release\cod_native_target_coordinator_tests.exe
```

Expected: the nonfresh tick becomes `Coasting`, processed miss retains request state, anonymous hold is rejected, and reliability 0.65 lacks fire authority.

- [ ] **Step 3: Implement no-publication preservation**

When there is no candidate and `capture_fresh == false`, retain the last processed lifecycle as live observed/reacquiring while advancing age and prediction. Do not set `was_missing_` and do not clear fire state.

When `capture_fresh == true` with no accepted candidate:

```cpp
fire_requested_ = false;
observed_fire_eligible_ = false;
was_missing_ = true;
lifecycle = pipeline_contract::TargetLifecycle::Coasting;
```

- [ ] **Step 4: Implement anonymous hold identity and authority cleanup**

Allow source ID 0 to associate only inside the existing association radius. When accepted, preserve `source_id_`:

```cpp
if (candidate->source_id != 0) {
    source_id_ = candidate->source_id;
}
```

Keep cue/weak safety through `observed_fire_eligible`; do not infer fire from candidate presence.

Replace the duplicate threshold with:

```cpp
plan.fire_authority = observed_fire_eligible_ &&
    lifecycle != pipeline_contract::TargetLifecycle::Coasting;
```

`Reacquiring` counts as current live evidence but still passes through the unique-frame readiness gate.

- [ ] **Step 5: Rebuild and verify GREEN**

Run the coordinator test executable. Expected: all coordinator tests pass.

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native/target_coordinator.cpp `
  native/controller_native/target_coordinator_tests.cpp
git commit -m "fix: preserve fire authority between vision frames"
```

### Task 3: Implement the 30 ms / 100 ms synthetic pulse scheduler

**Files:**
- Modify: `native/controller_native/auto_fire_gate.h`
- Modify: `native/controller_native/auto_fire_gate.cpp`
- Modify: `native/controller_native/auto_fire_gate_tests.cpp`

- [ ] **Step 1: Write failing pulse timing tests**

Add a helper with `require_aim_ready=false` so this test isolates cadence:

```cpp
void test_pulse_scheduler_emits_ten_thirty_ms_presses_per_second() {
    GamepadAutoFireConfig fire;
    fire.require_aim_ready = false;
    fire.pulse_width_ms = 30.0f;
    fire.pulse_period_ms = 100.0f;
    AutoFireGate gate(fire, {});

    int starts = 0;
    bool previous = false;
    int current_width_ticks = 0;
    std::vector<int> widths;
    for (int tick = 0; tick < 1000; ++tick) {
        const auto decision = gate.evaluate(ready_input(20.0 + tick * 0.001, tick + 1));
        if (decision.should_fire && !previous) ++starts;
        if (decision.should_fire) ++current_width_ticks;
        if (!decision.should_fire && previous) {
            widths.push_back(current_width_ticks);
            current_width_ticks = 0;
        }
        previous = decision.should_fire;
    }
    require_eq_u64(static_cast<std::uint64_t>(starts), 10,
                   "one second must start exactly ten pulses");
    require_true(std::all_of(widths.begin(), widths.end(),
        [](int width) { return width >= 30; }), "every pulse must last at least 30ms");
    require_eq_u64(gate.counters().pulse_starts, 10,
                   "pulse-start counter must not count held ticks");
}
```

Add separate tests for immediate first pulse, released state at 31-99 ms, next start at 100 ms, no catch-up burst after a 250 ms clock jump, and immediate revocation when authority disappears.

- [ ] **Step 2: Build and run to verify RED**

```powershell
& $cmake --build D:\codex-build\autofire-pulse --config Release `
  --target cod_native_auto_fire_tests -- /m
D:\codex-build\autofire-pulse\Release\cod_native_auto_fire_tests.exe
```

Expected: compile failure for pulse fields/counter or timing assertions fail because current behavior is continuous/flickering rather than scheduled.

- [ ] **Step 3: Add minimal scheduler state**

Add private state:

```cpp
bool pulse_cycle_active_ = false;
double pulse_started_at_seconds_ = -1.0;
double next_pulse_at_seconds_ = -1.0;
```

Add `pulse_starts` to `NativeAutoFireCounters` and a cadence wait block reason or explicit `pulse_waiting` decision field.

- [ ] **Step 4: Implement monotonic cadence**

After base authorization and manual takeover are resolved:

```cpp
const double width = auto_fire_config_.pulse_width_ms / 1000.0;
const double period = auto_fire_config_.pulse_period_ms / 1000.0;
if (!authorized) {
    reset_pulse_schedule();
    should_fire = false;
} else if (!pulse_cycle_active_ || input.now_seconds >= next_pulse_at_seconds_) {
    pulse_cycle_active_ = true;
    pulse_started_at_seconds_ = input.now_seconds;
    next_pulse_at_seconds_ = input.now_seconds + period;
    ++counters_.pulse_starts;
    should_fire = true;
} else {
    should_fire = input.now_seconds - pulse_started_at_seconds_ < width;
}
```

Use a small floating-point epsilon no larger than half a controller tick only if the exact 30/100 ms boundary test demonstrates binary rounding error. Never emit multiple starts in one evaluation.

- [ ] **Step 5: Remove physical-output clearing from AutoFireGate**

Delete or stop calling `release_fire_output(GamepadOutputState&)`. AutoFireGate must return synthetic state only. Keep `apply_fire_output` only if it ORs the configured synthetic channel and never clears RB/RT.

- [ ] **Step 6: Rebuild and verify GREEN**

Run `cod_native_auto_fire_tests.exe`. Expected: all old authority/readiness tests and new cadence tests pass.

- [ ] **Step 7: Commit**

```powershell
git add native/controller_native/auto_fire_gate.h `
  native/controller_native/auto_fire_gate.cpp `
  native/controller_native/auto_fire_gate_tests.cpp
git commit -m "feat: emit reliable autofire pulses"
```

### Task 4: Guarantee physical RB/RT passthrough in the production controller

**Files:**
- Modify: `native/controller_native/native_gamepad_controller.cpp:409-439`
- Modify: `native/controller_native/target_pipeline_integration_tests.cpp`

- [ ] **Step 1: Write failing integration tests for physical ownership**

Add cases that establish an active synthetic pulse and then exercise both fire bindings:

```cpp
void test_physical_fire_is_never_cleared_by_autofire() {
    // Establish readiness and an active pulse.
    auto physical = aiming_physical_state();
    physical.rb = true;
    auto output = controller.build_output(physical);
    require(output.rb, "physical RB must survive synthetic takeover release");

    now += 0.001;
    physical.rb = false;
    physical.right_trigger = 1.0f;
    output = controller.build_output(physical);
    require(output.right_trigger >= 0.999f,
            "physical RT must survive synthetic takeover release");
}
```

Repeat during pulse pressed, cadence wait, manual takeover release, and manual takeover guard.

- [ ] **Step 2: Build and run to verify RED**

```powershell
& $cmake --build D:\codex-build\autofire-pulse --config Release `
  --target cod_native_controller_tests -- /m
D:\codex-build\autofire-pulse\Release\cod_native_controller_tests.exe
```

Expected: physical RB/RT is cleared by the current `release_fire_output` path during takeover.

- [ ] **Step 3: Implement one-way synthetic mixing**

Preserve the physical output created at the start of `build_output`, then add synthetic fire only:

```cpp
if (fire.should_fire) {
    if (config_.auto_fire.fire_output == "RT") {
        output.right_trigger = std::max(output.right_trigger, 1.0f);
    } else {
        output.rb = true;
    }
}
```

Do not call any AutoFire method that writes false/zero to the final output. Recoil input remains:

```cpp
input.fire_active = auto_fire_active || manual_fire_pressed(physical);
```

- [ ] **Step 4: Rebuild and verify GREEN**

Run the integration test. Expected: all physical-fire ownership assertions pass.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/native_gamepad_controller.cpp `
  native/controller_native/target_pipeline_integration_tests.cpp
git commit -m "fix: preserve physical fire through autofire"
```

### Task 5: Add the full 100 Hz / 1000 Hz cadence regression

**Files:**
- Modify: `native/controller_native/target_pipeline_integration_tests.cpp`

- [ ] **Step 1: Write the failing one-second production-chain test**

Drive a deterministic clock for ticks 0 through 999. Submit a new strong Vision snapshot every 10 ticks and call `build_output` every 1 ms. Count false-to-true RB transitions and consecutive pressed ticks.

Assertions:

```cpp
require(pulse_starts == 10, "100Hz/1000Hz chain must emit ten pulse starts");
require(min_pressed_ticks >= 30, "every uninterrupted pulse must last 30ms");
require(max_period_error_ticks <= 1, "pulse period must remain within one tick");
require(frame_gap_releases == 0, "no-publication ticks must not release a pulse");
```

At tick 550 in a second subcase, submit a fresh processed no-target snapshot and assert synthetic release on that same tick. Add cue-hold and weak-target subcases that never start a pulse.

- [ ] **Step 2: Build and run to verify RED if any production coupling remains**

Run `cod_native_controller_tests.exe` as above. Expected before remaining fixes: wrong pulse count, sub-30 ms widths, or frame-gap release.

- [ ] **Step 3: Make only the minimal integration corrections**

Adjust state transfer between `TargetPlan`, `vision_state_from_plan`, and `AutoFireGateInput` only where the failing assertions show missing information. Do not add a parallel controller or a second hold gate.

- [ ] **Step 4: Rebuild and verify GREEN**

Run:

```powershell
D:\codex-build\autofire-pulse\Release\cod_native_controller_tests.exe
D:\codex-build\autofire-pulse\Release\cod_native_auto_fire_tests.exe
D:\codex-build\autofire-pulse\Release\cod_native_target_coordinator_tests.exe
```

Expected: all pass and the cadence test reports 10 starts, minimum width at least 30 ticks.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/target_pipeline_integration_tests.cpp `
  native/controller_native/native_gamepad_controller.cpp `
  native/controller_native/target_coordinator.cpp
git commit -m "test: guard live autofire cadence"
```

### Task 6: Make pulse telemetry auditable

**Files:**
- Modify: `native/controller_native/output_mixer.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/runtime_app/telemetry_schema.h`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/runtime_telemetry.cpp`
- Modify: `native/runtime_app/runtime_telemetry_tests.cpp`

- [ ] **Step 1: Write failing telemetry tests**

Extend the telemetry fixture with:

```cpp
input.auto_fire_pulse_starts = 7;
input.auto_fire_pulse_pressed = true;
input.auto_fire_cadence_wait = false;
```

Assert the serialized controller sample contains the exact fields and values.

- [ ] **Step 2: Build and run to verify RED**

```powershell
& $cmake --build D:\codex-build\autofire-pulse --config Release `
  --target cod_native_runtime_telemetry_tests -- /m
D:\codex-build\autofire-pulse\Release\cod_native_runtime_telemetry_tests.exe
```

Expected: compile failure because the telemetry fields do not exist.

- [ ] **Step 3: Wire scheduler observability**

Expose pulse starts separately from held ticks and cadence wait. Keep legacy requested/allowed/blocked fields unchanged for compatibility.

- [ ] **Step 4: Rebuild and verify GREEN**

Run the telemetry test. Expected: exit 0 with exact JSON fields.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/output_mixer.h `
  native/controller_native/native_gamepad_controller.cpp `
  native/runtime_app/telemetry_schema.h `
  native/runtime_app/runtime_loop.cpp `
  native/runtime_app/runtime_telemetry.cpp `
  native/runtime_app/runtime_telemetry_tests.cpp
git commit -m "telemetry: expose autofire pulse state"
```

### Task 7: Full verification and launch-path build

**Files:**
- Modify: `docs/project/NATIVE_CONTROLLER_BENCHMARKS.md`

- [ ] **Step 1: Run the focused tests**

```powershell
$bin = 'D:\codex-build\autofire-pulse\Release'
& "$bin\cod_native_runtime_config_tests.exe"
& "$bin\cod_native_target_coordinator_tests.exe"
& "$bin\cod_native_auto_fire_tests.exe"
& "$bin\cod_native_controller_tests.exe"
& "$bin\cod_native_runtime_telemetry_tests.exe"
```

Expected: all exit 0.

- [ ] **Step 2: Build all Release targets**

```powershell
& $cmake --build D:\codex-build\autofire-pulse --config Release -- /m
```

Expected: exit 0 and `cod_native_runtime.exe` produced.

- [ ] **Step 3: Run every native test executable**

```powershell
$tests = Get-ChildItem "$bin\*tests.exe"
$failed = foreach ($test in $tests) {
    & $test.FullName *> $null
    if ($LASTEXITCODE -ne 0) { $test.Name }
}
if ($failed) { throw "Failed native tests: $($failed -join ', ')" }
```

Expected: 43 or more tests, zero failures.

- [ ] **Step 4: Run behavior benchmarks and contracts**

```powershell
& "$bin\cod_native_gamepad_benchmark.exe" --self-test
& "$bin\cod_native_left_stick_motion_benchmark.exe" --require-fixed
& "$bin\cod_native_partial_occlusion_benchmark_tests.exe"
& 'scripts\verify\native_pipeline_contract.bat'
git diff --check
```

Expected: self-test PASS, left-stick 5 scenarios/0 defects, partial occlusion PASS, pipeline contract exit 0, no whitespace errors.

- [ ] **Step 5: Record the cadence gate**

Add the fixed command, 30 ms minimum width, 100 ms period, ten starts, seed/timing model, and physical-fire passthrough result to `NATIVE_CONTROLLER_BENCHMARKS.md`.

- [ ] **Step 6: Commit verification documentation**

```powershell
git add docs/project/NATIVE_CONTROLLER_BENCHMARKS.md
git commit -m "docs: record autofire cadence verification"
```

- [ ] **Step 7: Merge only after verification and rebuild the actual launch path**

After local merge to `dev`, run:

```powershell
& $cmake --build native\vision_native\build --config Release -- /m
Get-FileHash native\vision_native\build\Release\cod_native_runtime.exe -Algorithm SHA256
```

Expected: launch-path build exits 0 and a fresh runtime hash/timestamp is reported to the user.
