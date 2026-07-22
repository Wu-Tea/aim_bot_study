#pragma once

#include "control_learning/causal_response_types.h"
#include "control_learning/control_history.h"

#include <cstdint>

namespace control_learning {

struct PendingMotionRequest {
    std::uint64_t previous_capture_ns = 0;
    std::uint64_t current_capture_ns = 0;
    std::uint64_t decision_ns = 0;
    float delay_ms = 0.0f;
    ResponseMatrix2d right_response{};
    ResponseMatrix2d left_response{};
    float selected_delay_confidence = 0.0f;
    float response_confidence = 0.0f;
    bool stable_coordinates_valid = true;
    bool identity_continuous = true;
    bool ads_epoch_continuous = true;
};

struct PendingMotionEstimate {
    Vec2d realized_px{};
    Vec2d scheduled_px{};
    Vec2d total_px{};
    float confidence = 0.0f;
    bool history_complete = false;
    bool valid = false;
};

class PendingMotionModel {
public:
    static PendingMotionEstimate estimate(
        const PendingMotionRequest& request,
        const ControlHistory<1024>& history) noexcept;
};

}  // namespace control_learning
