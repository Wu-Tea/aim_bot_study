#pragma once

#include "../pipeline_contract/assist_authority.h"

#include <cstdint>

namespace controller_native {

enum class BodylockTransitionReason : std::uint8_t {
    None = 0,
    Observed = 1,
    Continuity = 2,
    AuthorityLost = 3,
    TargetSwitched = 4,
    UserYield = 5,
    AimReleased = 6,
    BodylockUnavailable = 7,
};

[[nodiscard]] inline const char* bodylock_transition_reason_name(
    BodylockTransitionReason reason) noexcept {
    switch (reason) {
    case BodylockTransitionReason::None: return "none";
    case BodylockTransitionReason::Observed: return "observed";
    case BodylockTransitionReason::Continuity: return "continuity";
    case BodylockTransitionReason::AuthorityLost: return "authority_lost";
    case BodylockTransitionReason::TargetSwitched: return "target_switched";
    case BodylockTransitionReason::UserYield: return "user_yield";
    case BodylockTransitionReason::AimReleased: return "aim_released";
    case BodylockTransitionReason::BodylockUnavailable: return "bodylock_unavailable";
    }
    return "unknown";
}

struct BodylockLifecycleInput {
    bool aiming = false;
    bool bodylock_available = false;
    std::uint64_t selected_track_id = 0;
    pipeline_contract::AssistAuthorityState authority =
        pipeline_contract::AssistAuthorityState::Reject;
    pipeline_contract::AssistAuthorityReason authority_reason =
        pipeline_contract::AssistAuthorityReason::None;
    float manual_x = 0.0f;
    float manual_y = 0.0f;
    double now_seconds = 0.0;
};

struct BodylockLifecycleDecision {
    pipeline_contract::BodylockLifecycleState state =
        pipeline_contract::BodylockLifecycleState::Inactive;
    BodylockTransitionReason reason = BodylockTransitionReason::None;
    std::uint64_t track_id = 0;
    bool reset_assist_history = false;
};

class BodylockLifecycle {
public:
    void reset();
    [[nodiscard]] BodylockLifecycleDecision update(const BodylockLifecycleInput& input);
    [[nodiscard]] pipeline_contract::BodylockLifecycleState state() const;
    [[nodiscard]] std::uint64_t track_id() const;

private:
    BodylockLifecycleDecision transition(
        pipeline_contract::BodylockLifecycleState next,
        BodylockTransitionReason reason,
        std::uint64_t track_id,
        bool reset_history);

    pipeline_contract::BodylockLifecycleState state_ =
        pipeline_contract::BodylockLifecycleState::Inactive;
    std::uint64_t track_id_ = 0;
    double coast_started_at_seconds_ = 0.0;
    double geometry_gap_started_at_seconds_ = 0.0;
    bool coast_timer_active_ = false;
    bool geometry_gap_timer_active_ = false;
};

}  // namespace controller_native
