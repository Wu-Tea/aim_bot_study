#pragma once

#include "pipeline_contract/user_aim_intent.h"
#include "pipeline_contract/vision_observation.h"

namespace pipeline_contract {

enum class StickPhase : unsigned char {
    Neutral,
    Onset,
    Sustained,
    Reversal,
    Release,
};

enum class IntentRelationship : unsigned char {
    Ambiguous,
    Aligned,
    Opposing,
};

struct AxisIntentState {
    float raw = 0.0f;
    float filtered = 0.0f;
    float neutral_bias = 0.0f;
    float noise_envelope = 0.0f;
    float confidence = 0.0f;
    // Continuous evidence above this axis's learned neutral/noise threshold.
    // Zero within the noise envelope, one at twice its threshold.
    float activity = 0.0f;
};

struct IntentState {
    Vec2f raw_left{};
    Vec2f raw_right{};
    Vec2f filtered_left{};
    Vec2f filtered_right{};
    AxisIntentState left_x{};
    AxisIntentState left_y{};
    AxisIntentState right_x{};
    AxisIntentState right_y{};
    StickPhase left_phase = StickPhase::Neutral;
    StickPhase right_phase = StickPhase::Neutral;
    UserAimIntentPurpose right_purpose =
        UserAimIntentPurpose::AcquireTarget;
    IntentRelationship relationship = IntentRelationship::Ambiguous;
    float left_confidence = 0.0f;
    float right_confidence = 0.0f;
    double sample_time_seconds = 0.0;
    bool ads = false;
    bool fire = false;
};

}  // namespace pipeline_contract
