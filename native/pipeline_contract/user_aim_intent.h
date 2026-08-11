#pragma once

#include "../common_native/screen_geometry.h"
#include "../common_native/time_types.h"

#include <cstdint>

namespace pipeline_contract {

enum class UserAimIntentPurpose : unsigned char {
    AcquireTarget,
    CorrectCurrentTarget,
    HandoverTarget,
};

struct UserAimIntent {
    std::uint64_t intent_id = 0;
    common_native::TimeSeconds timestamp;
    bool valid = false;
    float strength = 0.0f;
    bool has_point = false;
    common_native::Vec2f point_px;
    bool has_direction = false;
    common_native::Vec2f direction;
    bool aiming = false;
    // One continuous gesture keeps one identity meaning from onset through
    // release/reversal. Every consumer receives this same purpose.
    UserAimIntentPurpose purpose = UserAimIntentPurpose::AcquireTarget;
};

}  // namespace pipeline_contract
