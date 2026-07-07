#include "tracker_authority.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace tracking_native {

namespace {

std::string normalized_tier(std::string_view target_tier) {
    std::string value(target_tier);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

TargetTierClass classify_target_tier(std::string_view target_tier) {
    const std::string tier = normalized_tier(target_tier);
    if (tier.empty() || tier == "none" || tier == "lost") {
        return TargetTierClass::LostOrNone;
    }
    if (tier == "projected" || tier == "projection" || tier == "predicted") {
        return TargetTierClass::Projected;
    }
    if (tier == "cue_hold") {
        return TargetTierClass::CueHold;
    }
    if (tier == "associated_weak" ||
        tier == "weak" ||
        tier == "weak_association" ||
        tier == "weak_observed") {
        return TargetTierClass::WeakContinuity;
    }
    return TargetTierClass::StrongObserved;
}

TargetAuthorityDecision classify_target_authority(
    bool has_target,
    bool aim_authority,
    bool fire_authority,
    std::string_view target_tier) {
    TargetAuthorityDecision decision;
    decision.tier_class = classify_target_tier(target_tier);
    decision.is_weak_continuity =
        decision.tier_class == TargetTierClass::WeakContinuity ||
        decision.tier_class == TargetTierClass::CueHold;
    decision.is_projected = decision.tier_class == TargetTierClass::Projected;
    decision.is_terminal = decision.tier_class == TargetTierClass::LostOrNone;

    if (!has_target || decision.is_terminal) {
        return decision;
    }

    decision.is_strong_aim_target =
        aim_authority && decision.tier_class == TargetTierClass::StrongObserved;
    if (aim_authority) {
        decision.assist_authority = decision.is_strong_aim_target
            ? common_native::AssistAuthority::AimObserved
            : common_native::AssistAuthority::AimCoast;
        if (decision.is_strong_aim_target) {
            decision.target_authority_state =
                common_native::TargetAuthorityState::StrongAssist;
        } else if (decision.is_weak_continuity) {
            decision.target_authority_state =
                common_native::TargetAuthorityState::WeakAssist;
        } else {
            decision.target_authority_state =
                common_native::TargetAuthorityState::TrackOnly;
        }
    } else {
        decision.target_authority_state =
            common_native::TargetAuthorityState::TrackOnly;
    }
    if (fire_authority && decision.is_strong_aim_target) {
        decision.fire_authority = common_native::FireAuthority::ObservedOnly;
    }
    return decision;
}

bool is_strong_observation(std::string_view target_tier) {
    return classify_target_tier(target_tier) == TargetTierClass::StrongObserved;
}

bool is_weak_continuity_observation(std::string_view target_tier) {
    const TargetTierClass tier_class = classify_target_tier(target_tier);
    return tier_class == TargetTierClass::WeakContinuity ||
        tier_class == TargetTierClass::CueHold;
}

bool is_cue_hold_observation(std::string_view target_tier) {
    return classify_target_tier(target_tier) == TargetTierClass::CueHold;
}

bool is_projected_observation(std::string_view target_tier) {
    return classify_target_tier(target_tier) == TargetTierClass::Projected;
}

bool is_terminal_observation(std::string_view target_tier) {
    return classify_target_tier(target_tier) == TargetTierClass::LostOrNone;
}

}  // namespace tracking_native
