#pragma once

#include "sustained_aimlab_types.h"

#include <cstdint>
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
    std::int64_t max_controller_source_time_us = 0;
};

struct BlindWindowTrace {
    std::vector<BlindTraceFrame> frames;
};

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

bool finite(const BlindWindowMetrics& metrics) noexcept;

}  // namespace controller_native::blind_window
