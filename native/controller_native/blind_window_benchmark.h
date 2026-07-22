#pragma once

#include "sustained_aimlab_types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace controller_native::blind_window {

using sustained_aimlab::Vec2d;

inline constexpr std::uint32_t kBlindWindowFixtureSemanticsVersion = 1;

enum class BlindKnowledgeClass : std::uint8_t {
    SelfPredictable,
    InputObservable,
    ExternallyUnobservable,
    ObservationInvalid,
};

struct BlindTimingProfile {
    int vision_period_us = 10'000;
    int event_phase_per_mille = 50;
    int result_latency_us = 16'000;
    int response_delay_us = 45'000;
    std::uint32_t jitter_seed = 0;
};

struct BlindTraceFrame {
    int now_us = 0;
    Vec2d true_error_px{};
    Vec2d manual_stick{};
    Vec2d ai_stick{};
    Vec2d final_stick{};
    Vec2d response_applied_reticle_px_per_sec{};
    Vec2d scheduled_pending_reticle_motion_px{};
    std::int64_t max_controller_source_time_us = 0;
};

struct BlindWindowTrace {
    std::vector<BlindTraceFrame> frames;
};

struct BlindSchedule {
    int capture_at_us = 0;
    int event_at_us = 0;
    int next_capture_at_us = 0;
    int next_result_at_us = 0;
};

struct BlindFixture {
    std::string name;
    BlindKnowledgeClass knowledge_class = BlindKnowledgeClass::SelfPredictable;
    BlindTimingProfile timing{};
    int duration_us = 250'000;
    Vec2d initial_error_px{};
    Vec2d target_velocity_px_per_second{};
    Vec2d target_acceleration_px_per_sec2{};
    std::array<double, 4> right_response_px_per_stick_second{
        500.0, 0.0, 0.0, 500.0};
    bool warm_start_observation = false;
    Vec2d preloaded_final_stick{};
};

struct BlindControllerObservation {
    int now_us = 0;
    bool target_present = false;
    bool fresh_vision = false;
    std::int64_t captured_at_us = -1;
    std::int64_t result_at_us = -1;
    Vec2d observed_error_px{};
    Vec2d manual_stick{};
    Vec2d left_stick{};
};

struct BlindControllerOutput {
    Vec2d ai_stick{};
    Vec2d final_stick{};
};

using BlindControllerStep = std::function<BlindControllerOutput(
    const BlindControllerObservation&)>;

struct BlindWindowMetrics {
    double blind_duration_ms = 0.0;
    double stale_ai_impulse_stick_ms = 0.0;
    double harmful_ai_motion_px = 0.0;
    double harmful_pending_at_reveal_px = 0.0;
    double future_burden_40_px_ms = 0.0;
    double future_burden_80_px_ms = 0.0;
    double future_burden_160_px_ms = 0.0;
    double reverse_correction_80_stick_ms = 0.0;
    int reveal_to_reacquire_ms = -1;
    double post_cross_area_px_ms = 0.0;
    double user_fight_stick_ms = 0.0;
    double far_error_closing_speed_px_per_sec = 0.0;
    double output_total_variation = 0.0;
    double p95_output_delta = 0.0;
    int incorrect_interruption_count = 0;
    int identity_authority_violations = 0;
    int future_dependency_violations = 0;
};

struct BlindWindowRun {
    BlindSchedule schedule{};
    BlindWindowTrace trace;
    BlindWindowMetrics metrics{};

    const BlindTraceFrame& frame_at_us(int timestamp_us) const;
};

bool finite(const BlindWindowMetrics& metrics) noexcept;

BlindSchedule build_blind_schedule(
    const BlindTimingProfile& timing,
    int capture_at_us,
    int duration_us) noexcept;

BlindWindowRun run_blind_fixture(
    const BlindFixture& fixture,
    const BlindControllerStep& controller_step);

}  // namespace controller_native::blind_window
