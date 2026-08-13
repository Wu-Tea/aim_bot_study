#pragma once

#include "native_gamepad_controller.h"
#include "runtime_config.h"
#include "sustained_aimlab_simulator.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace controller_native::benchmark_adapter {

struct AssistedModeCoverage {
    bool saw_assisted_mode = false;
    std::vector<std::uint64_t> ads_target_ids;
    // Operation-model diagnostics (§4.5): how often the synthetic user fired,
    // how often the classifier labelled the frame recoil_pull, how often the
    // recoil compensator actually pushed down, and the total frame count.
    std::uint64_t firing_frames = 0;
    std::uint64_t recoil_pull_frames = 0;
    std::uint64_t recoil_active_frames = 0;
    std::uint64_t unreliable_frames = 0;
    std::uint64_t total_frames = 0;
};

ControllerVisionSnapshot snapshot_from(
    const sustained_aimlab::ControllerObservation& input,
    double now_seconds);

class NativeReplayAdapter {
public:
    NativeReplayAdapter(
        GamepadRuntimeConfig source_config,
        sustained_aimlab::BenchmarkCohort cohort,
        std::shared_ptr<AssistedModeCoverage> coverage = {});

    sustained_aimlab::ControllerStepResult step(
        const sustained_aimlab::ControllerObservation& input);

private:
    sustained_aimlab::BenchmarkCohort cohort_ =
        sustained_aimlab::BenchmarkCohort::AdsAcquire;
    std::uint64_t last_ads_target_id_ = 0;
    double now_seconds_ = 0.0;
    GamepadRuntimeConfig config_;
    NativeGamepadController controller_;
    PhysicalGamepadState physical_;
    std::shared_ptr<AssistedModeCoverage> coverage_;
};

sustained_aimlab::ControllerStep make_native_controller(
    GamepadRuntimeConfig config,
    sustained_aimlab::BenchmarkCohort cohort,
    std::shared_ptr<AssistedModeCoverage> coverage = {},
    bool recoil_enabled = false);

}  // namespace controller_native::benchmark_adapter
