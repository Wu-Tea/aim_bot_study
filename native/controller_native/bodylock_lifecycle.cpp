#include "bodylock_lifecycle.h"

namespace controller_native {

void BodylockLifecycle::reset() {
    state_ = pipeline_contract::BodylockLifecycleState::Inactive;
    track_id_ = 0;
    coast_started_at_seconds_ = 0.0;
    geometry_gap_started_at_seconds_ = 0.0;
    coast_timer_active_ = false;
    geometry_gap_timer_active_ = false;
}

BodylockLifecycleDecision BodylockLifecycle::update(const BodylockLifecycleInput& input) {
    using pipeline_contract::AssistAuthorityReason;
    using pipeline_contract::AssistAuthorityState;
    using pipeline_contract::BodylockLifecycleState;

    const bool was_active =
        state_ == BodylockLifecycleState::Warm ||
        state_ == BodylockLifecycleState::Tracking ||
        state_ == BodylockLifecycleState::Coast;
    const auto clear_gap_timers = [&]() {
        coast_started_at_seconds_ = 0.0;
        geometry_gap_started_at_seconds_ = 0.0;
        coast_timer_active_ = false;
        geometry_gap_timer_active_ = false;
    };

    if (!input.aiming) {
        clear_gap_timers();
        return transition(
            was_active ? BodylockLifecycleState::Yield : BodylockLifecycleState::Inactive,
            BodylockTransitionReason::AimReleased,
            0,
            false);
    }

    if (state_ == BodylockLifecycleState::Yield) {
        if (input.bodylock_available &&
            input.authority == AssistAuthorityState::ObservedStrong &&
            input.selected_track_id != 0) {
            clear_gap_timers();
            return transition(
                BodylockLifecycleState::Warm,
                BodylockTransitionReason::Observed,
                input.selected_track_id,
                false);
        }
        clear_gap_timers();
        return transition(
            BodylockLifecycleState::Inactive,
            BodylockTransitionReason::AuthorityLost,
            0,
            false);
    }

    if (was_active && input.selected_track_id != 0 && track_id_ != 0 &&
        input.selected_track_id != track_id_) {
        clear_gap_timers();
        return transition(
            BodylockLifecycleState::Yield,
            BodylockTransitionReason::TargetSwitched,
            input.selected_track_id,
            false);
    }

    if (input.bodylock_available &&
        input.authority == AssistAuthorityState::ObservedStrong &&
        input.selected_track_id != 0) {
        clear_gap_timers();
        if (state_ == BodylockLifecycleState::Inactive) {
            return transition(
                BodylockLifecycleState::Warm,
                BodylockTransitionReason::Observed,
                input.selected_track_id,
                false);
        }
        return transition(
            BodylockLifecycleState::Tracking,
            BodylockTransitionReason::Observed,
            input.selected_track_id,
            false);
    }

    const bool same_track_continuity =
        input.authority == AssistAuthorityState::Continuity ||
        (input.authority == AssistAuthorityState::TrackOnly &&
         input.authority_reason == AssistAuthorityReason::ShortEvidenceGap);
    if (same_track_continuity &&
        input.selected_track_id != 0 && input.selected_track_id == track_id_ &&
        was_active) {
        constexpr double kMaxCoastSeconds = 0.096;
        if (!coast_timer_active_) {
            coast_started_at_seconds_ = input.now_seconds;
            coast_timer_active_ = true;
        }
        geometry_gap_timer_active_ = false;
        if (input.now_seconds - coast_started_at_seconds_ > kMaxCoastSeconds) {
            clear_gap_timers();
            return transition(
                BodylockLifecycleState::Yield,
                BodylockTransitionReason::AuthorityLost,
                0,
                false);
        }
        return transition(
            BodylockLifecycleState::Coast,
            BodylockTransitionReason::Continuity,
            track_id_,
            false);
    }

    if (!input.bodylock_available && was_active &&
        input.authority == AssistAuthorityState::ObservedStrong &&
        input.selected_track_id != 0 && input.selected_track_id == track_id_) {
        constexpr double kGeometryGraceSeconds = 0.040;
        if (!geometry_gap_timer_active_) {
            geometry_gap_started_at_seconds_ = input.now_seconds;
            geometry_gap_timer_active_ = true;
        }
        coast_timer_active_ = false;
        if (input.now_seconds - geometry_gap_started_at_seconds_ <=
            kGeometryGraceSeconds) {
            return transition(
                BodylockLifecycleState::Coast,
                BodylockTransitionReason::BodylockUnavailable,
                track_id_,
                false);
        }
    }

    if (!input.bodylock_available) {
        clear_gap_timers();
        return transition(
            was_active ? BodylockLifecycleState::Yield : BodylockLifecycleState::Inactive,
            BodylockTransitionReason::BodylockUnavailable,
            0,
            false);
    }

    BodylockTransitionReason reason = BodylockTransitionReason::AuthorityLost;
    if (input.authority_reason == AssistAuthorityReason::TargetSwitched) {
        reason = BodylockTransitionReason::TargetSwitched;
    } else if (input.authority_reason == AssistAuthorityReason::UserYield) {
        reason = BodylockTransitionReason::UserYield;
    }
    clear_gap_timers();
    return transition(
        was_active ? BodylockLifecycleState::Yield : BodylockLifecycleState::Inactive,
        reason,
        0,
        false);
}

pipeline_contract::BodylockLifecycleState BodylockLifecycle::state() const {
    return state_;
}

std::uint64_t BodylockLifecycle::track_id() const {
    return track_id_;
}

BodylockLifecycleDecision BodylockLifecycle::transition(
    pipeline_contract::BodylockLifecycleState next,
    BodylockTransitionReason reason,
    std::uint64_t track_id,
    bool reset_history) {
    state_ = next;
    track_id_ = track_id;
    return {state_, reason, track_id_, reset_history};
}

}  // namespace controller_native
