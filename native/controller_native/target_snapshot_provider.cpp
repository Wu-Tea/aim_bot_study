#include "target_snapshot_provider.h"

#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <cmath>

namespace controller_native {

namespace {

NativeControllerVisionState cleared_target_state(NativeControllerVisionState state) {
    state.has_target = false;
    state.auto_fire_requested = false;
    state.aim_authority = false;
    state.fire_authority = false;
    state.dx = 0.0f;
    state.dy = 0.0f;
    state.has_body_box = false;
    state.body_x1 = 0.0f;
    state.body_y1 = 0.0f;
    state.body_x2 = 0.0f;
    state.body_y2 = 0.0f;
    state.has_tracker_projection = false;
    state.tracker_dx = 0.0f;
    state.tracker_dy = 0.0f;
    state.target_tier = "none";
    return state;
}

bool state_age_exceeds_ms(
    const NativeControllerVisionState& state,
    double now_seconds,
    float max_age_ms) {
    if (!state.has_target || state.observed_at_seconds <= 0.0 || now_seconds <= 0.0) {
        return false;
    }
    if (max_age_ms <= 0.0f) {
        return false;
    }
    const double age_ms = std::max(0.0, now_seconds - state.observed_at_seconds) * 1000.0;
    return age_ms > static_cast<double>(max_age_ms);
}

pipeline_contract::TargetTrackerConfig target_tracker_config_from_ai_aim(
    const GamepadAiAimConfig& config) {
    pipeline_contract::TargetTrackerConfig tracker_config;
    tracker_config.reticle_speed_px_per_sec = config.target_projection_reticle_speed_px_per_sec;
    tracker_config.max_projection_age_ms = config.target_projection_max_age_ms;
    tracker_config.velocity_lowpass_alpha = config.target_projection_velocity_lowpass_alpha;
    tracker_config.max_target_velocity_px_per_sec = config.target_projection_max_velocity_px_per_sec;
    tracker_config.weak_observation_velocity_decay = config.target_projection_weak_velocity_decay;
    return tracker_config;
}

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

bool candidate_can_assist(const pipeline_contract::VisionCandidateSnapshot& candidate) {
    return candidate.valid &&
        candidate.has_aim_point &&
        !candidate.is_friendly &&
        candidate.suggested_authority_state != common_native::TargetAuthorityState::Reject;
}

std::pair<float, float> screen_center_for_snapshot(
    const ControllerVisionSnapshot& snapshot) {
    return {
        snapshot.state.screen_center_x > 0.0f ? snapshot.state.screen_center_x : 320.0f,
        snapshot.state.screen_center_y > 0.0f ? snapshot.state.screen_center_y : 256.0f,
    };
}

float candidate_intent_alignment(
    const pipeline_contract::VisionCandidateSnapshot& candidate,
    const pipeline_contract::UserAimIntent& intent,
    float screen_center_x,
    float screen_center_y) {
    if (!intent.valid || !intent.aiming || !intent.has_direction) {
        return 0.0f;
    }
    const float to_target_x = candidate.aim_point_px.x - screen_center_x;
    const float to_target_y = candidate.aim_point_px.y - screen_center_y;
    const float target_length = std::hypot(to_target_x, to_target_y);
    const float intent_length = std::hypot(intent.direction.x, intent.direction.y);
    if (target_length <= 0.001f || intent_length <= 0.001f) {
        return 0.0f;
    }
    const float alignment =
        ((to_target_x / target_length) * (intent.direction.x / intent_length)) +
        ((to_target_y / target_length) * (intent.direction.y / intent_length));
    return clamp01(alignment);
}

float middle_layer_candidate_score(
    const pipeline_contract::VisionCandidateSnapshot& candidate,
    const pipeline_contract::UserAimIntent& intent,
    float screen_center_x,
    float screen_center_y) {
    if (!candidate_can_assist(candidate)) {
        return -1.0e9f;
    }
    const float confidence_score = clamp01(candidate.confidence) * 400.0f;
    const float cue_score =
        candidate.has_cue_point ? (100.0f + clamp01(candidate.cue_score) * 100.0f) : 0.0f;
    const float authority_score =
        candidate.suggested_authority_state == common_native::TargetAuthorityState::StrongAssist
            ? 80.0f
            : 20.0f;
    const float distance_px = std::hypot(
        candidate.aim_point_px.x - screen_center_x,
        candidate.aim_point_px.y - screen_center_y);
    const float distance_penalty = std::min(160.0f, distance_px * 0.20f);
    const float intent_score =
        candidate_intent_alignment(candidate, intent, screen_center_x, screen_center_y) *
        clamp01(intent.strength) *
        700.0f;
    return confidence_score + cue_score + authority_score + intent_score - distance_penalty;
}

bool candidate_matches_state(
    const pipeline_contract::VisionCandidateSnapshot& candidate,
    const NativeControllerVisionState& state) {
    if (!state.has_target || !candidate.has_aim_point) {
        return false;
    }
    return std::hypot(
        candidate.aim_point_px.x - state.target_x,
        candidate.aim_point_px.y - state.target_y) <= 4.0f;
}

std::string target_tier_for_candidate(
    const pipeline_contract::VisionCandidateSnapshot& candidate) {
    if (candidate.suggested_authority_state ==
        common_native::TargetAuthorityState::StrongAssist) {
        return "observed_strong";
    }
    if (candidate.suggested_authority_state ==
        common_native::TargetAuthorityState::WeakAssist) {
        return "associated_weak";
    }
    return "projected";
}

NativeControllerVisionState state_from_candidate(
    const ControllerVisionSnapshot& snapshot,
    const pipeline_contract::VisionCandidateSnapshot& candidate) {
    NativeControllerVisionState state = snapshot.state;
    const auto center = screen_center_for_snapshot(snapshot);
    state.has_target = true;
    state.auto_fire_requested = false;
    state.aim_authority =
        candidate.suggested_authority_state == common_native::TargetAuthorityState::StrongAssist ||
        candidate.suggested_authority_state == common_native::TargetAuthorityState::WeakAssist;
    state.fire_authority = false;
    state.target_tier = target_tier_for_candidate(candidate);
    state.screen_center_x = center.first;
    state.screen_center_y = center.second;
    state.target_x = candidate.aim_point_px.x;
    state.target_y = candidate.aim_point_px.y;
    state.dx = candidate.aim_point_px.x - center.first;
    state.dy = candidate.aim_point_px.y - center.second;
    state.has_body_box = true;
    state.body_x1 = candidate.body_box_px.x;
    state.body_y1 = candidate.body_box_px.y;
    state.body_x2 = candidate.body_box_px.x + candidate.body_box_px.w;
    state.body_y2 = candidate.body_box_px.y + candidate.body_box_px.h;
    state.observed_at_seconds = snapshot.ready_time_seconds > 0.0
        ? snapshot.ready_time_seconds
        : snapshot.capture_time_seconds;
    state.has_tracker_projection = false;
    state.tracker_dx = 0.0f;
    state.tracker_dy = 0.0f;
    return state;
}

}  // namespace

TargetSnapshotProvider::TargetSnapshotProvider(
    GamepadAiAimConfig ai_config,
    tracking_native::TrackerBackendKind tracker_backend)
    : ai_config_(ai_config),
      target_tracker_(tracking_native::create_tracker_backend(
          tracker_backend,
          target_tracker_config_from_ai_aim(ai_config_))) {}

void TargetSnapshotProvider::reset() {
    latest_vision_state_ = NativeControllerVisionState{};
    target_tracker_->reset();
    latest_vision_sequence_ = 0;
    raw_vision_sequence_consumed_ = 0;
    last_output_at_seconds_ = 0.0;
    has_committed_target_ = false;
    committed_target_dx_ = 0.0f;
    committed_target_dy_ = 0.0f;
    has_candidate_target_ = false;
    candidate_target_dx_ = 0.0f;
    candidate_target_dy_ = 0.0f;
    candidate_first_observed_at_seconds_ = 0.0;
    candidate_last_observed_at_seconds_ = 0.0;
    candidate_fresh_samples_ = 0;
    candidate_projection_hold_until_seconds_ = 0.0;
    candidate_reacquire_snap_until_seconds_ = 0.0;
    candidate_output_hold_until_seconds_ = 0.0;
}

void TargetSnapshotProvider::submit_vision_state(
    const NativeControllerVisionState& state,
    double now_seconds,
    bool ads_active) {
    const double capture_time = state.observed_at_seconds > 0.0
        ? state.observed_at_seconds
        : now_seconds;
    const double ready_time = now_seconds;
    bool suppress_tracker_ingest = false;
    latest_vision_state_ =
        credibility_gated_vision_state(state, ready_time, ads_active, &suppress_tracker_ingest);
    ++latest_vision_sequence_;
    if (!suppress_tracker_ingest) {
        ingest_tracker_observation(state, {}, 0, capture_time, ready_time, now_seconds);
    }
}

void TargetSnapshotProvider::submit_vision_snapshot(
    const ControllerVisionSnapshot& snapshot,
    double now_seconds,
    bool ads_active) {
    if (!snapshot.frame_updated) {
        return;
    }

    const double query_time = snapshot.ready_time_seconds > 0.0
        ? snapshot.ready_time_seconds
        : (snapshot.capture_time_seconds > 0.0 ? snapshot.capture_time_seconds : now_seconds);
    const NativeControllerVisionState selected_state =
        select_middle_layer_target(snapshot);
    bool suppress_tracker_ingest = false;
    latest_vision_state_ = credibility_gated_vision_state(
        selected_state,
        query_time,
        ads_active,
        &suppress_tracker_ingest);
    ++latest_vision_sequence_;
    if (!suppress_tracker_ingest) {
        ingest_tracker_observation(
            selected_state,
            snapshot.tracker_detections,
            snapshot.frame_id,
            snapshot.capture_time_seconds,
            snapshot.ready_time_seconds,
            now_seconds);
    }
}

void TargetSnapshotProvider::ingest_tracker_observation(
    const NativeControllerVisionState& state,
    const std::vector<tracking_native::TrackerDetection>& detections,
    std::uint64_t frame_id,
    double capture_time_seconds,
    double ready_time_seconds,
    double fallback_now_seconds) {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            state.has_target,
            state.aim_authority,
            state.fire_authority,
            state.target_tier);
    tracking_native::TrackerObservation observation;
    observation.has_target =
        authority.assist_authority != common_native::AssistAuthority::None;
    observation.aim_error_px = {state.dx, state.dy};
    observation.has_body_box = state.has_body_box;
    observation.body_box_px = {
        state.body_x1,
        state.body_y1,
        std::max(0.0f, state.body_x2 - state.body_x1),
        std::max(0.0f, state.body_y2 - state.body_y1)};
    observation.target_tier = state.target_tier;
    observation.capture_time = {
        capture_time_seconds > 0.0 ? capture_time_seconds : fallback_now_seconds};
    observation.ready_time = {
        ready_time_seconds > 0.0 ? ready_time_seconds : observation.capture_time.value};
    observation.screen_center_px = {state.screen_center_x, state.screen_center_y};
    observation.frame_id = frame_id;
    observation.detections = detections;
    target_tracker_->ingest(observation);
}

NativeControllerVisionState TargetSnapshotProvider::select_middle_layer_target(
    const ControllerVisionSnapshot& snapshot) const {
    const pipeline_contract::UserAimIntent& intent = snapshot.user_intent;
    if (!intent.valid || !intent.aiming || !intent.has_direction || intent.strength < 0.05f ||
        snapshot.candidates.size() < 2) {
        return snapshot.state;
    }

    const auto center = screen_center_for_snapshot(snapshot);
    int best_index = -1;
    float best_score = -1.0e9f;
    int selected_index = -1;
    float selected_score = -1.0e9f;

    for (std::size_t index = 0; index < snapshot.candidates.size(); ++index) {
        const pipeline_contract::VisionCandidateSnapshot& candidate =
            snapshot.candidates[index];
        const float score = middle_layer_candidate_score(
            candidate,
            intent,
            center.first,
            center.second);
        if (score > best_score) {
            best_score = score;
            best_index = static_cast<int>(index);
        }
        if (candidate_matches_state(candidate, snapshot.state)) {
            selected_index = static_cast<int>(index);
            selected_score = score;
        }
    }

    if (best_index < 0) {
        return snapshot.state;
    }
    if (best_index == selected_index) {
        return snapshot.state;
    }

    const pipeline_contract::VisionCandidateSnapshot& best_candidate =
        snapshot.candidates[static_cast<std::size_t>(best_index)];
    const float best_alignment = candidate_intent_alignment(
        best_candidate,
        intent,
        center.first,
        center.second);
    if (best_alignment < 0.70f) {
        return snapshot.state;
    }

    constexpr float kSwitchScoreMargin = 120.0f;
    if (selected_index >= 0 && best_score < selected_score + kSwitchScoreMargin) {
        return snapshot.state;
    }

    return state_from_candidate(snapshot, best_candidate);
}

NativeControllerVisionState TargetSnapshotProvider::credibility_gated_vision_state(
    const NativeControllerVisionState& state,
    double query_time_seconds,
    bool ads_active,
    bool* suppress_tracker_ingest) {
    if (suppress_tracker_ingest) {
        *suppress_tracker_ingest = false;
    }
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            state.has_target,
            state.aim_authority,
            state.fire_authority,
            state.target_tier);
    if (authority.assist_authority == common_native::AssistAuthority::None) {
        has_candidate_target_ = false;
        candidate_fresh_samples_ = 0;
        candidate_projection_hold_until_seconds_ = 0.0;
        candidate_reacquire_snap_until_seconds_ = 0.0;
        if (!state.has_target) {
            has_committed_target_ = false;
            committed_target_dx_ = 0.0f;
            committed_target_dy_ = 0.0f;
        }
        return state;
    }
    const auto open_candidate_reacquire_snap = [&]() {
        const double configured_snap_seconds =
            static_cast<double>(std::max(0, ai_config_.ads_snap_window_ms)) / 1000.0;
        if (configured_snap_seconds <= 0.0) {
            return;
        }
        const double reacquire_seconds =
            std::max(0.160, std::min(0.280, configured_snap_seconds * 2.0));
        candidate_reacquire_snap_until_seconds_ =
            query_time_seconds + reacquire_seconds;
    };
    const double query_time =
        query_time_seconds > 0.0 ? query_time_seconds : candidate_last_observed_at_seconds_;
    const double candidate_time = state.observed_at_seconds > 0.0
        ? state.observed_at_seconds
        : query_time;
    const tracking_native::TrackerSnapshot projection =
        target_tracker_->query({query_time});
    constexpr float kSuspiciousJumpPx = 48.0f;
    constexpr float kCandidateMatchPx = 80.0f;
    constexpr int kCandidateAcceptSamples = 4;
    constexpr double kCandidateAcceptSeconds = 0.070;
    constexpr double kCandidateMaxGapSeconds = 0.055;
    const auto update_candidate_from_state = [&]() {
        const bool matches_candidate =
            has_candidate_target_ &&
            candidate_time >= candidate_last_observed_at_seconds_ &&
            (candidate_time - candidate_last_observed_at_seconds_) <=
                kCandidateMaxGapSeconds &&
            std::hypot(state.dx - candidate_target_dx_, state.dy - candidate_target_dy_) <=
                kCandidateMatchPx;
        if (!matches_candidate) {
            has_candidate_target_ = true;
            candidate_target_dx_ = state.dx;
            candidate_target_dy_ = state.dy;
            candidate_first_observed_at_seconds_ = candidate_time;
            candidate_last_observed_at_seconds_ = candidate_time;
            candidate_fresh_samples_ = 1;
        } else {
            candidate_target_dx_ = state.dx;
            candidate_target_dy_ = state.dy;
            candidate_last_observed_at_seconds_ = candidate_time;
            ++candidate_fresh_samples_;
        }
    };
    const auto candidate_verified = [&](int min_samples, double min_seconds) {
        return authority.is_strong_aim_target &&
            candidate_fresh_samples_ >= min_samples &&
            (candidate_last_observed_at_seconds_ - candidate_first_observed_at_seconds_) >=
                min_seconds;
    };
    const auto commit_observed_state = [&](bool reopen_snap_for_candidate) {
        if (reopen_snap_for_candidate) {
            open_candidate_reacquire_snap();
        } else {
            candidate_reacquire_snap_until_seconds_ = 0.0;
        }
        has_committed_target_ = true;
        committed_target_dx_ = state.dx;
        committed_target_dy_ = state.dy;
        has_candidate_target_ = false;
        candidate_fresh_samples_ = 0;
        candidate_projection_hold_until_seconds_ = 0.0;
        candidate_output_hold_until_seconds_ = 0.0;
    };
    const auto candidate_hold_state = [&]() {
        NativeControllerVisionState gated = state;
        gated.has_target = false;
        gated.aim_authority = false;
        gated.fire_authority = false;
        gated.auto_fire_requested = false;
        gated.target_tier = "none";
        gated.has_tracker_projection = false;
        gated.tracker_dx = 0.0f;
        gated.tracker_dy = 0.0f;
        return gated;
    };
    if (!projection.has_target ||
        projection.assist_authority == common_native::AssistAuthority::None ||
        projection.projection_age_ms > 80.0) {
        const double submit_age_ms =
            state.observed_at_seconds > 0.0 && query_time > 0.0
                ? std::max(0.0, query_time - state.observed_at_seconds) * 1000.0
                : 0.0;
        const bool fresh_submit_timestamp = submit_age_ms <= 5.0;
        const float committed_jump_px = has_committed_target_
            ? std::hypot(
                state.dx - committed_target_dx_,
                state.dy - committed_target_dy_)
            : 0.0f;
        const bool suspicious_against_committed =
            fresh_submit_timestamp &&
            has_committed_target_ &&
            committed_jump_px > kSuspiciousJumpPx;
        if (authority.is_strong_aim_target && fresh_submit_timestamp &&
            (has_candidate_target_ || suspicious_against_committed)) {
            update_candidate_from_state();
            constexpr int kNoProjectionAcceptSamples = 5;
            constexpr double kNoProjectionAcceptSeconds = 0.070;
            if (candidate_verified(kNoProjectionAcceptSamples, kNoProjectionAcceptSeconds)) {
                commit_observed_state(true);
                return state;
            }
            if (suppress_tracker_ingest) {
                *suppress_tracker_ingest = true;
            }
            candidate_output_hold_until_seconds_ =
                std::max(candidate_output_hold_until_seconds_, query_time + 0.080);
            return candidate_hold_state();
        }
        if (authority.is_strong_aim_target) {
            commit_observed_state(has_candidate_target_);
        }
        return state;
    }

    const float jump_px = std::hypot(
        state.dx - projection.aim_error_px.x,
        state.dy - projection.aim_error_px.y);
    if (jump_px <= kSuspiciousJumpPx) {
        if (authority.is_strong_aim_target) {
            commit_observed_state(has_candidate_target_);
        }
        return state;
    }

    update_candidate_from_state();
    if (candidate_verified(kCandidateAcceptSamples, kCandidateAcceptSeconds)) {
        commit_observed_state(true);
        return state;
    }

    NativeControllerVisionState gated = state;
    const float projection_delta_x = projection.aim_error_px.x - gated.dx;
    const float projection_delta_y = projection.aim_error_px.y - gated.dy;
    gated.dx = projection.aim_error_px.x;
    gated.dy = projection.aim_error_px.y;
    gated.target_x += projection_delta_x;
    gated.target_y += projection_delta_y;
    if (projection.has_body_box) {
        gated.has_body_box = true;
        gated.body_x1 = projection.body_box_px.x;
        gated.body_y1 = projection.body_box_px.y;
        gated.body_x2 = projection.body_box_px.x + projection.body_box_px.w;
        gated.body_y2 = projection.body_box_px.y + projection.body_box_px.h;
    } else if (gated.has_body_box) {
        gated.body_x1 += projection_delta_x;
        gated.body_x2 += projection_delta_x;
        gated.body_y1 += projection_delta_y;
        gated.body_y2 += projection_delta_y;
    }
    gated.has_target = true;
    gated.aim_authority = true;
    gated.fire_authority = false;
    gated.auto_fire_requested = false;
    gated.target_tier = "projected";
    gated.observed_at_seconds = query_time;
    gated.has_tracker_projection = true;
    gated.tracker_dx = projection.aim_error_px.x;
    gated.tracker_dy = projection.aim_error_px.y;
    if (suppress_tracker_ingest) {
        *suppress_tracker_ingest = true;
    }
    double projection_hold_seconds = 0.055;
    const float aim_max_age_ms = std::max(0.0f, ai_config_.target_max_age_ms);
    if (ads_active && aim_max_age_ms > 0.0f && aim_max_age_ms <= 140.0f) {
        projection_hold_seconds = std::max(
            projection_hold_seconds,
            std::min(0.090, static_cast<double>(aim_max_age_ms) / 1000.0));
    }
    candidate_projection_hold_until_seconds_ =
        std::max(
            candidate_projection_hold_until_seconds_,
            query_time + projection_hold_seconds);
    return gated;
}

NativeControllerVisionState TargetSnapshotProvider::vision_state_for_frame(
    double now_seconds,
    bool ads_active) {
    NativeControllerVisionState state = latest_vision_state_;
    const bool state_expired_for_projection = state_age_exceeds_ms(
        state,
        now_seconds,
        ai_config_.target_projection_max_age_ms);
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            state.has_target,
            state.aim_authority,
            state.fire_authority,
            state.target_tier);
    const tracking_native::TrackerSnapshot projection =
        target_tracker_->query({now_seconds});
    const float projection_max_age_ms =
        std::max(0.0f, ai_config_.target_projection_max_age_ms);
    if (projection.has_target &&
        projection_max_age_ms > 0.0f &&
        projection.projection_age_ms > static_cast<double>(projection_max_age_ms)) {
        const float aim_max_age_ms =
            std::max(0.0f, ai_config_.target_max_age_ms);
        const bool short_ads_occlusion_grace =
            aim_max_age_ms > 0.0f &&
            aim_max_age_ms <= projection_max_age_ms + 60.0f;
        if (ads_active &&
            short_ads_occlusion_grace &&
            has_fresh_aim_target(state, now_seconds) &&
            (is_strong_aim_target(state) ||
             tracking_native::is_projected_observation(state.target_tier))) {
            candidate_projection_hold_until_seconds_ = 0.0;
            state.has_tracker_projection = false;
            state.tracker_dx = 0.0f;
            state.tracker_dy = 0.0f;
            return state;
        }
        latest_vision_state_ = cleared_target_state(latest_vision_state_);
        target_tracker_->reset();
        candidate_projection_hold_until_seconds_ = 0.0;
        return cleared_target_state(state);
    }
    if (!projection.has_target ||
        projection.assist_authority == common_native::AssistAuthority::None) {
        candidate_projection_hold_until_seconds_ = 0.0;
        if (state_expired_for_projection) {
            const float aim_max_age_ms =
                std::max(0.0f, ai_config_.target_max_age_ms);
            const bool short_ads_occlusion_grace =
                aim_max_age_ms > 0.0f &&
                aim_max_age_ms <= projection_max_age_ms + 60.0f;
            if (ads_active &&
                short_ads_occlusion_grace &&
                has_fresh_aim_target(state, now_seconds) &&
                (is_strong_aim_target(state) ||
                 tracking_native::is_projected_observation(state.target_tier))) {
                state.has_tracker_projection = false;
                state.tracker_dx = 0.0f;
                state.tracker_dy = 0.0f;
                return state;
            }
            latest_vision_state_ = cleared_target_state(latest_vision_state_);
            target_tracker_->reset();
            return cleared_target_state(state);
        }
        return state;
    }
    if (candidate_projection_hold_until_seconds_ > 0.0 &&
        now_seconds <= candidate_projection_hold_until_seconds_) {
        NativeControllerVisionState held = state;
        const float projection_delta_x = projection.aim_error_px.x - held.dx;
        const float projection_delta_y = projection.aim_error_px.y - held.dy;
        held.dx = projection.aim_error_px.x;
        held.dy = projection.aim_error_px.y;
        held.target_x += projection_delta_x;
        held.target_y += projection_delta_y;
        if (projection.has_body_box) {
            held.has_body_box = true;
            held.body_x1 = projection.body_box_px.x;
            held.body_y1 = projection.body_box_px.y;
            held.body_x2 = projection.body_box_px.x + projection.body_box_px.w;
            held.body_y2 = projection.body_box_px.y + projection.body_box_px.h;
        } else if (held.has_body_box) {
            held.body_x1 += projection_delta_x;
            held.body_x2 += projection_delta_x;
            held.body_y1 += projection_delta_y;
            held.body_y2 += projection_delta_y;
        }
        held.has_target = true;
        held.aim_authority = true;
        held.fire_authority = false;
        held.auto_fire_requested = false;
        held.target_tier = "projected";
        held.observed_at_seconds = now_seconds;
        held.has_tracker_projection = true;
        held.tracker_dx = projection.aim_error_px.x;
        held.tracker_dy = projection.aim_error_px.y;
        return held;
    }
    if (candidate_projection_hold_until_seconds_ > 0.0 &&
        now_seconds > candidate_projection_hold_until_seconds_) {
        candidate_projection_hold_until_seconds_ = 0.0;
    }
    if (candidate_output_hold_until_seconds_ > 0.0) {
        if (now_seconds <= candidate_output_hold_until_seconds_) {
            return state;
        }
        candidate_output_hold_until_seconds_ = 0.0;
    }

    const bool had_selector_target =
        authority.assist_authority != common_native::AssistAuthority::None;
    if (had_selector_target && has_fresh_aim_target(state, now_seconds) &&
        latest_vision_sequence_ != raw_vision_sequence_consumed_) {
        raw_vision_sequence_consumed_ = latest_vision_sequence_;
        state.has_tracker_projection = true;
        state.tracker_dx = projection.aim_error_px.x;
        state.tracker_dy = projection.aim_error_px.y;
        return state;
    }
    if (!had_selector_target) {
        state.has_target = true;
        state.auto_fire_requested = false;
        state.aim_authority = true;
        state.fire_authority = false;
        state.target_tier = "projected";
        if (state.screen_center_x <= 0.0f && projection.has_body_box) {
            state.screen_center_x = (projection.body_box_px.x + (projection.body_box_px.w * 0.5f)) -
                projection.aim_error_px.x;
        }
        if (state.screen_center_y <= 0.0f && projection.has_body_box) {
            state.screen_center_y = (projection.body_box_px.y + (projection.body_box_px.h * 0.5f)) -
                projection.aim_error_px.y;
        }
        if (state.target_x == 0.0f && state.screen_center_x > 0.0f) {
            state.target_x = state.screen_center_x;
        }
        if (state.target_y == 0.0f && state.screen_center_y > 0.0f) {
            state.target_y = state.screen_center_y;
        }
    }
    const float projection_delta_x = projection.aim_error_px.x - state.dx;
    const float projection_delta_y = projection.aim_error_px.y - state.dy;
    state.dx = projection.aim_error_px.x;
    state.dy = projection.aim_error_px.y;
    state.target_x += projection_delta_x;
    state.target_y += projection_delta_y;
    if (projection.has_body_box) {
        state.has_body_box = true;
        state.body_x1 = projection.body_box_px.x;
        state.body_y1 = projection.body_box_px.y;
        state.body_x2 = projection.body_box_px.x + projection.body_box_px.w;
        state.body_y2 = projection.body_box_px.y + projection.body_box_px.h;
    } else if (state.has_body_box) {
        state.body_x1 += projection_delta_x;
        state.body_x2 += projection_delta_x;
        state.body_y1 += projection_delta_y;
        state.body_y2 += projection_delta_y;
    }
    state.observed_at_seconds = now_seconds;
    if (!had_selector_target && projection.source != tracking_native::TrackerSnapshotSource::Observed) {
        state.fire_authority = false;
        state.auto_fire_requested = false;
        state.target_tier = "projected";
    }
    state.has_tracker_projection = true;
    state.tracker_dx = projection.aim_error_px.x;
    state.tracker_dy = projection.aim_error_px.y;
    return state;
}

void TargetSnapshotProvider::record_output(
    const NativeControllerOutputComponents& components,
    double now_seconds) {
    const double dt = last_output_at_seconds_ > 0.0
        ? std::max(0.0, now_seconds - last_output_at_seconds_)
        : 0.0;
    last_output_at_seconds_ = now_seconds;
    if (dt <= 0.0) {
        return;
    }
    tracking_native::TrackerControlSample sample;
    sample.apply_time = {now_seconds};
    sample.dt = {dt};
    sample.sticks.manual = {components.manual_stick.x, components.manual_stick.y};
    sample.sticks.assist = {components.ai_aim_stick.x, components.ai_aim_stick.y};
    sample.sticks.dynamics = {
        components.dynamic_adjustment_stick.x,
        components.dynamic_adjustment_stick.y};
    sample.sticks.recoil = {components.recoil_stick.x, components.recoil_stick.y};
    sample.sticks.final_output = {components.final_stick.x, components.final_stick.y};
    target_tracker_->push_control_sample(sample);
}

void TargetSnapshotProvider::clear_ads_transient_state() {
    candidate_reacquire_snap_until_seconds_ = 0.0;
    candidate_output_hold_until_seconds_ = 0.0;
}

void TargetSnapshotProvider::clear_target_state_observed_before(double cutoff_seconds) {
    if (cutoff_seconds <= 0.0) {
        return;
    }
    if (!latest_vision_state_.has_target) {
        clear_ads_transient_state();
        return;
    }
    if (latest_vision_state_.observed_at_seconds > cutoff_seconds) {
        clear_ads_transient_state();
        return;
    }

    latest_vision_state_ = cleared_target_state(latest_vision_state_);
    target_tracker_->reset();
    ++latest_vision_sequence_;
    raw_vision_sequence_consumed_ = latest_vision_sequence_;
    has_committed_target_ = false;
    committed_target_dx_ = 0.0f;
    committed_target_dy_ = 0.0f;
    has_candidate_target_ = false;
    candidate_target_dx_ = 0.0f;
    candidate_target_dy_ = 0.0f;
    candidate_first_observed_at_seconds_ = 0.0;
    candidate_last_observed_at_seconds_ = 0.0;
    candidate_fresh_samples_ = 0;
    candidate_projection_hold_until_seconds_ = 0.0;
    candidate_reacquire_snap_until_seconds_ = 0.0;
    candidate_output_hold_until_seconds_ = 0.0;
}

bool TargetSnapshotProvider::candidate_reacquire_snap_active(double now_seconds) const {
    return candidate_reacquire_snap_until_seconds_ > 0.0 &&
        now_seconds <= candidate_reacquire_snap_until_seconds_;
}

bool TargetSnapshotProvider::candidate_output_hold_active(double now_seconds) {
    if (candidate_output_hold_until_seconds_ <= 0.0) {
        return false;
    }
    if (now_seconds <= candidate_output_hold_until_seconds_) {
        return true;
    }
    candidate_output_hold_until_seconds_ = 0.0;
    return false;
}

bool TargetSnapshotProvider::has_fresh_aim_target(
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    const float max_age_ms = ai_config_.target_max_age_ms;
    if (max_age_ms <= 0.0f || vision_state.observed_at_seconds <= 0.0 ||
        now_seconds <= 0.0) {
        return vision_state.has_target && vision_state.aim_authority;
    }
    const double age_seconds = std::max(0.0, now_seconds - vision_state.observed_at_seconds);
    return vision_state.has_target &&
        vision_state.aim_authority &&
        age_seconds <= (static_cast<double>(max_age_ms) / 1000.0);
}

bool TargetSnapshotProvider::is_strong_aim_target(
    const NativeControllerVisionState& vision_state) const {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            vision_state.has_target,
            vision_state.aim_authority,
            vision_state.fire_authority,
            vision_state.target_tier);
    return authority.is_strong_aim_target;
}

}  // namespace controller_native
