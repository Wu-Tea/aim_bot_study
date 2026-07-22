#include "blind_window_native_adapter.h"

#include "native_benchmark_controller_adapter.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace controller_native::blind_window {

BlindControllerFactory native_bodylock_controller_factory(
    GamepadRuntimeConfig config,
    double assist_scale) {
    config.recoil.enabled = false;
    return [config = std::move(config), assist_scale] {
        sustained_aimlab::BranchSchedule schedule;
        auto adapter = std::make_shared<
            benchmark_adapter::NativeReplayAdapter>(
                config,
                schedule,
                sustained_aimlab::BenchmarkCohort::BodyLockFollow,
                BenchmarkIntentFusionMode::CausalVector,
                std::shared_ptr<benchmark_adapter::AssistedModeCoverage>{},
                assist_scale);
        return [adapter](const BlindControllerObservation& observation) {
            sustained_aimlab::ControllerObservation input;
            input.now_ms = observation.now_us / 1'000;
            input.target_present = observation.target_present;
            input.fresh_vision = observation.fresh_vision;
            input.frame_id = observation.captured_at_us < 0
                ? 1u
                : static_cast<std::uint64_t>(
                    observation.captured_at_us / 1'000 + 2);
            input.target_id = observation.target_present ? 6671u : 0u;
            input.capture_time_seconds =
                static_cast<double>(observation.captured_at_us) / 1'000'000.0;
            input.ready_time_seconds =
                static_cast<double>(observation.result_at_us) / 1'000'000.0;
            input.observed_error_px = observation.observed_error_px;
            input.manual_stick = observation.manual_stick;
            const auto output = adapter->step(input);
            BlindControllerOutput result;
            result.ai_stick = output.shaped_assist_stick;
            result.final_stick = output.final_stick;
            return result;
        };
    };
}

}  // namespace controller_native::blind_window
