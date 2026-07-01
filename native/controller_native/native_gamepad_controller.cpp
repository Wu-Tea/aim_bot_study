#include "native_gamepad_controller.h"

#include "controller_pipeline.h"
#include "target_tracker.h"
#include "vision_snapshot_adapter.h"

#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace controller_native {

namespace {

double current_seconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

bool has_manual_right_stick_input(float manual_right_x, float manual_right_y) {
    constexpr float kManualFireReadyStickThreshold = 0.08f;
    return std::hypot(manual_right_x, manual_right_y) >= kManualFireReadyStickThreshold;
}

int axis_sign(float value, float deadzone) {
    if (value > deadzone) {
        return 1;
    }
    if (value < -deadzone) {
        return -1;
    }
    return 0;
}

constexpr float kWrongWayCorrectionCap = 0.24f;
constexpr float kWrongWayCorrectionMin = 0.08f;

float target_correction_sign(float error, bool y_axis, float error_deadzone) {
    const int error_sign = axis_sign(error, error_deadzone);
    if (error_sign == 0) {
        return 0.0f;
    }
    return static_cast<float>(y_axis ? -error_sign : error_sign);
}

float bounded_wrong_way_correction(
    float output_axis,
    float current_error,
    bool y_axis,
    float error_deadzone) {
    const float correction_sign =
        target_correction_sign(current_error, y_axis, error_deadzone);
    if (correction_sign == 0.0f) {
        return 0.0f;
    }
    const float correction_magnitude = std::min(
        kWrongWayCorrectionCap,
        std::max(kWrongWayCorrectionMin, std::fabs(output_axis) * 0.45f));
    return correction_sign * correction_magnitude;
}

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

}  // namespace

NativeGamepadController::NativeGamepadController(
    GamepadRuntimeConfig config,
    std::function<double()> clock)
    : config_(std::move(config)),
      ai_aim_(config_.ai_aim),
      aim_assist_dynamics_(config_.aim_assist_dynamics),
      recoil_(config_.recoil),
      target_tracker_(tracking_native::create_tracker_backend(
          config_.tracker_backend,
          target_tracker_config_from_ai_aim(config_.ai_aim))),
      clock_(std::move(clock)) {
    recoil_.set_recognizer_state_path(config_.recoil.recognizer_state_path);
    if (!config_.recoil.recognizer_state_path.empty()) {
        recoil_.load_profile_directory(config_.recoil.profile_directory);
    }
    recoil_.load_calibration_directory(config_.recoil.calibration_directory);
}

void NativeGamepadController::reset() {
    latest_vision_state_ = NativeControllerVisionState{};
    target_tracker_->reset();
    ai_aim_.reset();
    aim_assist_dynamics_.reset();
    recoil_.reset();
    last_pipeline_traces_.clear();
    last_tracker_motion_output_ = GamepadOutputState{};
    last_output_components_ = NativeControllerOutputComponents{};
    last_frame_vision_state_ = NativeControllerVisionState{};
    manual_fire_was_pressed_ = false;
    auto_fire_was_active_ = false;
    manual_takeover_started_at_seconds_ = -1.0;
    last_output_at_seconds_ = 0.0;
    latest_vision_sequence_ = 0;
    raw_vision_sequence_consumed_ = 0;
    ads_active_ = false;
    ads_started_at_seconds_ = 0.0;
    has_last_body_lock_short_plan_x_ = false;
    has_last_body_lock_short_plan_y_ = false;
    last_body_lock_short_plan_x_ = 0.0f;
    last_body_lock_short_plan_y_ = 0.0f;
    has_last_body_lock_error_for_plan_ = false;
    last_body_lock_plan_error_x_ = 0.0f;
    last_body_lock_plan_error_y_ = 0.0f;
    has_last_aim_error_for_plan_ = false;
    last_aim_plan_error_x_ = 0.0f;
    last_aim_plan_error_y_ = 0.0f;
    has_last_output_validation_error_ = false;
    last_output_validation_error_x_ = 0.0f;
    last_output_validation_error_y_ = 0.0f;
    output_validation_correction_x_until_seconds_ = 0.0;
    output_validation_correction_y_until_seconds_ = 0.0;
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
    body_lock_short_plan_x_until_seconds_ = 0.0;
    body_lock_short_plan_y_until_seconds_ = 0.0;
    body_lock_manual_brake_x_until_seconds_ = 0.0;
    body_lock_manual_brake_y_until_seconds_ = 0.0;
    body_lock_manual_brake_x_sign_ = 0;
    body_lock_manual_brake_y_sign_ = 0;
    reset_auto_fire_readiness_tracking();
}

void NativeGamepadController::submit_vision_state(const NativeControllerVisionState& state) {
    const double capture_time = state.observed_at_seconds > 0.0
        ? state.observed_at_seconds
        : now_seconds();
    const double ready_time = now_seconds();
    bool suppress_tracker_ingest = false;
    latest_vision_state_ =
        credibility_gated_vision_state(state, ready_time, &suppress_tracker_ingest);
    ++latest_vision_sequence_;
    if (!suppress_tracker_ingest) {
        ingest_tracker_observation(state, {}, 0, capture_time, ready_time);
    }
}

void NativeGamepadController::ingest_tracker_observation(
    const NativeControllerVisionState& state,
    const std::vector<tracking_native::TrackerDetection>& detections,
    std::uint64_t frame_id,
    double capture_time_seconds,
    double ready_time_seconds) {
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
        capture_time_seconds > 0.0 ? capture_time_seconds : now_seconds()};
    observation.ready_time = {
        ready_time_seconds > 0.0 ? ready_time_seconds : observation.capture_time.value};
    observation.screen_center_px = {state.screen_center_x, state.screen_center_y};
    observation.frame_id = frame_id;
    observation.detections = detections;
    target_tracker_->ingest(observation);
}

NativeControllerVisionState NativeGamepadController::credibility_gated_vision_state(
    const NativeControllerVisionState& state,
    double query_time_seconds,
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
            static_cast<double>(std::max(0, config_.ai_aim.ads_snap_window_ms)) / 1000.0;
        if (configured_snap_seconds <= 0.0) {
            return;
        }
        const double reacquire_seconds =
            std::max(0.160, std::min(0.280, configured_snap_seconds * 2.0));
        candidate_reacquire_snap_until_seconds_ =
            query_time_seconds + reacquire_seconds;
    };
    const double query_time =
        query_time_seconds > 0.0 ? query_time_seconds : now_seconds();
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
    gated.observed_at_seconds = query_time_seconds;
    gated.has_tracker_projection = true;
    gated.tracker_dx = projection.aim_error_px.x;
    gated.tracker_dy = projection.aim_error_px.y;
    if (suppress_tracker_ingest) {
        *suppress_tracker_ingest = true;
    }
    double projection_hold_seconds = 0.055;
    const float aim_max_age_ms = std::max(0.0f, config_.ai_aim.target_max_age_ms);
    if (ads_active_ && aim_max_age_ms > 0.0f && aim_max_age_ms <= 140.0f) {
        projection_hold_seconds = std::max(
            projection_hold_seconds,
            std::min(0.090, static_cast<double>(aim_max_age_ms) / 1000.0));
    }
    candidate_projection_hold_until_seconds_ =
        std::max(
            candidate_projection_hold_until_seconds_,
            query_time_seconds + projection_hold_seconds);
    return gated;
}

void NativeGamepadController::submit_vision_result(const vision_native::VisionResult& result) {
    const ControllerVisionSnapshot snapshot = adapt_vision_result(result);
    if (!snapshot.frame_updated) {
        return;
    }

    bool suppress_tracker_ingest = false;
    latest_vision_state_ = credibility_gated_vision_state(
        snapshot.state,
        snapshot.ready_time_seconds > 0.0
            ? snapshot.ready_time_seconds
            : snapshot.capture_time_seconds,
        &suppress_tracker_ingest);
    ++latest_vision_sequence_;
    if (!suppress_tracker_ingest) {
        ingest_tracker_observation(
            snapshot.state,
            snapshot.tracker_detections,
            snapshot.frame_id,
            snapshot.capture_time_seconds,
            snapshot.ready_time_seconds);
    }
}

GamepadOutputState NativeGamepadController::build_output(const PhysicalGamepadState& physical) {
    last_pipeline_traces_.clear();

    GamepadOutputState output = output_from_physical_input(physical);
    NativeControllerOutputComponents output_components =
        output_components_from_manual_output(output);

    const double now = now_seconds();
    const NativeControllerVisionState frame_vision_state = vision_state_for_frame(now);
    last_frame_vision_state_ = frame_vision_state;
    const bool aiming = is_aiming(physical);
    update_ads_state(aiming, now);
    const bool auto_fire_requested = frame_vision_state.auto_fire_requested;
    const float manual_right_x = output.right_x;
    const float manual_right_y = output.right_y;

    // Pipeline contract: vision/tracker state feeds controller assistance first;
    // recoil stays the final feed-forward stage and does not receive target error.
    GamepadOutputState stage_before_output = output;
    float stage_before_right_y = output.right_y;
    apply_ai_aim(output, physical, frame_vision_state, now);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.ai_aim_stick);
    record_stage_trace("ai_aim", stage_before_right_y, output, false, false);

    const bool aim_ready = auto_fire_aim_ready(
        frame_vision_state,
        aiming,
        now,
        manual_right_x,
        manual_right_y,
        output);
    bool should_fire = auto_fire_allowed(frame_vision_state, aiming, now, aim_ready);
    stage_before_output = output;
    stage_before_right_y = output.right_y;
    apply_aim_assist_dynamics(output, manual_right_x, manual_right_y, physical, should_fire);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.dynamic_adjustment_stick);
    record_stage_trace(
        "aim_assist_dynamics",
        stage_before_right_y,
        output,
        should_fire,
        should_fire);

    const bool manual_fire_is_pressed = manual_fire_pressed(physical);
    const bool manual_fire_started = manual_fire_is_pressed && !manual_fire_was_pressed_;
    if (manual_fire_started && (should_fire || auto_fire_was_active_)) {
        manual_takeover_started_at_seconds_ = now;
    }

    double takeover_elapsed = manual_takeover_elapsed(now);
    bool in_takeover_release = takeover_elapsed >= 0.0 &&
        takeover_elapsed < std::max(0.0f, config_.auto_fire.manual_takeover_release_seconds);
    bool in_takeover_guard = takeover_elapsed >= 0.0 &&
        takeover_elapsed < manual_takeover_total_seconds();
    if (takeover_elapsed >= manual_takeover_total_seconds()) {
        manual_takeover_started_at_seconds_ = -1.0;
        in_takeover_release = false;
        in_takeover_guard = false;
    }

    manual_fire_was_pressed_ = manual_fire_is_pressed;
    if (manual_fire_is_pressed) {
        should_fire = false;
        if (in_takeover_release) {
            release_fire_output(output);
        }
    } else if (in_takeover_guard) {
        should_fire = false;
        release_fire_output(output);
    }

    if (auto_fire_requested) {
        ++auto_fire_counters_.requested;
        if (should_fire) {
            ++auto_fire_counters_.allowed;
        } else {
            ++auto_fire_counters_.blocked;
        }
    }
    stage_before_right_y = output.right_y;
    apply_auto_fire(output, should_fire);
    record_stage_trace("auto_fire", stage_before_right_y, output, false, should_fire);

    auto_fire_was_active_ = should_fire;
    stage_before_output = output;
    stage_before_right_y = output.right_y;
    apply_recoil(output, physical, should_fire, now);
    capture_output_component_delta(
        stage_before_output,
        output,
        &output_components.recoil_stick);
    record_stage_trace("recoil", stage_before_right_y, output, should_fire, should_fire);
    capture_final_output_component(output, &output_components);
    // Tracker receives final camera motion for ego projection, while the
    // component split preserves manual/assist/dynamics/recoil attribution.
    record_target_tracker_output(output_components, now);
    last_output_components_ = output_components;
    return output;
}

NativeAutoFireCounters NativeGamepadController::auto_fire_counters() const {
    return auto_fire_counters_;
}

const std::vector<NativeControllerStageTrace>& NativeGamepadController::last_pipeline_traces() const {
    return last_pipeline_traces_;
}

GamepadOutputState NativeGamepadController::last_tracker_motion_output() const {
    return last_tracker_motion_output_;
}

const NativeControllerOutputComponents& NativeGamepadController::last_output_components() const {
    return last_output_components_;
}

const NativeControllerVisionState& NativeGamepadController::last_frame_vision_state() const {
    return last_frame_vision_state_;
}

const std::string& NativeGamepadController::last_ai_aim_mode() const {
    return ai_aim_.last_mode();
}

bool NativeGamepadController::is_aiming(const PhysicalGamepadState& physical) const {
    return physical.left_trigger > 0.05f || (
        config_.rb_counts_as_aiming && physical.rb);
}

NativeControllerVisionState NativeGamepadController::vision_state_for_frame(double now_seconds) {
    NativeControllerVisionState state = latest_vision_state_;
    const bool state_expired_for_projection = state_age_exceeds_ms(
        state,
        now_seconds,
        config_.ai_aim.target_projection_max_age_ms);
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            state.has_target,
            state.aim_authority,
            state.fire_authority,
            state.target_tier);
    const tracking_native::TrackerSnapshot projection =
        target_tracker_->query({now_seconds});
    const float projection_max_age_ms =
        std::max(0.0f, config_.ai_aim.target_projection_max_age_ms);
    if (projection.has_target &&
        projection_max_age_ms > 0.0f &&
        projection.projection_age_ms > static_cast<double>(projection_max_age_ms)) {
        const float aim_max_age_ms =
            std::max(0.0f, config_.ai_aim.target_max_age_ms);
        const bool short_ads_occlusion_grace =
            aim_max_age_ms > 0.0f &&
            aim_max_age_ms <= projection_max_age_ms + 60.0f;
        if (ads_active_ &&
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
                std::max(0.0f, config_.ai_aim.target_max_age_ms);
            const bool short_ads_occlusion_grace =
                aim_max_age_ms > 0.0f &&
                aim_max_age_ms <= projection_max_age_ms + 60.0f;
            if (ads_active_ &&
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

bool NativeGamepadController::auto_fire_allowed(
    const NativeControllerVisionState& vision_state,
    bool aiming,
    double now_seconds,
    bool aim_ready) const {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            vision_state.has_target,
            vision_state.aim_authority,
            vision_state.fire_authority,
            vision_state.target_tier);
    const bool aiming_allowed = !config_.auto_fire.aim_only || aiming;
    const bool readiness_allowed = !config_.auto_fire.require_aim_ready || aim_ready;
    return aiming_allowed &&
        readiness_allowed &&
        vision_state.auto_fire_requested &&
        vision_state.has_target &&
        authority.fire_authority == common_native::FireAuthority::ObservedOnly &&
        has_fresh_auto_fire_source(vision_state, now_seconds);
}

bool NativeGamepadController::has_fresh_auto_fire_source(
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    const float max_age_ms = config_.auto_fire.max_source_age_ms;
    if (max_age_ms <= 0.0f || vision_state.observed_at_seconds <= 0.0 ||
        now_seconds <= 0.0) {
        return true;
    }
    const double age_seconds = std::max(0.0, now_seconds - vision_state.observed_at_seconds);
    return age_seconds <= (static_cast<double>(max_age_ms) / 1000.0);
}

bool NativeGamepadController::has_fresh_aim_target(
    const NativeControllerVisionState& vision_state,
    double now_seconds) const {
    const float max_age_ms = config_.ai_aim.target_max_age_ms;
    if (max_age_ms <= 0.0f || vision_state.observed_at_seconds <= 0.0 ||
        now_seconds <= 0.0) {
        return vision_state.has_target && vision_state.aim_authority;
    }
    const double age_seconds = std::max(0.0, now_seconds - vision_state.observed_at_seconds);
    return vision_state.has_target &&
        vision_state.aim_authority &&
        age_seconds <= (static_cast<double>(max_age_ms) / 1000.0);
}

void NativeGamepadController::update_ads_state(bool aiming, double now_seconds) {
    if (aiming) {
        if (!ads_active_) {
            ads_active_ = true;
            ads_started_at_seconds_ = now_seconds;
            reset_auto_fire_readiness_tracking();
        }
        return;
    }

    ads_active_ = false;
    ads_started_at_seconds_ = 0.0;
    candidate_reacquire_snap_until_seconds_ = 0.0;
    candidate_output_hold_until_seconds_ = 0.0;
    reset_auto_fire_readiness_tracking();
}

bool NativeGamepadController::auto_fire_aim_ready(
    const NativeControllerVisionState& vision_state,
    bool aiming,
    double now_seconds,
    float manual_right_x,
    float manual_right_y,
    const GamepadOutputState& output) {
    if (!config_.auto_fire.require_aim_ready) {
        return true;
    }
    if (!vision_state.auto_fire_requested) {
        reset_auto_fire_readiness_tracking();
        return true;
    }
    if (!aiming || !has_fresh_aim_target(vision_state, now_seconds)) {
        reset_auto_fire_readiness_tracking();
        return false;
    }
    if (!is_strong_fire_target(vision_state)) {
        reset_auto_fire_readiness_tracking();
        return false;
    }

    const double min_ads_seconds =
        static_cast<double>(std::max(0.0f, config_.ai_aim.auto_fire_ready_min_ads_ms)) / 1000.0;
    if (!ads_active_ || ads_started_at_seconds_ <= 0.0 ||
        now_seconds - ads_started_at_seconds_ < min_ads_seconds) {
        reset_auto_fire_readiness_tracking();
        return false;
    }

    float settle_dx = vision_state.dx;
    float settle_dy = vision_state.dy;
    float body_lock_dx = 0.0f;
    float body_lock_dy = 0.0f;
    if (body_lock_error_for_state(vision_state, &body_lock_dx, &body_lock_dy)) {
        settle_dx = body_lock_dx;
        settle_dy = body_lock_dy;
    }
    const float error_px = static_cast<float>(std::hypot(
        settle_dx * config_.ai_aim.ai_delta_gain,
        settle_dy * config_.ai_aim.ai_delta_gain));
    if (error_px > std::max(0.0f, config_.ai_aim.auto_fire_ready_error_px)) {
        reset_auto_fire_readiness_tracking();
        return false;
    }

    const float ai_stick_mag =
        std::max(
            std::fabs(output.right_x - manual_right_x),
            std::fabs(output.right_y - manual_right_y)) *
        32767.0f;
    const float max_ai_stick = std::max(0.0f, config_.ai_aim.auto_fire_ready_max_ai_stick);
    const bool manual_tracking =
        has_manual_right_stick_input(manual_right_x, manual_right_y) &&
        vision_state.auto_fire_requested;
    if (max_ai_stick > 0.0f && ai_stick_mag > max_ai_stick && !manual_tracking) {
        reset_auto_fire_readiness_tracking();
        return false;
    }

    ++auto_fire_ready_frames_;
    return auto_fire_ready_frames_ >= std::max(1, config_.ai_aim.auto_fire_ready_frames);
}

void NativeGamepadController::reset_auto_fire_readiness_tracking() {
    auto_fire_ready_frames_ = 0;
}

bool NativeGamepadController::is_strong_fire_target(
    const NativeControllerVisionState& vision_state) const {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            vision_state.has_target,
            vision_state.aim_authority,
            vision_state.fire_authority,
            vision_state.target_tier);
    return authority.fire_authority == common_native::FireAuthority::ObservedOnly;
}

bool NativeGamepadController::is_strong_aim_target(
    const NativeControllerVisionState& vision_state) const {
    const tracking_native::TargetAuthorityDecision authority =
        tracking_native::classify_target_authority(
            vision_state.has_target,
            vision_state.aim_authority,
            vision_state.fire_authority,
            vision_state.target_tier);
    return authority.is_strong_aim_target;
}

bool NativeGamepadController::ads_snap_active_for_frame(
    const NativeControllerVisionState& vision_state,
    bool aiming,
    double now_seconds) const {
    if (!aiming || !has_fresh_aim_target(vision_state, now_seconds) ||
        !is_strong_aim_target(vision_state)) {
        return false;
    }
    if (candidate_reacquire_snap_until_seconds_ > 0.0 &&
        now_seconds <= candidate_reacquire_snap_until_seconds_) {
        return true;
    }
    if (!ads_active_ || ads_started_at_seconds_ <= 0.0) {
        return false;
    }
    const double window_seconds =
        static_cast<double>(std::max(0, config_.ai_aim.ads_snap_window_ms)) / 1000.0;
    return now_seconds - ads_started_at_seconds_ <= window_seconds;
}

float NativeGamepadController::ads_snap_progress_ratio(double now_seconds) const {
    if (!ads_active_ || ads_started_at_seconds_ <= 0.0) {
        return 0.0f;
    }
    const double window_seconds =
        static_cast<double>(std::max(1, config_.ai_aim.ads_snap_window_ms)) / 1000.0;
    const double elapsed = std::max(0.0, now_seconds - ads_started_at_seconds_);
    return static_cast<float>(std::max(0.0, std::min(1.0, elapsed / window_seconds)));
}

float NativeGamepadController::ads_snap_remaining_seconds(double now_seconds) const {
    if (!ads_active_ || ads_started_at_seconds_ <= 0.0) {
        return 0.0f;
    }
    const double window_seconds =
        static_cast<double>(std::max(1, config_.ai_aim.ads_snap_window_ms)) / 1000.0;
    const double elapsed = std::max(0.0, now_seconds - ads_started_at_seconds_);
    return static_cast<float>(std::max(0.0, window_seconds - elapsed));
}

bool NativeGamepadController::body_lock_error_for_state(
    const NativeControllerVisionState& vision_state,
    float* out_dx,
    float* out_dy) const {
    if (!vision_state.has_target || !vision_state.aim_authority ||
        !vision_state.has_body_box ||
        vision_state.body_x2 <= vision_state.body_x1 ||
        vision_state.body_y2 <= vision_state.body_y1) {
        return false;
    }
    const float tolerance = std::max(0.0f, config_.ai_aim.body_lock_box_tolerance_px);
    if (vision_state.screen_center_x < vision_state.body_x1 - tolerance ||
        vision_state.screen_center_x > vision_state.body_x2 + tolerance ||
        vision_state.screen_center_y < vision_state.body_y1 - tolerance ||
        vision_state.screen_center_y > vision_state.body_y2 + tolerance) {
        return false;
    }

    const float ratio = std::max(
        0.0f,
        std::min(1.0f, config_.ai_aim.body_lock_upper_body_ratio));
    const float lock_x = (vision_state.body_x1 + vision_state.body_x2) * 0.5f;
    const float lock_y =
        vision_state.body_y1 + ((vision_state.body_y2 - vision_state.body_y1) * ratio);
    const float lock_dx = lock_x - vision_state.screen_center_x;
    const float lock_dy = lock_y - vision_state.screen_center_y;
    const float activation_half =
        std::max(0.0f, config_.ai_aim.body_lock_activation_box_px) * 0.5f;
    if (std::fabs(lock_dx * config_.ai_aim.ai_delta_gain) > activation_half ||
        std::fabs(lock_dy * config_.ai_aim.ai_delta_gain) > activation_half) {
        return false;
    }

    if (out_dx != nullptr) {
        *out_dx = lock_dx;
    }
    if (out_dy != nullptr) {
        *out_dy = lock_dy;
    }
    return true;
}

bool NativeGamepadController::manual_fire_pressed(const PhysicalGamepadState& physical) const {
    return physical.rb || physical.right_trigger > 0.04f;
}

double NativeGamepadController::manual_takeover_elapsed(double now_seconds) const {
    if (manual_takeover_started_at_seconds_ < 0.0) {
        return -1.0;
    }
    return std::max(0.0, now_seconds - manual_takeover_started_at_seconds_);
}

double NativeGamepadController::manual_takeover_total_seconds() const {
    return std::max(0.0f, config_.auto_fire.manual_takeover_release_seconds) +
        std::max(0.0f, config_.auto_fire.manual_takeover_resume_delay_seconds);
}

void NativeGamepadController::apply_auto_fire(
    GamepadOutputState& output,
    bool should_fire) const {
    if (!should_fire) {
        return;
    }
    if (config_.auto_fire.fire_output == "RT") {
        output.right_trigger = 1.0f;
        return;
    }
    output.rb = true;
}

void NativeGamepadController::release_fire_output(GamepadOutputState& output) const {
    output.rb = false;
    output.right_trigger = 0.0f;
}

void NativeGamepadController::apply_ai_aim(
    GamepadOutputState& output,
    const PhysicalGamepadState& physical,
    const NativeControllerVisionState& vision_state,
    double now_seconds) {
    NativeAiAimInput input;
    input.aiming = is_aiming(physical);
    input.has_target = vision_state.has_target;
    input.aim_authority = vision_state.aim_authority;
    input.ads_snap_active = ads_snap_active_for_frame(vision_state, input.aiming, now_seconds);
    input.fire_active =
        physical.rb || physical.right_trigger > 0.04f ||
        vision_state.auto_fire_requested || auto_fire_was_active_;
    input.ads_snap_progress_ratio = ads_snap_progress_ratio(now_seconds);
    input.ads_snap_remaining_seconds = ads_snap_remaining_seconds(now_seconds);
    input.dx = vision_state.dx;
    input.dy = vision_state.dy;
    input.has_mixing_reference = vision_state.has_tracker_projection;
    input.mixing_reference_dx = vision_state.tracker_dx;
    input.mixing_reference_dy = vision_state.tracker_dy;
    input.target_x = vision_state.target_x;
    input.target_y = vision_state.target_y;
    input.screen_center_x = vision_state.screen_center_x;
    input.screen_center_y = vision_state.screen_center_y;
    input.has_body_box = vision_state.has_body_box;
    input.body_x1 = vision_state.body_x1;
    input.body_y1 = vision_state.body_y1;
    input.body_x2 = vision_state.body_x2;
    input.body_y2 = vision_state.body_y2;
    input.target_tier = vision_state.target_tier;
    input.observed_at_seconds = vision_state.observed_at_seconds;
    input.now_seconds = now_seconds;
    input.manual_right_x = output.right_x;
    input.manual_right_y = output.right_y;

    const NativeAiAimOutput assist = ai_aim_.compute(input);
    if (assist.has_assist) {
        output.right_x = clamp_unit(output.right_x + assist.assist_x);
        output.right_y = clamp_unit(output.right_y + assist.assist_y);
    }
    apply_body_lock_short_plan(
        output,
        input.manual_right_x,
        input.manual_right_y,
        !input.fire_active,
        vision_state,
        now_seconds);
    if (vision_state.has_tracker_projection) {
        constexpr float kManualCorrectionDeadzone = 0.05f;
        const auto manual_corrects_tracker_axis = [&](
            float manual_axis,
            float tracker_error,
            bool y_axis) {
            if (std::fabs(manual_axis) <= kManualCorrectionDeadzone ||
                std::fabs(tracker_error) <= 1.0f) {
                return false;
            }
            return y_axis
                ? (manual_axis * tracker_error < 0.0f)
                : (manual_axis * tracker_error > 0.0f);
        };
        const auto preserve_manual_correction = [&](
            float tracker_error,
            float manual_axis,
            float& output_axis,
            bool y_axis) {
            if (!manual_corrects_tracker_axis(manual_axis, tracker_error, y_axis)) {
                return;
            }
            const bool output_opposes_manual = output_axis * manual_axis < 0.0f;
            const bool output_erases_manual =
                std::fabs(output_axis) < (std::fabs(manual_axis) * 0.75f);
            if (output_opposes_manual || output_erases_manual) {
                output_axis = manual_axis;
            }
        };
        preserve_manual_correction(
            vision_state.tracker_dx,
            input.manual_right_x,
            output.right_x,
            false);
        preserve_manual_correction(
            vision_state.tracker_dy,
            input.manual_right_y,
            output.right_y,
            true);
        if (!vision_state.aim_authority ||
            tracking_native::is_projected_observation(vision_state.target_tier)) {
            const auto output_pushes_away_tracker = [](
                float output_axis,
                float tracker_error,
                bool y_axis) {
                if (output_axis == 0.0f || std::fabs(tracker_error) <= 1.0f) {
                    return false;
                }
                return y_axis
                    ? (output_axis * tracker_error > 0.0f)
                    : (output_axis * tracker_error < 0.0f);
            };
            if (output_pushes_away_tracker(output.right_x, vision_state.tracker_dx, false)) {
                output.right_x = 0.0f;
            }
            if (output_pushes_away_tracker(output.right_y, vision_state.tracker_dy, true)) {
                output.right_y = 0.0f;
            }
        }
    }
    if (vision_state.has_target && vision_state.aim_authority &&
        !tracking_native::is_projected_observation(vision_state.target_tier)) {
        constexpr float kCrossErrorDeadzonePx = 1.0f;
        constexpr double kOutputValidationCorrectionSeconds = 0.130;
        const float validation_arm_px = std::max(
            16.0f,
            config_.ai_aim.body_lock_near_lock_error_px);
        const auto output_pushes_away = [](float output_axis, float error, bool y_axis) {
            if (output_axis == 0.0f || error == 0.0f) {
                return false;
            }
            return y_axis ? (output_axis * error > 0.0f) : (output_axis * error < 0.0f);
        };
        const auto apply_bounded_wrong_way_correction = [&](
            float current_error,
            float& output_axis,
            bool y_axis) {
            output_axis = bounded_wrong_way_correction(
                output_axis,
                current_error,
                y_axis,
                kCrossErrorDeadzonePx);
        };
        const auto correct_crossed_wrong_way_axis = [&](
            float previous_error,
            float current_error,
            float& output_axis,
            double& correction_until_seconds,
            bool y_axis) {
            const int previous_sign = axis_sign(previous_error, kCrossErrorDeadzonePx);
            const int current_sign = axis_sign(current_error, kCrossErrorDeadzonePx);
            const bool crossed =
                previous_sign != 0 && current_sign != 0 && previous_sign != current_sign;
            if (crossed &&
                std::fabs(current_error) <= validation_arm_px &&
                output_pushes_away(output_axis, current_error, y_axis)) {
                correction_until_seconds = now_seconds + kOutputValidationCorrectionSeconds;
                apply_bounded_wrong_way_correction(current_error, output_axis, y_axis);
            }
        };
        const auto correct_active_wrong_way_axis = [&](
            float current_error,
            float& output_axis,
            double& correction_until_seconds,
            bool y_axis) {
            if (correction_until_seconds <= 0.0) {
                return;
            }
            if (now_seconds > correction_until_seconds) {
                correction_until_seconds = 0.0;
                return;
            }
            if (output_pushes_away(output_axis, current_error, y_axis)) {
                apply_bounded_wrong_way_correction(current_error, output_axis, y_axis);
            }
        };
        const float aim_target_max_age_ms =
            std::max(0.0f, config_.ai_aim.target_max_age_ms);
        const double vision_age_ms =
            vision_state.observed_at_seconds > 0.0 && now_seconds > 0.0
                ? std::max(0.0, now_seconds - vision_state.observed_at_seconds) * 1000.0
                : 0.0;
        const double stale_guard_age_ms =
            aim_target_max_age_ms > 0.0f
                ? std::min(
                    60.0,
                    std::max(45.0, static_cast<double>(aim_target_max_age_ms) * 0.60))
                : 0.0;
        const auto correct_stale_wrong_way_axis = [&](
            float current_error,
            float& output_axis,
            bool y_axis) {
            if (ads_active_ &&
                stale_guard_age_ms > 0.0 &&
                vision_age_ms >= stale_guard_age_ms &&
                output_pushes_away(output_axis, current_error, y_axis)) {
                apply_bounded_wrong_way_correction(current_error, output_axis, y_axis);
            }
        };
        if (has_last_output_validation_error_) {
            correct_crossed_wrong_way_axis(
                last_output_validation_error_x_,
                vision_state.dx,
                output.right_x,
                output_validation_correction_x_until_seconds_,
                false);
            correct_crossed_wrong_way_axis(
                last_output_validation_error_y_,
                vision_state.dy,
                output.right_y,
                output_validation_correction_y_until_seconds_,
                true);
        }
        correct_active_wrong_way_axis(
            vision_state.dx,
            output.right_x,
            output_validation_correction_x_until_seconds_,
            false);
        correct_active_wrong_way_axis(
            vision_state.dy,
            output.right_y,
            output_validation_correction_y_until_seconds_,
            true);
        correct_stale_wrong_way_axis(vision_state.dx, output.right_x, false);
        correct_stale_wrong_way_axis(vision_state.dy, output.right_y, true);
        has_last_output_validation_error_ = true;
        last_output_validation_error_x_ = vision_state.dx;
        last_output_validation_error_y_ = vision_state.dy;
    } else {
        has_last_output_validation_error_ = false;
        last_output_validation_error_x_ = 0.0f;
        last_output_validation_error_y_ = 0.0f;
        output_validation_correction_x_until_seconds_ = 0.0;
        output_validation_correction_y_until_seconds_ = 0.0;
    }
    if (candidate_output_hold_until_seconds_ > 0.0 &&
        now_seconds <= candidate_output_hold_until_seconds_) {
        output.right_x = 0.0f;
        output.right_y = 0.0f;
    } else if (candidate_output_hold_until_seconds_ > 0.0 &&
        now_seconds > candidate_output_hold_until_seconds_) {
        candidate_output_hold_until_seconds_ = 0.0;
    }
}

void NativeGamepadController::apply_body_lock_short_plan(
    GamepadOutputState& output,
    float manual_right_x,
    float manual_right_y,
    bool vertical_plan_allowed,
    const NativeControllerVisionState& vision_state,
    double now_seconds) {
    float lock_dx = 0.0f;
    float lock_dy = 0.0f;
    const float manual_escape_threshold = std::max(
        0.0f,
        std::min(1.0f, config_.ai_aim.body_lock_manual_escape_input_threshold));
    constexpr float kOutputDeadzone = 0.015f;
    constexpr float kCrossErrorDeadzonePx = 1.0f;
    constexpr float kManualCrossBrakeOutputCap = 0.08f;
    constexpr double kManualCrossBrakeSeconds = 0.130;
    const float manual_cross_brake_arm_px = std::max(
        16.0f,
        config_.ai_aim.body_lock_near_lock_error_px);
    const auto manual_pushes_away = [](float manual, float error, bool y_axis) {
        if (manual == 0.0f || error == 0.0f) {
            return false;
        }
        return y_axis ? (manual * error > 0.0f) : (manual * error < 0.0f);
    };
    const auto output_pushes_away = [](float output_axis, float error, bool y_axis) {
        if (output_axis == 0.0f || error == 0.0f) {
            return false;
        }
        return y_axis ? (output_axis * error > 0.0f) : (output_axis * error < 0.0f);
    };
    const auto apply_active_manual_cross_brake = [&](
        float manual_axis,
        float& output_axis,
        double& brake_until_seconds,
        int& brake_manual_sign,
        float current_error,
        bool y_axis) {
        if (brake_until_seconds <= 0.0) {
            return;
        }
        if (now_seconds > brake_until_seconds) {
            brake_until_seconds = 0.0;
            brake_manual_sign = 0;
            return;
        }
        const int manual_sign = axis_sign(manual_axis, kOutputDeadzone);
        if (manual_sign == 0) {
            brake_until_seconds = 0.0;
            brake_manual_sign = 0;
            return;
        }
        if (brake_manual_sign == 0) {
            brake_manual_sign = manual_sign;
        } else if (manual_sign != brake_manual_sign) {
            brake_until_seconds = 0.0;
            brake_manual_sign = 0;
            return;
        }
        if (axis_sign(output_axis, kOutputDeadzone) != brake_manual_sign) {
            return;
        }
        if (std::fabs(output_axis) <= kManualCrossBrakeOutputCap) {
            if (output_pushes_away(output_axis, current_error, y_axis)) {
                output_axis = bounded_wrong_way_correction(
                    output_axis,
                    current_error,
                    y_axis,
                    kCrossErrorDeadzonePx);
            }
            return;
        }
        if (output_pushes_away(output_axis, current_error, y_axis)) {
            output_axis = bounded_wrong_way_correction(
                output_axis,
                current_error,
                y_axis,
                kCrossErrorDeadzonePx);
            return;
        }
        output_axis = std::copysign(kManualCrossBrakeOutputCap, output_axis);
    };
    const auto update_manual_cross_brake = [&](
        float previous_error,
        float current_error,
        float manual_axis,
        float& output_axis,
        double& brake_until_seconds,
        int& brake_manual_sign,
        bool y_axis) {
        const int previous_sign = axis_sign(previous_error, kCrossErrorDeadzonePx);
        const int current_sign = axis_sign(current_error, kCrossErrorDeadzonePx);
        const int manual_sign = axis_sign(manual_axis, kOutputDeadzone);
        const bool crossed =
            previous_sign != 0 && current_sign != 0 && previous_sign != current_sign;
        const bool same_side_moving_away =
            previous_sign != 0 &&
            previous_sign == current_sign &&
            std::fabs(current_error) <= manual_cross_brake_arm_px * 2.0f &&
            std::fabs(current_error) > std::fabs(previous_error) + kCrossErrorDeadzonePx;
        const bool crossed_with_wrong_way_manual =
            crossed &&
            std::fabs(current_error) <= manual_cross_brake_arm_px &&
            std::fabs(manual_axis) >= kOutputDeadzone &&
            manual_pushes_away(manual_axis, current_error, y_axis);
        const bool worsening_with_wrong_way_manual =
            same_side_moving_away &&
            std::fabs(manual_axis) >= manual_escape_threshold &&
            manual_pushes_away(manual_axis, current_error, y_axis);
        const bool large_wrong_way_manual =
            std::fabs(current_error) > manual_cross_brake_arm_px &&
            std::fabs(current_error) <= manual_cross_brake_arm_px * 2.0f &&
            std::fabs(manual_axis) >= manual_escape_threshold &&
            manual_pushes_away(manual_axis, current_error, y_axis);
        if (crossed_with_wrong_way_manual || worsening_with_wrong_way_manual ||
            large_wrong_way_manual) {
            brake_until_seconds = now_seconds + kManualCrossBrakeSeconds;
            brake_manual_sign = manual_sign;
        }
        apply_active_manual_cross_brake(
            manual_axis,
            output_axis,
            brake_until_seconds,
            brake_manual_sign,
            current_error,
            y_axis);
    };

    if (vision_state.has_target && vision_state.aim_authority) {
        if (has_last_aim_error_for_plan_) {
            update_manual_cross_brake(
                last_aim_plan_error_x_,
                vision_state.dx,
                manual_right_x,
                output.right_x,
                body_lock_manual_brake_x_until_seconds_,
                body_lock_manual_brake_x_sign_,
                false);
            update_manual_cross_brake(
                last_aim_plan_error_y_,
                vision_state.dy,
                manual_right_y,
                output.right_y,
                body_lock_manual_brake_y_until_seconds_,
                body_lock_manual_brake_y_sign_,
                true);
        }
        has_last_aim_error_for_plan_ = true;
        last_aim_plan_error_x_ = vision_state.dx;
        last_aim_plan_error_y_ = vision_state.dy;
    } else {
        has_last_aim_error_for_plan_ = false;
        last_aim_plan_error_x_ = 0.0f;
        last_aim_plan_error_y_ = 0.0f;
    }

    const bool body_lock_available =
        ai_aim_.last_mode() == "body_lock" &&
        body_lock_error_for_state(vision_state, &lock_dx, &lock_dy);
    if (!body_lock_available) {
        if (vision_state.has_target && vision_state.aim_authority) {
            apply_active_manual_cross_brake(
                manual_right_x,
                output.right_x,
                body_lock_manual_brake_x_until_seconds_,
                body_lock_manual_brake_x_sign_,
                vision_state.dx,
                false);
            apply_active_manual_cross_brake(
                manual_right_y,
                output.right_y,
                body_lock_manual_brake_y_until_seconds_,
                body_lock_manual_brake_y_sign_,
                vision_state.dy,
                true);
        } else {
            body_lock_manual_brake_x_until_seconds_ = 0.0;
            body_lock_manual_brake_y_until_seconds_ = 0.0;
            body_lock_manual_brake_x_sign_ = 0;
            body_lock_manual_brake_y_sign_ = 0;
        }
        has_last_body_lock_short_plan_x_ = false;
        has_last_body_lock_short_plan_y_ = false;
        has_last_body_lock_error_for_plan_ = false;
        last_body_lock_short_plan_x_ = 0.0f;
        last_body_lock_short_plan_y_ = 0.0f;
        last_body_lock_plan_error_x_ = 0.0f;
        last_body_lock_plan_error_y_ = 0.0f;
        body_lock_short_plan_x_until_seconds_ = 0.0;
        body_lock_short_plan_y_until_seconds_ = 0.0;
        return;
    }

    const float near_lock_px = std::max(1.0f, config_.ai_aim.body_lock_near_lock_error_px);
    const float error_radius = std::hypot(lock_dx, lock_dy);

    if (has_last_body_lock_error_for_plan_) {
        update_manual_cross_brake(
            last_body_lock_plan_error_x_,
            lock_dx,
            manual_right_x,
            output.right_x,
            body_lock_manual_brake_x_until_seconds_,
            body_lock_manual_brake_x_sign_,
            false);
        update_manual_cross_brake(
            last_body_lock_plan_error_y_,
            lock_dy,
            manual_right_y,
            output.right_y,
            body_lock_manual_brake_y_until_seconds_,
            body_lock_manual_brake_y_sign_,
            true);
    }
    if (error_radius > near_lock_px) {
        body_lock_short_plan_x_until_seconds_ = 0.0;
        body_lock_short_plan_y_until_seconds_ = 0.0;
        has_last_body_lock_short_plan_x_ = true;
        has_last_body_lock_short_plan_y_ = true;
        last_body_lock_short_plan_x_ = output.right_x;
        last_body_lock_short_plan_y_ = output.right_y;
        has_last_body_lock_error_for_plan_ = true;
        last_body_lock_plan_error_x_ = lock_dx;
        last_body_lock_plan_error_y_ = lock_dy;
        return;
    }

    constexpr double kShortPlanSeconds = 0.018;
    const float previous_plan_x = last_body_lock_short_plan_x_;
    const float previous_plan_y = last_body_lock_short_plan_y_;
    const bool had_previous_plan =
        has_last_body_lock_short_plan_x_ && has_last_body_lock_short_plan_y_;
    if (std::fabs(manual_right_x) < manual_escape_threshold) {
        const int previous_sign =
            has_last_body_lock_short_plan_x_
                ? axis_sign(last_body_lock_short_plan_x_, kOutputDeadzone)
                : 0;
        const int current_sign = axis_sign(output.right_x, kOutputDeadzone);
        if (previous_sign != 0 && current_sign != 0 && previous_sign != current_sign) {
            body_lock_short_plan_x_until_seconds_ = now_seconds + kShortPlanSeconds;
        }
    } else {
        body_lock_short_plan_x_until_seconds_ = 0.0;
    }

    if (body_lock_short_plan_x_until_seconds_ > 0.0 &&
        now_seconds <= body_lock_short_plan_x_until_seconds_) {
        output.right_x = 0.0f;
    }

    if (vertical_plan_allowed && std::fabs(manual_right_y) < manual_escape_threshold) {
        const int previous_sign =
            has_last_body_lock_short_plan_y_
                ? axis_sign(last_body_lock_short_plan_y_, kOutputDeadzone)
                : 0;
        const int current_sign = axis_sign(output.right_y, kOutputDeadzone);
        if (previous_sign != 0 && current_sign != 0 && previous_sign != current_sign) {
            body_lock_short_plan_y_until_seconds_ = now_seconds + kShortPlanSeconds;
        }
    } else {
        body_lock_short_plan_y_until_seconds_ = 0.0;
    }

    if (body_lock_short_plan_y_until_seconds_ > 0.0 &&
        now_seconds <= body_lock_short_plan_y_until_seconds_) {
        output.right_y = 0.0f;
    }

    if (vertical_plan_allowed &&
        std::fabs(manual_right_x) < manual_escape_threshold &&
        std::fabs(manual_right_y) < manual_escape_threshold &&
        had_previous_plan) {
        const float previous_mag = std::hypot(previous_plan_x, previous_plan_y);
        const float current_mag = std::hypot(output.right_x, output.right_y);
        constexpr float kSmallPlanMagnitude = 0.08f;
        if (previous_mag >= kOutputDeadzone &&
            current_mag >= kOutputDeadzone &&
            previous_mag <= kSmallPlanMagnitude &&
            current_mag <= kSmallPlanMagnitude) {
            const float alignment =
                ((previous_plan_x * output.right_x) + (previous_plan_y * output.right_y)) /
                (previous_mag * current_mag);
            if (alignment < 0.25f) {
                body_lock_short_plan_x_until_seconds_ = now_seconds + kShortPlanSeconds;
                body_lock_short_plan_y_until_seconds_ = now_seconds + kShortPlanSeconds;
                output.right_x = 0.0f;
                output.right_y = 0.0f;
            }
        }
    }

    has_last_body_lock_short_plan_x_ = true;
    has_last_body_lock_short_plan_y_ = true;
    last_body_lock_short_plan_x_ = output.right_x;
    last_body_lock_short_plan_y_ = output.right_y;
    has_last_body_lock_error_for_plan_ = true;
    last_body_lock_plan_error_x_ = lock_dx;
    last_body_lock_plan_error_y_ = lock_dy;
}

void NativeGamepadController::apply_aim_assist_dynamics(
    GamepadOutputState& output,
    float manual_right_x,
    float manual_right_y,
    const PhysicalGamepadState& physical,
    bool auto_fire_active) {
    NativeAimAssistDynamicsInput input;
    input.manual_right_x = manual_right_x;
    input.manual_right_y = manual_right_y;
    input.assisted_right_x = output.right_x;
    input.assisted_right_y = output.right_y;
    input.recoil_active = false;
    input.manual_fire_active = physical.rb || physical.right_trigger > 0.04f;
    input.auto_fire_active = auto_fire_active;
    input.now_seconds = now_seconds();

    const NativeAimAssistDynamicsOutput shaped = aim_assist_dynamics_.apply(input);
    output.right_x = shaped.right_x;
    output.right_y = shaped.right_y;
}

void NativeGamepadController::apply_recoil(
    GamepadOutputState& output,
    const PhysicalGamepadState& physical,
    bool auto_fire_active,
    double now_seconds) {
    NativeRecoilInput input;
    input.fire_active = auto_fire_active || physical.rb || physical.right_trigger > 0.04f;
    input.aiming = is_aiming(physical);
    input.now_seconds = now_seconds;

    const recoil_native::RecoilBoundaryOutput recoil_output = recoil_.compute(input);
    if (!recoil_output.recoil_active) {
        return;
    }
    output.right_x = clamp_unit(output.right_x + recoil_output.recoil_stick.x);
    output.right_y = clamp_unit(output.right_y + recoil_output.recoil_stick.y);
}

void NativeGamepadController::record_target_tracker_output(
    const NativeControllerOutputComponents& components,
    double now_seconds) {
    last_tracker_motion_output_ = GamepadOutputState{};
    last_tracker_motion_output_.right_x = components.final_stick.x;
    last_tracker_motion_output_.right_y = components.final_stick.y;
    last_tracker_motion_output_.rb = components.fire_button;
    if (last_output_at_seconds_ > 0.0 && now_seconds >= last_output_at_seconds_) {
        tracking_native::TrackerControlSample sample;
        sample.apply_time = {now_seconds};
        sample.dt = {now_seconds - last_output_at_seconds_};
        sample.sticks.manual = {components.manual_stick.x, components.manual_stick.y};
        sample.sticks.assist = {components.ai_aim_stick.x, components.ai_aim_stick.y};
        sample.sticks.dynamics = {
            components.dynamic_adjustment_stick.x,
            components.dynamic_adjustment_stick.y};
        sample.sticks.recoil = {components.recoil_stick.x, components.recoil_stick.y};
        sample.sticks.final_output = {components.final_stick.x, components.final_stick.y};
        target_tracker_->push_control_sample(sample);
    }
    last_output_at_seconds_ = now_seconds;
}

void NativeGamepadController::record_stage_trace(
    const std::string& stage_name,
    float before_right_y,
    const GamepadOutputState& output,
    bool before_auto_fire_active,
    bool after_auto_fire_active) {
    NativeControllerStageTrace trace;
    trace.stage_name = stage_name;
    trace.before_right_y = before_right_y;
    trace.after_right_y = output.right_y;
    trace.delta_right_y = output.right_y - before_right_y;
    trace.before_auto_fire_active = before_auto_fire_active;
    trace.after_auto_fire_active = after_auto_fire_active;
    last_pipeline_traces_.push_back(std::move(trace));
}

double NativeGamepadController::now_seconds() const {
    return clock_ ? clock_() : current_seconds();
}

}  // namespace controller_native
