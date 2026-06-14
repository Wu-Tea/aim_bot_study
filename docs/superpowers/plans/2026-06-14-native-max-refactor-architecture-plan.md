# Native Runtime Maximum Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the current tunable native gamepad runtime as a good git checkpoint, then refactor the runtime into explicit vision, tracking, controller, recoil, output, logging, and replay boundaries without changing live feel until each boundary is proven.

**Architecture:** The final architecture is a measured feedback system, not a one-way script. Vision produces timestamped observations, tracker maintains 2D target memory and authority, controller converts tracker snapshots and player intent into assist commands, recoil contributes an explicit control component, output mixing emits the final gamepad state, and the applied output feeds tracker ego-motion on the next tick through a structured sample.

**Tech Stack:** Native C++ runtime, CMake through `native/vision_native/CMakeLists.txt`, existing native controller behavior tests in `native/controller_native/controller_behavior_tests.cpp`, runtime config through `config.toml` and `native/controller_native/runtime_config.*`, reference tracker package in `native/fps_2d_tracker_package/`.

---

## Relationship To The Short TODO Plan

Use this plan as the maximum refactor target. Use `docs/superpowers/plans/2026-06-14-native-2d-tracker-controller-recoil-plan.md` as the shorter near-term TODO list.

The short plan answers "what should we do first?" This plan answers "what should the whole native runtime look like after the cleanup?"

## Non-Negotiable Baseline Rule

Before implementation starts, the current tuned version should become a git checkpoint.

Current live-feel note:

```text
User reported the current feel is acceptable on 2026-06-14.
Treat this as the candidate baseline before behavior-changing tracker/controller refactor work.
```

Recommended checkpoint once live feel is acceptable:

```powershell
git status --short
git diff --stat
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once
```

Then commit the accepted baseline with a message shaped like:

```text
checkpoint: native controller tuning baseline before tracker refactor
```

If `config.toml` contains local-only settings that should not be committed, record the relevant knob values in a local note or a sanitized docs file instead of committing private/local runtime state.

## Target Runtime Data Flow

```text
Physical gamepad input
  -> input sample
  -> runtime tick context

Vision capture/inference
  -> timestamped detections
  -> tracker ingest at capture time

Tracker query
  -> selected target snapshot
  -> assist authority
  -> fire authority
  -> debug state

Controller policy
  -> ADS snap command
  -> bodylock command
  -> manual arbitration decision
  -> dynamic curve shaping

Recoil policy
  -> recoil compensation command
  -> optional recoil visual event for tracker, disabled by default

Output mixer
  -> clamped final right stick
  -> fire output
  -> virtual gamepad state

Applied output sample
  -> tracker ego-motion buffer for future queries
```

The key feedback edge is the final applied output sample. It must be structured and logged. Until a recoil visual model is calibrated, the default tracker ego-motion input remains the pre-recoil motion component that already matches the current tested behavior.

## Target File Structure

This is the intended end state. Implement it in waves; do not move everything in one change.

```text
native/
  common_native/
    time_types.h
    screen_geometry.h
    stick_types.h
    authority_types.h

  tracking_native/
    tracker_contract.h
    tracker_backend.h
    legacy_projection_tracker.h
    legacy_projection_tracker.cpp
    tracker_adapter.h
    tracker_adapter.cpp
    tracker_authority.h
    tracker_authority.cpp
    projection_model.h
    projection_model.cpp
    ego_motion_buffer.h
    ego_motion_buffer.cpp
    kalman_tracker.h
    kalman_tracker.cpp
    association.h
    association.cpp
    tracker_debug.h

  controller_native/
    native_gamepad_controller.h
    native_gamepad_controller.cpp
    controller_tick_context.h
    controller_pipeline.h
    controller_pipeline.cpp
    ai_aim.h
    ai_aim.cpp
    ads_snap_policy.h
    ads_snap_policy.cpp
    bodylock_policy.h
    bodylock_policy.cpp
    manual_intent.h
    manual_intent.cpp
    aim_assist_dynamics.h
    aim_assist_dynamics.cpp
    output_mixer.h
    output_mixer.cpp

  recoil_native/
    recoil_compensation.h
    recoil_compensation.cpp
    recoil_profile_store.h
    recoil_profile_store.cpp
    recoil_visual_model.h
    recoil_visual_model.cpp
    recoil_debug.h

  runtime_app/
    runtime_loop.cpp
    runtime_loop.h
    aim_perf_file_logger.cpp
    aim_perf_file_logger.h
    native_replay_writer.h
    native_replay_writer.cpp
    native_replay_runner.h
    native_replay_runner.cpp

  replay_native/
    replay_schema.h
    replay_reader.h
    replay_reader.cpp
    replay_metrics.h
    replay_metrics.cpp
```

The target names can be adjusted during implementation, but the responsibilities should stay separated.

## Wave 0: Current-Version Checkpoint And Freeze

**Files:**
- Inspect: `.agent-context/handoff.md`
- Inspect: `config.toml`
- Inspect: `native/controller_native/controller_behavior_tests.cpp`
- Inspect: `docs/superpowers/plans/2026-06-14-native-2d-tracker-controller-recoil-plan.md`

- [ ] **Step 1: Confirm live tuning baseline**

Run the current native runtime in the actual game environment and decide whether the current feel is good enough to preserve before refactor.

Acceptance:

```text
ADS snap is not over-pulling in normal use.
Bodylock feels acceptable enough to be a rollback baseline.
Recoil interaction is acceptable enough to compare against later.
Aim-only perf log writes correctly.
```

- [ ] **Step 2: Run native build verification**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected:

```text
native build completes successfully
controller behavior tests complete successfully if the build script runs them
```

- [ ] **Step 3: Run runtime smoke**

Run:

```powershell
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once
```

Expected:

```text
process starts, loads config/model paths, runs one iteration or exits cleanly
no crash before entering live loop setup
```

- [ ] **Step 4: Review git state**

Run:

```powershell
git status --short
git diff --stat
```

Expected:

```text
all intended tuning, logging, and plan files are visible
no unexpected destructive or unrelated edits are included in the checkpoint
```

- [ ] **Step 5: Create checkpoint commit when user approves**

Run only after the user says the current tuning is the baseline:

```powershell
git add <accepted files>
git commit -m "checkpoint: native controller tuning baseline before tracker refactor"
```

Expected:

```text
commit succeeds
working tree contains only intentionally untracked or local-only files
```

Do not commit local-only secrets, private configs, generated logs, or heavyweight model artifacts unless the repository already intentionally tracks them.

## Wave 1: Shared Native Types, No Behavior Change

**Files:**
- Create: `native/common_native/time_types.h`
- Create: `native/common_native/screen_geometry.h`
- Create: `native/common_native/stick_types.h`
- Create: `native/common_native/authority_types.h`
- Modify: `native/vision_native/CMakeLists.txt`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [x] **Step 1: Add native time types**

Create `native/common_native/time_types.h` with explicit runtime timestamp wrappers:

```cpp
#pragma once

namespace common_native {

struct TimeSeconds {
    double value = 0.0;
};

struct DurationSeconds {
    double value = 0.0;
};

inline double duration_ms(DurationSeconds duration) {
    return duration.value * 1000.0;
}

inline DurationSeconds operator-(TimeSeconds newer, TimeSeconds older) {
    return DurationSeconds{newer.value - older.value};
}

}  // namespace common_native
```

- [x] **Step 2: Add screen geometry types**

Create `native/common_native/screen_geometry.h`:

```cpp
#pragma once

namespace common_native {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Box2f {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct ScreenSize {
    float width = 0.0f;
    float height = 0.0f;
};

}  // namespace common_native
```

- [x] **Step 3: Add stick component types**

Create `native/common_native/stick_types.h`:

```cpp
#pragma once

namespace common_native {

struct Stick2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct StickComponents {
    Stick2f manual;
    Stick2f assist;
    Stick2f dynamics;
    Stick2f recoil;
    Stick2f final_output;
};

}  // namespace common_native
```

- [x] **Step 4: Add authority enums**

Create `native/common_native/authority_types.h`:

```cpp
#pragma once

#include <cstdint>

namespace common_native {

enum class AssistAuthority : std::uint8_t {
    None = 0,
    AimObserved = 1,
    AimCoast = 2,
};

enum class FireAuthority : std::uint8_t {
    None = 0,
    ObservedOnly = 1,
};

}  // namespace common_native
```

- [x] **Step 5: Add a compile-only test**

Add a small test in `native/controller_native/controller_behavior_tests.cpp`:

```cpp
bool test_common_native_types_compile() {
    common_native::TimeSeconds now{10.0};
    common_native::TimeSeconds then{9.5};
    const auto dt = now - then;
    assert_near(static_cast<float>(common_native::duration_ms(dt)), 500.0f, 0.01f,
        "time wrapper should compute milliseconds");

    common_native::StickComponents components;
    components.final_output = {0.25f, -0.5f};
    assert_near(components.final_output.x, 0.25f, 0.0001f,
        "stick component should store x");

    const auto assist = common_native::AssistAuthority::AimObserved;
    const auto fire = common_native::FireAuthority::ObservedOnly;
    assert_true(assist == common_native::AssistAuthority::AimObserved,
        "assist authority enum should compare");
    assert_true(fire == common_native::FireAuthority::ObservedOnly,
        "fire authority enum should compare");
    return true;
}
```

Register the test in the existing test list using the same pattern already used in the file.

- [x] **Step 6: Run verification**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected:

```text
new compile-only test passes
no controller behavior changes
```

- [x] **Step 7: Commit**

```powershell
git add native/common_native native/vision_native/CMakeLists.txt native/controller_native/controller_behavior_tests.cpp
git commit -m "refactor: add shared native runtime contract types"
```

## Wave 2: Tracker Contract And Legacy Adapter

**Files:**
- Create: `native/tracking_native/tracker_contract.h`
- Create: `native/tracking_native/legacy_projection_tracker.h`
- Create: `native/tracking_native/legacy_projection_tracker.cpp`
- Modify: `native/controller_native/target_tracker.h`
- Modify: `native/controller_native/target_tracker.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [x] **Step 1: Add tracker contract**

Create `native/tracking_native/tracker_contract.h`:

```cpp
#pragma once

#include "../common_native/authority_types.h"
#include "../common_native/screen_geometry.h"
#include "../common_native/stick_types.h"
#include "../common_native/time_types.h"

namespace tracking_native {

enum class TrackerSnapshotSource {
    Absent,
    Observed,
    Projected,
    Coast,
};

struct TrackerObservation {
    bool has_target = false;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    const char* target_tier = "none";
    common_native::TimeSeconds capture_time;
};

struct TrackerControlSample {
    common_native::TimeSeconds apply_time;
    common_native::DurationSeconds dt;
    common_native::StickComponents sticks;
};

struct TrackerQuery {
    common_native::TimeSeconds query_time;
};

struct TrackerSnapshot {
    bool has_target = false;
    TrackerSnapshotSource source = TrackerSnapshotSource::Absent;
    common_native::Vec2f aim_error_px;
    common_native::Box2f body_box_px;
    bool has_body_box = false;
    common_native::TimeSeconds observed_at;
    double projection_age_ms = 0.0;
    common_native::AssistAuthority assist_authority = common_native::AssistAuthority::None;
    common_native::FireAuthority fire_authority = common_native::FireAuthority::None;
};

}  // namespace tracking_native
```

- [x] **Step 2: Add legacy adapter wrapper**

Create `native/tracking_native/legacy_projection_tracker.h`:

```cpp
#pragma once

#include "tracker_contract.h"
#include "../controller_native/target_tracker.h"

namespace tracking_native {

class LegacyProjectionTracker {
public:
    explicit LegacyProjectionTracker(
        controller_native::NativeTargetTrackerConfig config = {});

    void reset();
    void ingest(const TrackerObservation& observation);
    void push_control_sample(const TrackerControlSample& sample);
    TrackerSnapshot query(const TrackerQuery& query) const;

private:
    controller_native::NativeGamepadTargetTracker inner_;
};

}  // namespace tracking_native
```

Create `native/tracking_native/legacy_projection_tracker.cpp`:

```cpp
#include "legacy_projection_tracker.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace tracking_native {

namespace {

std::string normalized_tier(const char* tier) {
    std::string value = tier ? tier : "none";
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool is_weak_or_continuity_tier(const char* tier) {
    const std::string value = normalized_tier(tier);
    return value == "associated_weak" ||
        value == "weak" ||
        value == "weak_association" ||
        value == "weak_observed" ||
        value == "cue_hold";
}

bool is_strong_tier(const char* tier) {
    const std::string value = normalized_tier(tier);
    return value != "associated_weak" &&
        value != "weak" &&
        value != "weak_association" &&
        value != "weak_observed" &&
        value != "cue_hold" &&
        value != "predicted" &&
        value != "projected" &&
        value != "projection" &&
        value != "none" &&
        value != "lost";
}

}  // namespace

LegacyProjectionTracker::LegacyProjectionTracker(
    controller_native::NativeTargetTrackerConfig config)
    : inner_(config) {}

void LegacyProjectionTracker::reset() {
    inner_.reset();
}

void LegacyProjectionTracker::ingest(const TrackerObservation& observation) {
    controller_native::NativeTargetTrackerObservation native_observation;
    native_observation.has_target = observation.has_target;
    native_observation.dx = observation.aim_error_px.x;
    native_observation.dy = observation.aim_error_px.y;
    native_observation.target_tier = observation.target_tier ? observation.target_tier : "none";
    native_observation.observed_at_seconds = observation.capture_time.value;
    inner_.update_observation(native_observation);
}

void LegacyProjectionTracker::push_control_sample(const TrackerControlSample& sample) {
    const auto motion = sample.sticks.final_output;
    inner_.record_output(motion.x, motion.y, sample.dt.value);
}

TrackerSnapshot LegacyProjectionTracker::query(const TrackerQuery& query) const {
    TrackerSnapshot snapshot;
    const auto projection = inner_.project(query.query_time.value);
    if (!projection) {
        return snapshot;
    }

    snapshot.has_target = true;
    snapshot.source = TrackerSnapshotSource::Projected;
    snapshot.aim_error_px = {projection->dx, projection->dy};
    snapshot.observed_at = {projection->observed_at_seconds};
    snapshot.projection_age_ms =
        (query.query_time.value - projection->observed_at_seconds) * 1000.0;
    snapshot.assist_authority = common_native::AssistAuthority::AimCoast;
    snapshot.fire_authority = common_native::FireAuthority::None;
    return snapshot;
}

}  // namespace tracking_native
```

- [x] **Step 3: Add adapter parity tests**

Add tests that instantiate both `NativeGamepadTargetTracker` and `LegacyProjectionTracker` with the same config, then compare projection results after the same observation/control sample.

Expected test names:

```text
test_legacy_projection_tracker_matches_native_project_output
test_legacy_projection_tracker_expires_after_max_age
test_legacy_projection_tracker_empty_observation_clears_snapshot
```

- [x] **Step 4: Run verification**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

Expected:

```text
adapter tests pass
existing controller behavior tests pass
no live controller integration changed yet
```

- [x] **Step 5: Commit**

```powershell
git add native/tracking_native native/controller_native/controller_behavior_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "refactor: add legacy tracker contract adapter"
```

## Wave 3: Controller Pipeline Extraction

**Files:**
- Create: `native/controller_native/controller_tick_context.h`
- Create: `native/controller_native/controller_pipeline.h`
- Create: `native/controller_native/controller_pipeline.cpp`
- Create: `native/controller_native/output_mixer.h`
- Create: `native/controller_native/output_mixer.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/native_gamepad_controller.h`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Define tick context**

Create a tick context type containing physical input, current vision/tracker snapshot, recoil state, config-derived state, and timestamps. The first version should be a passive struct and should not change output behavior.

Required fields:

```text
input state
latest vision state
tracker snapshot
current time
delta time
ADS state
fire state
weapon/recoil state
```

- [ ] **Step 2: Define output components**

Create an output component struct:

```text
manual stick
ai aim stick
dynamic adjustment stick
recoil stick
final stick
fire button state
```

This type should become the source for `TrackerControlSample` later.

- [ ] **Step 3: Move build-output stages behind named functions**

Split the current `NativeGamepadController::build_output` order into explicit stage calls:

```text
read base input
apply_ai_aim
apply_aim_assist_dynamics
apply_auto_fire
capture tracker motion sample
apply_recoil
clamp/output
record tracker sample
```

The function order must remain unchanged in this wave.

- [ ] **Step 4: Add stage-order test**

Add a controller behavior test that proves tracker recording still happens from the same pre-recoil motion component as the current baseline.

Expected invariant:

```text
recoil-only output does not move tracker projection in default mode
manual/assist movement still moves tracker projection
```

- [ ] **Step 5: Run verification**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
```

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native native/vision_native/CMakeLists.txt
git commit -m "refactor: extract native controller pipeline stages"
```

## Wave 4: Authority Model Unification

**Files:**
- Create: `native/tracking_native/tracker_authority.h`
- Create: `native/tracking_native/tracker_authority.cpp`
- Modify: `native/controller_native/target_tracker.cpp`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Move target-tier classification into one helper**

Create a helper that maps target tier strings to:

```text
strong observed
weak continuity
projected/predicted
lost/none
```

- [ ] **Step 2: Replace duplicated tier checks**

Replace duplicated checks in:

```text
target_tracker.cpp
native_gamepad_controller.cpp
ai_aim.cpp if it parses the same strings
```

- [ ] **Step 3: Route controller decisions through authority**

Controller code should read:

```text
AssistAuthority for ADS/bodylock
FireAuthority for auto-fire/fire gate
```

It should not decide fire authority from tier strings directly.

- [ ] **Step 4: Add authority tests**

Required tests:

```text
strong observed target can produce assist authority
weak/cue target can assist only as coast/low authority
projected target never has fire authority
missed target clears fire authority
auto-fire ignores projected-only snapshots
```

- [ ] **Step 5: Run verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
git add native/tracking_native native/controller_native native/vision_native/CMakeLists.txt
git commit -m "refactor: centralize tracker authority decisions"
```

## Wave 5: Tracker Backend Interface And Reference Package Import

**Files:**
- Create: `native/tracking_native/tracker_backend.h`
- Create: `native/tracking_native/projection_model.h`
- Create: `native/tracking_native/projection_model.cpp`
- Create: `native/tracking_native/ego_motion_buffer.h`
- Create: `native/tracking_native/ego_motion_buffer.cpp`
- Create: `native/tracking_native/kalman_tracker.h`
- Create: `native/tracking_native/kalman_tracker.cpp`
- Create: `native/tracking_native/association.h`
- Create: `native/tracking_native/association.cpp`
- Modify: `native/vision_native/CMakeLists.txt`
- Modify: `native/controller_native/runtime_config.h`
- Modify: `native/controller_native/runtime_config.cpp`
- Modify: `config.toml`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Add backend enum**

Add config support for:

```text
tracker_backend = "legacy_projection"
tracker_backend = "kalman_experimental"
```

Default must remain:

```text
legacy_projection
```

- [ ] **Step 2: Add backend interface**

The interface should expose:

```text
reset()
ingest(TrackerObservation)
push_control_sample(TrackerControlSample)
query(TrackerQuery) -> TrackerSnapshot
debug_tracks()
```

- [ ] **Step 3: Keep legacy backend as default implementation**

Wrap the current adapter as the default backend.

- [ ] **Step 4: Import only low-risk reference components first**

Import or adapt:

```text
math helpers
projection model
authority type ideas
```

Do not import full `fps::TargetTracker` in this step.

- [ ] **Step 5: Add experimental Kalman backend behind config**

The experimental backend can use package ideas, but it must be off by default.

- [ ] **Step 6: Add backend parity tests**

Required tests:

```text
default config chooses legacy_projection
unknown backend fails config load clearly
experimental backend can be constructed
legacy backend behavior remains unchanged under default config
```

- [ ] **Step 7: Run verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
git add native/tracking_native native/controller_native native/vision_native/CMakeLists.txt config.toml
git commit -m "refactor: add tracker backend interface"
```

## Wave 6: Recoil Boundary Extraction

**Files:**
- Create: `native/recoil_native/recoil_compensation.h`
- Create: `native/recoil_native/recoil_compensation.cpp`
- Create: `native/recoil_native/recoil_visual_model.h`
- Create: `native/recoil_native/recoil_visual_model.cpp`
- Create: `native/recoil_native/recoil_debug.h`
- Modify: `native/controller_native/native_gamepad_controller.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Move recoil compensation behind explicit policy**

The recoil policy should output a recoil stick component, not directly mutate the same output state that tracker consumes.

- [ ] **Step 2: Add disabled recoil visual model**

Default behavior:

```text
recoil visual displacement disabled
visual displacement returns zero
tracker ego-motion remains pre-recoil/default mode
```

- [ ] **Step 3: Add recoil component tests**

Required tests:

```text
default recoil visual model returns zero displacement
anti-recoil component is visible in output component logs
tracker default ego-motion excludes anti-recoil component
experimental final-stick mode requires explicit config enable
```

- [ ] **Step 4: Run verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
git add native/recoil_native native/controller_native native/vision_native/CMakeLists.txt
git commit -m "refactor: split recoil compensation from tracker motion"
```

## Wave 7: Replay And Benchmark Harness

**Files:**
- Create: `native/replay_native/replay_schema.h`
- Create: `native/replay_native/replay_reader.h`
- Create: `native/replay_native/replay_reader.cpp`
- Create: `native/replay_native/replay_metrics.h`
- Create: `native/replay_native/replay_metrics.cpp`
- Create: `native/runtime_app/native_replay_writer.h`
- Create: `native/runtime_app/native_replay_writer.cpp`
- Create: `native/runtime_app/native_replay_runner.h`
- Create: `native/runtime_app/native_replay_runner.cpp`
- Modify: `native/runtime_app/aim_perf_file_logger.*`
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Define replay schema**

Replay frames should include:

```text
vision capture time
vision ready time
ROI transform
all detections
selected target
tracker snapshot
assist authority
fire authority
target velocity estimate
velocity consistency score
prediction/lead horizon
manual input
assist/recoil/final stick components
ADS/fire state
weapon/recoil state
stage timing
```

- [ ] **Step 2: Extend aim-only perf logs**

Add tracker/controller fields without making per-frame logging mandatory in normal runs.

- [ ] **Step 3: Add replay runner for deterministic comparison**

Replay should compare:

```text
legacy projection backend
experimental Kalman backend
detector-only baseline
```

- [ ] **Step 4: Add benchmark metrics**

Required metrics:

```text
p50/p95/p99 target error
projection age
wrong-lock duration
reacquire time
follow-lag error on sustained one-direction motion
overshoot after direction reversal
predicted-only fire violations
stale fire violations
CPU time per tick
```

- [ ] **Step 5: Run verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
git add native/replay_native native/runtime_app native/vision_native/CMakeLists.txt
git commit -m "feat: add native tracker replay benchmark harness"
```

## Wave 8: Controller Feel Policies

**Files:**
- Create: `native/controller_native/ads_snap_policy.h`
- Create: `native/controller_native/ads_snap_policy.cpp`
- Create: `native/controller_native/bodylock_policy.h`
- Create: `native/controller_native/bodylock_policy.cpp`
- Create: `native/controller_native/manual_intent.h`
- Create: `native/controller_native/manual_intent.cpp`
- Modify: `native/controller_native/ai_aim.cpp`
- Modify: `native/controller_native/ai_aim.h`
- Modify: `native/controller_native/aim_assist_dynamics.cpp`
- Modify: `native/controller_native/controller_behavior_tests.cpp`

- [ ] **Step 1: Extract ADS snap policy**

ADS snap should take:

```text
tracker snapshot
assist authority
manual input
current ADS state
config
```

It should output:

```text
snap stick component
snap debug fields
```

- [ ] **Step 2: Extract bodylock policy**

Bodylock should take the same tracker snapshot, plus short-term body motion if still needed.

Before moving velocity prediction into tracker, audit existing:

```text
NativeAiAim::observe_body_lock_motion
motion_velocity_x_
motion_velocity_y_
```

- [ ] **Step 3: Extract manual intent policy**

Manual intent policy owns:

```text
when user is clearly correcting
when assist can override
when assist must yield
how dynamic curve straightening applies
```

- [ ] **Step 4: Add aggressive mode behind config**

Aggressive bodylock can be tested only when:

```text
snapshot source is observed or very fresh projected
assist authority allows it
manual intent does not reject it
projection age cap is satisfied
```

- [ ] **Step 5: Add sustained-motion follow boost**

This is the optimization for targets that keep moving in one direction. The tracker should expose velocity and consistency; controller policy should decide how much extra follow speed or one-tick lead is allowed.

Enable the boost only when:

```text
target has at least N fresh strong observations
velocity direction is stable within a configured angle threshold
velocity magnitude is above a minimum movement threshold
target identity has not switched
projection age is below the fresh-observed cap
manual intent does not indicate the player is pulling away
```

Disable or decay the boost when:

```text
direction reverses
innovation jumps
target switches
ADS state changes
snapshot is weak/coast/projected-only
manual override is active
recoil window is marked unstable
```

Prediction should be capped:

```text
lead_horizon_ms <= one controller tick by default
lead_horizon_ms <= configured maximum
lead never grants fire authority
lead affects aim command only, not target identity
```

- [ ] **Step 6: Add feel-policy tests**

Required tests:

```text
observed target allows high correction
projected target correction is capped
weak/coast target cannot use high-force ADS snap
manual correction away from target reduces assist
wrong initial aim can be corrected quickly on fresh observed target
upward ADS over-pull case is bounded
sustained one-direction target motion increases follow gain after N observations
next-tick prediction is capped to the configured horizon
weak/coast/projected-only target cannot use sustained-motion boost
direction reversal clears boost within one controller tick
manual override disables sustained-motion boost
```

- [ ] **Step 7: Run verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
git add native/controller_native native/vision_native/CMakeLists.txt
git commit -m "refactor: split native aim feel policies"
```

## Wave 9: Runtime Orchestration Cleanup

**Files:**
- Modify: `native/runtime_app/runtime_loop.cpp`
- Modify: `native/runtime_app/runtime_loop.h`
- Modify: `native/runtime_app/main.cpp`
- Modify: `native/runtime_app/aim_perf_file_logger.*`
- Modify: `native/controller_native/native_gamepad_controller.*`
- Modify: `docs/project/NATIVE_CPP_RUNTIME.md`

- [ ] **Step 1: Keep runtime loop orchestration-only**

`runtime_loop` should coordinate:

```text
capture
inference
tracker ingest/query
controller tick
output write
logging
shutdown
```

It should not contain controller tuning logic.

- [ ] **Step 2: Keep gamepad controller control-only**

`NativeGamepadController` should become a thin owner of:

```text
physical input state
pipeline/policies
virtual output
tracker backend reference
recoil policy reference
```

- [ ] **Step 3: Add shutdown invariants**

Required behavior:

```text
virtual gamepad output neutralizes on stop
tracker stops receiving samples after shutdown
perf logger closes file cleanly
```

- [ ] **Step 4: Update docs**

Update `docs/project/NATIVE_CPP_RUNTIME.md` with:

```text
default backend
tracker authority model
recoil default boundary
experimental backend switches
replay benchmark workflow
rollback checkpoint
```

- [ ] **Step 5: Run verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once
git add native/runtime_app native/controller_native docs/project/NATIVE_CPP_RUNTIME.md
git commit -m "refactor: clarify native runtime orchestration boundaries"
```

## Wave 10: Remove Transitional Coupling

**Files:**
- Delete or retire: `native/controller_native/target_tracker.h`
- Delete or retire: `native/controller_native/target_tracker.cpp`
- Modify: `native/tracking_native/*`
- Modify: `native/controller_native/*`
- Modify: `native/vision_native/CMakeLists.txt`
- Modify: `docs/project/NATIVE_CPP_RUNTIME.md`
- Modify: `.agent-context/handoff.md` only after user-confirmed SyncSet

- [ ] **Step 1: Remove old include paths**

Only after all callers use `tracking_native`, remove direct controller-private tracker includes.

- [ ] **Step 2: Remove string-tier checks outside authority helper**

No controller policy should parse raw target tier strings for authority decisions.

- [ ] **Step 3: Remove duplicated motion estimates where possible**

If tracker now owns body motion adequately, remove or reduce `NativeAiAim` internal motion tracking. If the internal bodylock estimate remains useful, document it as a short-horizon controller-local estimate, not target identity tracking.

- [ ] **Step 4: Update context**

Prepare a `.agent-context` SyncSet recording:

```text
accepted tracker/controller/recoil architecture
default tracker backend
recoil visual model status
rollback checkpoint commit
test commands
```

Write only after user confirms the SyncSet.

- [ ] **Step 5: Final verification and commit**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_native_vision.ps1
native\vision_native\build\Release\cod_native_runtime.exe --config config.toml --once
git add native docs .agent-context
git commit -m "refactor: complete native tracker controller recoil split"
```

## Acceptance Gates

Do not promote a refactor wave into default live behavior unless all applicable gates pass:

- Native C++ build passes.
- Focused unit tests for that behavior pass.
- Existing controller behavior tests pass.
- Runtime `--once` smoke passes.
- Aim-only perf logging still works.
- Predicted/coasting targets have no fire authority.
- Recoil default behavior matches the checkpoint unless the user intentionally changes it.
- Manual live A/B against the checkpoint does not show worse pull, drag, or ADS over-shoot.

## Rollback Strategy

Every wave should be independently revertible. The baseline checkpoint is the rollback anchor.

Keep these defaults until live evidence supports changing them:

```text
tracker_backend = legacy_projection
recoil_visual_model = disabled
tracker ego-motion = pre-recoil motion component
fire authority = observed only
projected target snap strength = capped
```

## What To Delegate Later

Use the local executor for mechanical implementation of individual waves after the checkpoint exists.

Recommended delegation shape:

```text
One executor run per wave.
Executor writes tests first.
Executor keeps behavior unchanged unless the wave explicitly changes behavior.
Codex reviews diffs and test output before moving to the next wave.
```

Use deeper reasoning only for:

```text
Kalman backend design
recoil visual model calibration
controller feel policy changes
unexpected live-feel regressions
```
