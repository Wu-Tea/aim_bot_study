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
    ConfirmedWrongWay,
    EvidenceAmbiguous,
    ManualEscape,
};

struct AxisIntentInput {
    float error = 0.0f;
    float error_rate = 0.0f;
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
    float manual_yield_confidence = 0.0f;
    bool intervention = false;
    bool wrong_way = false;
    bool evidence_stable = false;
    bool error_worsening = false;
    AxisDecisionReason reason = AxisDecisionReason::Neutral;
};

class AxisIntentArbiter {
public:
    AxisDecision update(Axis axis, const AxisIntentInput& input, float dt_seconds) noexcept;
    void reset() noexcept;

private:
    struct AxisState {
        float previous_error = 0.0f;
        float geometry_cooldown_seconds = 0.0f;
        float intervention_hold_seconds = 0.0f;
        std::uint64_t target_id = 0;
        bool initialized = false;
    };

    std::array<AxisState, 2> axes_{};
};

const char* to_string(AxisDecisionReason reason) noexcept;

}  // namespace controller_native
