#pragma once

#include "pipeline_contract/target_plan.h"

#include <array>
#include <cstdint>

namespace controller_native {

enum class Axis : unsigned char {
    X = 0,
    Y = 1,
};

enum class AxisDecisionReason : unsigned char {
    Neutral,
    HelpfulResidual,
    ProbableWrongWay,
    CrossingLimit,
    EvidenceAmbiguous,
    ManualEscape,
};

struct AxisIntentInput {
    float error = 0.0f;
    float error_rate = 0.0f;
    float requested_assist = 0.0f;
    float manual = 0.0f;
    float manual_confidence = 0.0f;
    float reliability = 0.0f;
    float target_innovation_px = 0.0f;
    float normalized_size_change = 0.0f;
    float manual_escape_threshold = 0.45f;
    std::uint64_t target_id = 0;
    pipeline_contract::TargetLifecycle lifecycle = pipeline_contract::TargetLifecycle::None;
    pipeline_contract::ControlMode mode = pipeline_contract::ControlMode::Manual;
};

struct AxisDecision {
    float assist_output = 0.0f;
    float assist_scale = 1.0f;
    float wrong_way_budget = 1.0f;
    float stopping_output_budget = 1.0f;
    float divergence_risk = 0.0f;
    AxisDecisionReason reason = AxisDecisionReason::Neutral;
};

class AxisIntentArbiter {
public:
    AxisDecision update(Axis axis, const AxisIntentInput& input, float dt_seconds) noexcept;
    void reset() noexcept;

private:
    struct AxisState {
        float previous_error = 0.0f;
        float risk = 0.0f;
        std::uint64_t target_id = 0;
        bool initialized = false;
    };

    std::array<AxisState, 2> axes_{};
};

const char* to_string(AxisDecisionReason reason) noexcept;

}  // namespace controller_native
