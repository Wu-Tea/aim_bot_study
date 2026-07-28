#pragma once

#include "native_gamepad_controller.h"
#include "runtime_config.h"
#include "sustained_aimlab_counterfactual.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace controller_native::benchmark_adapter {

struct AssistedModeCoverage {
    bool saw_assisted_mode = false;
    std::vector<std::uint64_t> ads_target_ids;
};

ControllerVisionSnapshot snapshot_from(
    const sustained_aimlab::ControllerObservation& input,
    double now_seconds);

class NativeReplayAdapter {
public:
    NativeReplayAdapter(
        GamepadRuntimeConfig source_config,
        sustained_aimlab::BranchSchedule schedule,
        sustained_aimlab::BenchmarkCohort cohort,
        BenchmarkIntentFusionMode intent_fusion_mode,
        std::shared_ptr<AssistedModeCoverage> coverage = {},
        double assist_scale = 1.0,
        double tracker_velocity_alpha = -1.0);

    sustained_aimlab::ControllerStepResult step(
        const sustained_aimlab::ControllerObservation& input);

private:
    sustained_aimlab::BranchSchedule schedule_;
    sustained_aimlab::BenchmarkCohort cohort_ =
        sustained_aimlab::BenchmarkCohort::AdsAcquire;
    std::uint64_t last_ads_target_id_ = 0;
    int now_ms_ = 0;
    double now_seconds_ = 0.0;
    GamepadRuntimeConfig config_;
    NativeGamepadController controller_;
    PhysicalGamepadState physical_;
    std::shared_ptr<AssistedModeCoverage> coverage_;
    double assist_scale_ = 1.0;
};

sustained_aimlab::ReplayControllerFactory make_native_factory(
    GamepadRuntimeConfig config,
    sustained_aimlab::BenchmarkCohort cohort,
    BenchmarkIntentFusionMode intent_fusion_mode,
    std::shared_ptr<AssistedModeCoverage> coverage = {},
    double assist_scale = 1.0,
    double tracker_velocity_alpha = -1.0);

}  // namespace controller_native::benchmark_adapter
