#include "blind_window_fixtures.h"

#include <algorithm>
#include <string>

namespace controller_native::blind_window {

BlindTimingProfile timing_100hz_phase_5() noexcept {
    BlindTimingProfile timing;
    timing.vision_period_us = 10'000;
    timing.event_phase_per_mille = 50;
    timing.result_latency_us = 16'000;
    timing.response_delay_us = 45'000;
    return timing;
}

BlindFixture bodylock_pending_crossing_fixture(
    std::uint32_t seed,
    BlindTimingProfile timing) {
    BlindFixture fixture;
    fixture.name = "bodylock_pending_crossing_track_6671_seed" +
        std::to_string(seed);
    fixture.knowledge_class = BlindKnowledgeClass::SelfPredictable;
    fixture.timing = timing;
    fixture.duration_us = 250'000;
    fixture.right_response_px_per_stick_second = {
        600.0, 0.0, 0.0, 520.0};
    fixture.warm_start_observation = true;
    fixture.preloaded_final_stick = {0.72, 0.0};
    const double event_seconds =
        static_cast<double>(timing.vision_period_us) *
        static_cast<double>(timing.event_phase_per_mille) /
        1'000'000'000.0;
    const double preloaded_reticle_speed =
        fixture.right_response_px_per_stick_second[0] *
        fixture.preloaded_final_stick.x;
    fixture.initial_error_px = {
        12.0 + preloaded_reticle_speed * event_seconds,
        0.0};
    return fixture;
}

BlindControllerStep stale_proportional_controller() {
    return [](const BlindControllerObservation& input) {
        BlindControllerOutput output;
        if (!input.target_present) return output;
        output.ai_stick = {
            std::clamp(input.observed_error_px.x * 0.045, -1.0, 1.0),
            std::clamp(input.observed_error_px.y * 0.045, -1.0, 1.0),
        };
        output.final_stick = output.ai_stick;
        return output;
    };
}

}  // namespace controller_native::blind_window
