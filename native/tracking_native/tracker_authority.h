#pragma once

#include "../common_native/authority_types.h"

#include <string_view>

namespace tracking_native {

enum class TargetTierClass {
    LostOrNone,
    StrongObserved,
    WeakContinuity,
    CueHold,
    Projected,
};

struct TargetAuthorityDecision {
    TargetTierClass tier_class = TargetTierClass::LostOrNone;
    bool is_strong_aim_target = false;
    bool is_weak_continuity = false;
    bool is_projected = false;
    bool is_terminal = true;
    common_native::AssistAuthority assist_authority = common_native::AssistAuthority::None;
    common_native::FireAuthority fire_authority = common_native::FireAuthority::None;
};

TargetTierClass classify_target_tier(std::string_view target_tier);
TargetAuthorityDecision classify_target_authority(
    bool has_target,
    bool aim_authority,
    bool fire_authority,
    std::string_view target_tier);

bool is_strong_observation(std::string_view target_tier);
bool is_weak_continuity_observation(std::string_view target_tier);
bool is_cue_hold_observation(std::string_view target_tier);
bool is_projected_observation(std::string_view target_tier);
bool is_terminal_observation(std::string_view target_tier);

}  // namespace tracking_native
