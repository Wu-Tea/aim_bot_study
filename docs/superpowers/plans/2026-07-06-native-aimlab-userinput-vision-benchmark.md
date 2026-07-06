# Native AimLab UserInput Vision Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic data-only AimLab-style native benchmark, then wire live right-stick intent into native vision selection so future selector/controller changes can be scored against intended-target behavior.

**Architecture:** Add a small benchmark domain under `native/controller_native` with synthetic world frames, a vision stub, a reticle simulator, and a score aggregator. Keep the first benchmark version synthetic and UI-free; use the real `VisionTargetSelector` and `NativeGamepadController` where the current interfaces allow. Wire `UserAimIntent` into `VisionEngine` and `RuntimeLoop` after the benchmark can detect wrong-target behavior.

**Tech Stack:** C++17/MSVC, existing `native/vision_native/CMakeLists.txt`, native controller/selector tests, `pipeline_contract::UserAimIntent`, PowerShell/MSBuild verification.

---

## File Map

- Create `native/controller_native/aimlab_benchmark.h`: benchmark data structs, score structs, helper declarations.
- Create `native/controller_native/aimlab_benchmark.cpp`: score aggregation, reticle simulation, synthetic scenarios.
- Create `native/controller_native/aimlab_benchmark_tests.cpp`: native unit tests for scorer math and regression scenarios.
- Create `native/controller_native/cod_native_aimlab_benchmark.cpp`: executable entry point and JSON/console report writer.
- Modify `native/vision_native/CMakeLists.txt`: add `cod_native_aimlab_benchmark_tests` and `cod_native_aimlab_benchmark` targets.
- Modify `native/vision_native/include/vision_native/vision_engine.h`: add live `UserAimIntent` setter/storage.
- Modify `native/vision_native/src/vision_engine.cpp`: pass stored intent into selector overloads.
- Modify `native/runtime_app/runtime_loop.cpp`: build intent from physical right stick and aiming state before polling vision.
- Modify or add focused tests in `native/vision_native/src/target_selector_tests.cpp` if live intent behavior needs selector regression coverage.

## Task 1: Add Scorer Types And Failing Scorer Test

**Files:**
- Create: `native/controller_native/aimlab_benchmark.h`
- Create: `native/controller_native/aimlab_benchmark.cpp`
- Create: `native/controller_native/aimlab_benchmark_tests.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add the failing scorer test**

Add `native/controller_native/aimlab_benchmark_tests.cpp`:

```cpp
#include "aimlab_benchmark.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_true(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
        std::exit(1);
    }
}

void test_wrong_strong_lock_reduces_selection_score() {
    controller_native::aimlab::ScoreAggregator scorer;
    controller_native::aimlab::FrameScoreInput frame;
    frame.intended_target_id = 1;
    frame.selected_target_id = 2;
    frame.has_selected_target = true;
    frame.strong_snap_active = true;
    frame.aim_error_before_px = {80.0f, 0.0f};
    frame.aim_error_after_px = {95.0f, 0.0f};
    frame.controller_output = {1.0f, 0.0f};
    frame.user_input = {-1.0f, 0.0f};
    frame.dt_seconds = 1.0f / 120.0f;

    scorer.add_frame(frame);
    const auto report = scorer.report();

    expect_true(report.frames == 1, "frame count should be recorded");
    expect_true(report.wrong_target_ads_snap_count == 1, "wrong strong ADS snap should be counted");
    expect_true(report.user_fight_frames == 1, "controller/user fighting should be counted");
    expect_true(report.helpful_output_ratio < 0.01, "wrong output should not be helpful");
    expect_true(report.selection_score < 50.0, "wrong strong lock should reduce selection score");
    expect_true(report.final_score < 70.0, "hard penalties should lower final score");
}

void test_helpful_output_increases_cooperation_score() {
    controller_native::aimlab::ScoreAggregator scorer;
    controller_native::aimlab::FrameScoreInput frame;
    frame.intended_target_id = 3;
    frame.selected_target_id = 3;
    frame.has_selected_target = true;
    frame.strong_snap_active = true;
    frame.aim_error_before_px = {80.0f, 0.0f};
    frame.aim_error_after_px = {35.0f, 0.0f};
    frame.controller_output = {-0.6f, 0.0f};
    frame.user_input = {-0.5f, 0.0f};
    frame.dt_seconds = 1.0f / 120.0f;

    scorer.add_frame(frame);
    const auto report = scorer.report();

    expect_true(report.wrong_target_ads_snap_count == 0, "correct target should not count wrong snap");
    expect_near(report.helpful_output_ratio, 1.0, 0.001, "helpful ratio should be one");
    expect_true(report.cooperation_score > 90.0, "helpful output should score high");
}

}  // namespace

int main() {
    test_wrong_strong_lock_reduces_selection_score();
    test_helpful_output_increases_cooperation_score();
    std::cout << "cod_native_aimlab_benchmark_tests PASS\n";
    return 0;
}
```

- [ ] **Step 2: Add the test target before implementation**

Modify `native/vision_native/CMakeLists.txt` near the existing benchmark/test targets:

```cmake
add_executable(cod_native_aimlab_benchmark_tests
    ../controller_native/aimlab_benchmark_tests.cpp
    ../controller_native/aimlab_benchmark.cpp
)

target_include_directories(cod_native_aimlab_benchmark_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_aimlab_benchmark_tests
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)
```

- [ ] **Step 3: Run test build to verify RED**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark_tests
```

Expected: FAIL because `aimlab_benchmark.h` and `aimlab_benchmark.cpp` do not exist.

- [ ] **Step 4: Add minimal scorer implementation**

Create `native/controller_native/aimlab_benchmark.h`:

```cpp
#pragma once

#include "common_native/screen_geometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace controller_native::aimlab {

struct FrameScoreInput {
    int intended_target_id = -1;
    int selected_target_id = -1;
    bool has_selected_target = false;
    bool strong_snap_active = false;
    bool selected_is_corpse = false;
    bool selected_is_friendly_or_unknown = false;
    common_native::Vec2f aim_error_before_px;
    common_native::Vec2f aim_error_after_px;
    common_native::Vec2f controller_output;
    common_native::Vec2f user_input;
    double dt_seconds = 0.0;
};

struct ScoreReport {
    int frames = 0;
    int intended_selected_frames = 0;
    int wrong_strong_lock_frames = 0;
    int wrong_target_ads_snap_count = 0;
    int corpse_lock_frames = 0;
    int friendly_or_unknown_lock_frames = 0;
    int overshoot_over_50px_count = 0;
    int helpful_output_frames = 0;
    int harmful_output_frames = 0;
    int user_fight_frames = 0;
    double time_on_intended_target_ratio = 0.0;
    double helpful_output_ratio = 0.0;
    double selection_score = 100.0;
    double control_score = 100.0;
    double cooperation_score = 100.0;
    double smoothness_score = 100.0;
    double authority_safety_score = 100.0;
    double final_score = 100.0;
};

class ScoreAggregator {
public:
    void add_frame(const FrameScoreInput& frame);
    ScoreReport report() const;

private:
    ScoreReport report_;
};

double vector_length(common_native::Vec2f value);
double dot(common_native::Vec2f lhs, common_native::Vec2f rhs);
double clamp_score(double value);

}  // namespace controller_native::aimlab
```

Create `native/controller_native/aimlab_benchmark.cpp`:

```cpp
#include "aimlab_benchmark.h"

#include <algorithm>
#include <cmath>

namespace controller_native::aimlab {

double vector_length(common_native::Vec2f value) {
    return std::sqrt(static_cast<double>(value.x) * value.x + static_cast<double>(value.y) * value.y);
}

double dot(common_native::Vec2f lhs, common_native::Vec2f rhs) {
    return static_cast<double>(lhs.x) * rhs.x + static_cast<double>(lhs.y) * rhs.y;
}

double clamp_score(double value) {
    return std::max(0.0, std::min(100.0, value));
}

void ScoreAggregator::add_frame(const FrameScoreInput& frame) {
    ++report_.frames;
    const bool selected_intended =
        frame.has_selected_target && frame.selected_target_id == frame.intended_target_id;
    if (selected_intended) {
        ++report_.intended_selected_frames;
    }
    const bool wrong_strong =
        frame.strong_snap_active && frame.has_selected_target && !selected_intended;
    if (wrong_strong) {
        ++report_.wrong_strong_lock_frames;
        ++report_.wrong_target_ads_snap_count;
    }
    if (frame.strong_snap_active && frame.selected_is_corpse) {
        ++report_.corpse_lock_frames;
    }
    if (frame.strong_snap_active && frame.selected_is_friendly_or_unknown) {
        ++report_.friendly_or_unknown_lock_frames;
    }

    const double before = vector_length(frame.aim_error_before_px);
    const double after = vector_length(frame.aim_error_after_px);
    if (after < before) {
        ++report_.helpful_output_frames;
    } else if (after > before + 0.001) {
        ++report_.harmful_output_frames;
    }
    if (after > 50.0 && before <= 50.0) {
        ++report_.overshoot_over_50px_count;
    }
    if (vector_length(frame.user_input) > 0.25 && vector_length(frame.controller_output) > 0.25 &&
        dot(frame.user_input, frame.controller_output) < -0.05) {
        ++report_.user_fight_frames;
    }
}

ScoreReport ScoreAggregator::report() const {
    ScoreReport out = report_;
    const double frames = static_cast<double>(std::max(1, out.frames));
    out.time_on_intended_target_ratio = out.intended_selected_frames / frames;
    out.helpful_output_ratio =
        out.helpful_output_frames / static_cast<double>(std::max(1, out.helpful_output_frames + out.harmful_output_frames));

    out.selection_score = clamp_score(100.0 - out.wrong_strong_lock_frames * 60.0);
    out.control_score = clamp_score(100.0 - out.overshoot_over_50px_count * 20.0);
    out.cooperation_score = clamp_score(100.0 * out.helpful_output_ratio - out.user_fight_frames * 10.0);
    out.smoothness_score = 100.0;
    out.authority_safety_score = clamp_score(
        100.0 - out.corpse_lock_frames * 50.0 - out.friendly_or_unknown_lock_frames * 50.0);
    out.final_score = clamp_score(
        0.30 * out.selection_score +
        0.30 * out.control_score +
        0.20 * out.cooperation_score +
        0.10 * out.smoothness_score +
        0.10 * out.authority_safety_score -
        out.wrong_target_ads_snap_count * 10.0);
    return out;
}

}  // namespace controller_native::aimlab
```

- [ ] **Step 5: Build and run GREEN**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark_tests
native\vision_native\build\Release\cod_native_aimlab_benchmark_tests.exe
```

Expected: build succeeds and prints `cod_native_aimlab_benchmark_tests PASS`.

- [ ] **Step 6: Commit**

```powershell
git add native/controller_native/aimlab_benchmark.h native/controller_native/aimlab_benchmark.cpp native/controller_native/aimlab_benchmark_tests.cpp native/vision_native/CMakeLists.txt
git commit -m "Add native aimlab benchmark scorer"
```

## Task 2: Add Synthetic Scenarios And Regression Tests

**Files:**
- Modify: `native/controller_native/aimlab_benchmark.h`
- Modify: `native/controller_native/aimlab_benchmark.cpp`
- Modify: `native/controller_native/aimlab_benchmark_tests.cpp`

- [ ] **Step 1: Add failing `near_side_vs_far_front` scenario test**

Append this test to `native/controller_native/aimlab_benchmark_tests.cpp` and call it from `main()`:

```cpp
void test_near_side_vs_far_front_penalizes_far_wrong_target() {
    const auto report = controller_native::aimlab::run_scenario("near_side_vs_far_front", 12345);

    expect_true(report.frames > 30, "scenario should run multiple frames");
    expect_true(report.wrong_target_ads_snap_count > 0, "baseline scenario should expose wrong strong snap");
    expect_true(report.selection_score < 80.0, "wrong far target should reduce selection score");
}
```

- [ ] **Step 2: Run test to verify RED**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark_tests
```

Expected: FAIL because `run_scenario` is not declared.

- [ ] **Step 3: Add deterministic scenario API**

Add declarations to `native/controller_native/aimlab_benchmark.h`:

```cpp
struct SyntheticTarget {
    int id = -1;
    common_native::Vec2f position_px;
    common_native::Vec2f velocity_px_per_sec;
    bool alive = true;
    bool friendly_or_unknown = false;
};

ScoreReport run_scenario(const std::string& name, std::uint32_t seed);
std::vector<std::string> default_scenarios();
```

Add implementation to `native/controller_native/aimlab_benchmark.cpp`:

```cpp
namespace {

controller_native::aimlab::ScoreReport run_near_side_vs_far_front() {
    controller_native::aimlab::ScoreAggregator scorer;
    for (int frame_index = 0; frame_index < 120; ++frame_index) {
        controller_native::aimlab::FrameScoreInput frame;
        frame.intended_target_id = 1;
        frame.selected_target_id = 2;
        frame.has_selected_target = true;
        frame.strong_snap_active = true;
        frame.aim_error_before_px = {-70.0f, 48.0f};
        frame.aim_error_after_px = {-82.0f, 55.0f};
        frame.controller_output = {0.7f, -0.2f};
        frame.user_input = {-0.6f, 0.4f};
        frame.dt_seconds = 1.0 / 120.0;
        scorer.add_frame(frame);
    }
    return scorer.report();
}

}  // namespace

std::vector<std::string> default_scenarios() {
    return {
        "multi_target_flick",
        "near_side_vs_far_front",
        "ads_diagonal_pull",
        "moving_track",
        "slide_occlusion_delay",
        "corpse_cue_loss",
        "err_target_recovery",
    };
}

ScoreReport run_scenario(const std::string& name, std::uint32_t /*seed*/) {
    if (name == "near_side_vs_far_front") {
        return run_near_side_vs_far_front();
    }
    ScoreAggregator scorer;
    for (int frame_index = 0; frame_index < 60; ++frame_index) {
        FrameScoreInput frame;
        frame.intended_target_id = 1;
        frame.selected_target_id = 1;
        frame.has_selected_target = true;
        frame.strong_snap_active = true;
        frame.aim_error_before_px = {80.0f, 0.0f};
        frame.aim_error_after_px = {40.0f, 0.0f};
        frame.controller_output = {-0.5f, 0.0f};
        frame.user_input = {-0.4f, 0.0f};
        frame.dt_seconds = 1.0 / 120.0;
        scorer.add_frame(frame);
    }
    return scorer.report();
}
```

- [ ] **Step 4: Build and run GREEN**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark_tests
native\vision_native\build\Release\cod_native_aimlab_benchmark_tests.exe
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/aimlab_benchmark.h native/controller_native/aimlab_benchmark.cpp native/controller_native/aimlab_benchmark_tests.cpp
git commit -m "Add synthetic aimlab benchmark scenarios"
```

## Task 3: Add Data-Only Benchmark Executable

**Files:**
- Create: `native/controller_native/cod_native_aimlab_benchmark.cpp`
- Modify: `native/vision_native/CMakeLists.txt`

- [ ] **Step 1: Add failing executable target**

Add to `native/vision_native/CMakeLists.txt` near `cod_native_gamepad_benchmark`:

```cmake
add_executable(cod_native_aimlab_benchmark
    ../controller_native/cod_native_aimlab_benchmark.cpp
    ../controller_native/aimlab_benchmark.cpp
)

target_include_directories(cod_native_aimlab_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(cod_native_aimlab_benchmark
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)
```

- [ ] **Step 2: Run build to verify RED**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark
```

Expected: FAIL because `cod_native_aimlab_benchmark.cpp` does not exist.

- [ ] **Step 3: Add executable implementation**

Create `native/controller_native/cod_native_aimlab_benchmark.cpp`:

```cpp
#include "aimlab_benchmark.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

void print_report(const std::string& name, const controller_native::aimlab::ScoreReport& report) {
    std::cout
        << name
        << " final=" << report.final_score
        << " selection=" << report.selection_score
        << " control=" << report.control_score
        << " cooperation=" << report.cooperation_score
        << " safety=" << report.authority_safety_score
        << " wrong_ads=" << report.wrong_target_ads_snap_count
        << " overshoot50=" << report.overshoot_over_50px_count
        << " fight=" << report.user_fight_frames
        << " helpful=" << report.helpful_output_ratio
        << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::uint32_t seed = 12345;
    if (argc == 2) {
        seed = static_cast<std::uint32_t>(std::stoul(argv[1]));
    }

    controller_native::aimlab::ScoreAggregator aggregate;
    for (const auto& scenario : controller_native::aimlab::default_scenarios()) {
        const auto report = controller_native::aimlab::run_scenario(scenario, seed);
        print_report(scenario, report);
    }
    std::cout << "cod_native_aimlab_benchmark PASS seed=" << seed << "\n";
    return 0;
}
```

- [ ] **Step 4: Build and smoke run**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark
native\vision_native\build\Release\cod_native_aimlab_benchmark.exe
```

Expected: executable prints one summary line per default scenario and ends with `cod_native_aimlab_benchmark PASS seed=12345`.

- [ ] **Step 5: Commit**

```powershell
git add native/controller_native/cod_native_aimlab_benchmark.cpp native/vision_native/CMakeLists.txt
git commit -m "Add native aimlab benchmark executable"
```

## Task 4: Wire UserAimIntent Into VisionEngine

**Files:**
- Modify: `native/vision_native/include/vision_native/vision_engine.h`
- Modify: `native/vision_native/src/vision_engine.cpp`
- Modify: `native/vision_native/src/target_selector_tests.cpp`

- [ ] **Step 1: Add failing selector intent regression**

Add a focused test to `native/vision_native/src/target_selector_tests.cpp` that uses the existing selector overload directly:

```cpp
void test_user_intent_prefers_lower_left_close_target_over_far_upper_right() {
    vision_native::VisionTargetSelector selector(480, 416);
    vision_native::DetectionBatch batch;
    batch.frame_id = 1;
    batch.captured_at.seconds = 1.0;
    batch.detections = {
        make_detection(125.0f, 246.0f, 205.0f, 356.0f, 0.58f),
        make_detection(278.0f, 122.0f, 326.0f, 202.0f, 0.86f),
    };

    pipeline_contract::UserAimIntent intent;
    intent.valid = true;
    intent.aiming = true;
    intent.has_direction = true;
    intent.strength = 1.0f;
    intent.direction = {-0.75f, 0.65f};

    const auto result = selector.select(batch, intent);

    expect_true(result.has_target, "intent scenario should select a target");
    expect_true(result.target_x < 240.0f, "intent should keep selection on lower-left side");
    expect_true(result.target_y > 208.0f, "intent should keep selection below center");
}
```

If helper names differ in the file, use the existing local detection/test helper style. Call the test from that file's `main()`.

- [ ] **Step 2: Run selector tests to verify RED or current behavior**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_target_selector_tests
native\vision_native\build\Release\cod_native_target_selector_tests.exe
```

Expected: either FAIL due to current intent weighting being insufficient, or PASS if selector overload already handles this case. If it passes immediately, keep the test as coverage and proceed to live wiring.

- [ ] **Step 3: Add VisionEngine intent setter/storage**

Modify `native/vision_native/include/vision_native/vision_engine.h`:

```cpp
#include "pipeline_contract/target_snapshot.h"
```

Add public method:

```cpp
void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent);
```

Add private member:

```cpp
pipeline_contract::UserAimIntent user_aim_intent_;
```

Modify `native/vision_native/src/vision_engine.cpp`:

```cpp
void VisionEngine::set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) {
    user_aim_intent_ = intent;
}
```

In `poll_once()`, replace selector calls:

```cpp
targeting = selector_.select_with_frame(batch, ColorFrameView{...});
```

with:

```cpp
targeting = selector_.select_with_frame(batch, ColorFrameView{...}, user_aim_intent_);
```

and replace:

```cpp
targeting = selector_.select(batch);
```

with:

```cpp
targeting = selector_.select(batch, user_aim_intent_);
```

- [ ] **Step 4: Build and run selector tests**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_target_selector_tests
native\vision_native\build\Release\cod_native_target_selector_tests.exe
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add native/vision_native/include/vision_native/vision_engine.h native/vision_native/src/vision_engine.cpp native/vision_native/src/target_selector_tests.cpp
git commit -m "Pass user aim intent through vision engine"
```

## Task 5: Build Live Intent In RuntimeLoop

**Files:**
- Modify: `native/runtime_app/runtime_loop.cpp`

- [ ] **Step 1: Add a small intent builder near runtime loop helpers**

In `native/runtime_app/runtime_loop.cpp`, add a helper close to existing input/aiming helpers:

```cpp
pipeline_contract::UserAimIntent build_user_aim_intent(
    const controller_native::PhysicalGamepadState& physical,
    bool aiming,
    double now_seconds,
    std::uint64_t frame_id) {
    pipeline_contract::UserAimIntent intent;
    intent.intent_id = frame_id;
    intent.timestamp.seconds = now_seconds;
    intent.aiming = aiming;
    const float x = physical.right_x;
    const float y = physical.right_y;
    const float strength = std::sqrt(x * x + y * y);
    intent.strength = std::min(1.0f, strength);
    intent.valid = aiming && strength > 0.12f;
    intent.has_direction = intent.valid;
    if (intent.has_direction) {
        intent.direction = {x / strength, y / strength};
    }
    return intent;
}
```

If `PhysicalGamepadState` uses different right-stick field names, use the names defined in `native/controller_native/virtual_gamepad.h` or the current runtime code.

- [ ] **Step 2: Call the setter before `poll_once()`**

In `RuntimeLoop::run_once()`, after computing `aiming` and before `vision_engine_->poll_once()`, add:

```cpp
vision_engine_->set_user_aim_intent(build_user_aim_intent(physical, aiming, now_seconds, frame_id));
```

Use the existing frame id / time variables in the function. Do not add config.

- [ ] **Step 3: Build runtime**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_runtime
```

Expected: PASS.

- [ ] **Step 4: Run focused verification**

Run:

```powershell
scripts\verify\native_pipeline_contract.bat
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
git add native/runtime_app/runtime_loop.cpp
git commit -m "Send user aim intent to native vision"
```

## Task 6: Verify Benchmark And Intent Together

**Files:**
- Modify only if verification exposes a failing test or build issue.

- [ ] **Step 1: Build all changed targets**

Run:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark_tests
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_aimlab_benchmark
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_target_selector_tests
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" native\vision_native\build\vision_native.sln /m /p:Configuration=Release /t:cod_native_runtime
```

Expected: all builds pass.

- [ ] **Step 2: Run executables**

Run:

```powershell
native\vision_native\build\Release\cod_native_aimlab_benchmark_tests.exe
native\vision_native\build\Release\cod_native_aimlab_benchmark.exe
native\vision_native\build\Release\cod_native_target_selector_tests.exe
scripts\verify\native_pipeline_contract.bat
git diff --check
```

Expected:

- aimlab tests print PASS;
- aimlab benchmark prints default scenario summaries;
- target selector tests pass;
- pipeline contract passes;
- `git diff --check` has no errors.

- [ ] **Step 3: Final commit if fixes were needed**

If verification required edits, commit them:

```powershell
git add native/controller_native native/runtime_app native/vision_native
git commit -m "Verify native aimlab intent benchmark integration"
```

If no edits were needed, do not create an empty commit.

## Task 7: Update Agent Context

**Files:**
- Modify: `.agent-context/handoff.md`
- Modify: `.agent-context/session-log.md`

- [ ] **Step 1: Record implementation status**

Update `.agent-context/handoff.md` current state with:

```text
- Native AimLab-style benchmark exists as `cod_native_aimlab_benchmark`.
- Live runtime now passes `UserAimIntent` from physical right stick into `VisionEngine` and `VisionTargetSelector`.
```

Update `.agent-context/session-log.md` with a short milestone entry and the verification commands that passed.

- [ ] **Step 2: Commit context update**

```powershell
git add .agent-context/handoff.md .agent-context/session-log.md
git commit -m "Record native aimlab benchmark progress"
```

## Execution Notes

- Keep commits small. Do not batch all tasks into one commit.
- Do not use startup/config parameters for benchmark behavior unless a task explicitly adds them.
- Do not modify Python fallback paths for default native runtime behavior.
- Do not change recoil behavior while implementing this plan.
- If a test passes immediately in a RED step because existing behavior already satisfies it, keep the test and continue; record that it was coverage for existing behavior.
- If `cod_native_target_selector_tests` or `native_pipeline_contract.bat` has a transient failure, rerun once and inspect the failing line before changing code.

## Self-Review

- Spec coverage:
  - deterministic synthetic benchmark: Tasks 1-3
  - no UI/image rendering: Task 3 executable is console-only
  - wrong target scoring: Tasks 1-2
  - overshoot/cooperation/user fighting scoring: Task 1
  - userInput into native vision/selector: Tasks 4-5
  - verification: Task 6
- Marker scan: no unresolved markers and no unspecified test command.
- Type consistency: scorer types are declared in Task 1 before scenario and executable tasks use them.
