#pragma once

#include "control_learning/causal_response_types.h"
#include "pipeline_contract/target_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace control_learning {

struct RolloutSnapshot {
    std::uint64_t decision_at_ns = 0;
    std::uint64_t latest_evidence_at_ns = 0;
    std::uint64_t target_id = 0;
    pipeline_contract::ControlMode mode = pipeline_contract::ControlMode::Manual;
    Vec2d error_px{};
    Vec2d predicted_terminal_error_px{};
    Vec2d target_velocity_px_per_sec{};
    Vec2d target_acceleration_px_per_sec2{};
    Vec2d shaped_ai{};
    Vec2d manual{};
    Vec2d scheduled_pending_px{};
    ResponseMatrix2d right_response{};
    float response_confidence = 0.0f;
    float delay_confidence = 0.0f;
    bool has_target = false;
    bool single_strong_target = false;

    bool operator==(const RolloutSnapshot& other) const noexcept;
};

struct RolloutCandidate {
    float scale = 1.0f;
    double cost = 0.0;
};

struct RolloutResult {
    std::array<RolloutCandidate, 5> candidates{};
    std::size_t candidate_count = 0;
    float best_scale = 1.0f;
    float confidence = 0.0f;
    std::uint64_t used_latest_timestamp_ns = 0;
    bool manual_escape = false;
    bool valid = false;

    bool operator==(const RolloutResult& other) const noexcept;
};

class ShortHorizonRollout {
public:
    static RolloutResult evaluate(const RolloutSnapshot& snapshot) noexcept;
};

}  // namespace control_learning
