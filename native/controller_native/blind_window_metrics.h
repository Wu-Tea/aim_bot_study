#pragma once

#include "blind_window_benchmark.h"

namespace controller_native::blind_window {

double harmful_pending_at_reveal(
    Vec2d error_px,
    Vec2d pending_reticle_motion_px) noexcept;

double future_error_burden(
    const BlindWindowTrace& trace,
    int start_ms,
    int horizon_ms) noexcept;

double user_fight_area(
    const BlindWindowTrace& trace,
    double drift_floor) noexcept;

BlindWindowMetrics evaluate_blind_window(
    const BlindFixture& fixture,
    const BlindWindowTrace& trace,
    const BlindSchedule& schedule) noexcept;

}  // namespace controller_native::blind_window
