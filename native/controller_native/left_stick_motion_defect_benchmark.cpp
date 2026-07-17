#include "left_stick_motion_defect_benchmark.h"

#include "native_gamepad_controller.h"
#include "runtime_config.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <ostream>
#include <set>
#include <vector>

namespace controller_native::left_stick_defect {
namespace {

constexpr int kControllerHz = 1000;
constexpr double kDtSeconds = 1.0 / static_cast<double>(kControllerHz);
constexpr double kPi = 3.14159265358979323846;
constexpr double kBodyHeightPx = 160.0;
constexpr double kReticleSpeedPxPerSecond = 1500.0;
constexpr int kTicksPerScenario = 3600;
constexpr int kVisionDelayTicks = 30;
constexpr int kEvaluationStartTick = 700;
// Keep human input transitions deliberately off the 100 Hz vision cadence.
// This exercises the controller's inter-frame intent path instead of hiding
// every transition inside a fresh observation.
constexpr int kOnsetTick = 806;
constexpr int kReversalTick = 1806;
constexpr int kReleaseTick = 2806;
constexpr int kChainStableStartTick = 800;
constexpr int kChainUnavailableStartTick = 2000;
constexpr int kChainSelectedGapStartTick = 2200;
constexpr int kChainShortGapEndTick = 2280;
constexpr int kChainLongLossStartTick = 2600;
constexpr int kChainReacquireTick = 2900;
constexpr int kChainRecoveryTick = 3300;
constexpr int kChainTicks = 4300;
constexpr float kDriftRightX = -0.00393677f;
constexpr float kDriftManualThreshold = 0.02f;
constexpr float kDriftOnlyAssistThreshold = 0.005f;

enum class TargetRelation {
    Stationary,
    SameDirection,
    OppositeDirection,
};

struct ClosedLoopFixture {
    const char* name;
    const char* mobility;
    TargetRelation relation;
    double player_top_speed_body_s;
    double player_time_constant_s;
    bool manual_correction;
};

constexpr std::array<ClosedLoopFixture, 5> kClosedLoopFixtures = {{
    {"stationary_target_slow_ads", "slow", TargetRelation::Stationary, 0.78, 0.18, false},
    {"stationary_target_fast_ads", "fast", TargetRelation::Stationary, 1.45, 0.10, false},
    {"same_direction_target_fast_ads", "fast", TargetRelation::SameDirection, 1.45, 0.10, false},
    {"opposite_direction_target_fast_ads", "fast", TargetRelation::OppositeDirection, 1.45, 0.10, false},
    {"stationary_target_fast_ads_manual_correction", "fast", TargetRelation::Stationary, 1.45, 0.10, true},
}};

struct PendingVision {
    int delivery_tick = 0;
    double capture_time_seconds = 0.0;
    double error_x_px = 0.0;
    double relative_velocity_x_px_per_sec = 0.0;
};

float left_profile_for_tick(int tick) {
    if (tick >= kOnsetTick && tick < kReversalTick) {
        return 0.80f;
    }
    if (tick >= kReversalTick && tick < kReleaseTick) {
        return -0.80f;
    }
    return 0.0f;
}

bool is_phase_event_sample(int tick) {
    for (const int boundary : {kOnsetTick, kReversalTick, kReleaseTick}) {
        if (std::abs(tick - boundary) <= 2) {
            return true;
        }
    }
    return false;
}

NativeControllerVisionState intent_probe_vision(
    double now,
    double error_x,
    double relative_velocity_x_px_per_sec = 0.0,
    bool has_relative_velocity = false) {
    NativeControllerVisionState state;
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = false;
    state.target_tier = "observed_strong";
    state.screen_center_x = 320.0f;
    state.screen_center_y = 256.0f;
    state.target_x = static_cast<float>(320.0 + error_x);
    state.target_y = 256.0f;
    state.dx = static_cast<float>(error_x);
    state.dy = 0.0f;
    state.has_body_box = true;
    state.body_x1 = state.target_x - 42.0f;
    state.body_x2 = state.target_x + 42.0f;
    state.body_y1 = 176.0f;
    state.body_y2 = 336.0f;
    state.observed_at_seconds = now;
    state.has_camera_attributed_velocity = has_relative_velocity;
    state.camera_attributed_velocity_x_px_per_sec =
        static_cast<float>(relative_velocity_x_px_per_sec);
    return state;
}

PhysicalGamepadState intent_probe_input(float left_x, float right_x);

const char* target_relation_name(TargetRelation relation) {
    switch (relation) {
        case TargetRelation::Stationary:
            return "stationary";
        case TargetRelation::SameDirection:
            return "same_direction";
        case TargetRelation::OppositeDirection:
            return "opposite_direction";
    }
    return "unknown";
}

const char* phase_name(int tick) {
    if (tick < kOnsetTick) {
        return "settle";
    }
    if (tick < kReversalTick) {
        return "strafe_right";
    }
    if (tick < kReleaseTick) {
        return "strafe_left";
    }
    return "release";
}

const char* production_chain_phase_name(int tick) {
    if (tick < kChainStableStartTick) {
        return "acquire_settle";
    }
    if (tick < kChainUnavailableStartTick) {
        return "stable_bodylock_strafe";
    }
    if (tick < kChainUnavailableStartTick + 40) {
        return "target_present_bodylock_unavailable";
    }
    if (tick < kChainSelectedGapStartTick) {
        return "geometry_resumed";
    }
    if (tick < kChainShortGapEndTick) {
        return "short_same_track_gap";
    }
    if (tick < kChainLongLossStartTick) {
        return "post_short_gap_tracking";
    }
    if (tick < kChainReacquireTick) {
        return "long_identity_loss";
    }
    if (tick < kChainRecoveryTick) {
        return "reacquire_ads_snap";
    }
    return "recovered_bodylock";
}

bool is_production_chain_boundary(int tick) {
    return tick == 0 || tick == kChainStableStartTick ||
        tick == kChainUnavailableStartTick ||
        tick == kChainSelectedGapStartTick ||
        tick == kChainShortGapEndTick || tick == kChainLongLossStartTick ||
        tick == kChainReacquireTick || tick == kChainRecoveryTick ||
        tick == kChainTicks - 1;
}

double production_chain_error_px(int tick) {
    if (tick < 400) {
        return 44.0 - (40.0 * (static_cast<double>(tick) / 399.0));
    }
    if (tick < kChainStableStartTick) {
        return 4.0;
    }
    if (tick < kChainUnavailableStartTick) {
        const double progress = static_cast<double>(tick - kChainStableStartTick) / 1200.0;
        return 22.0 + (8.0 * std::sin(progress * kPi * 2.0));
    }
    if (tick < kChainSelectedGapStartTick) {
        return 37.0;
    }
    if (tick < kChainShortGapEndTick) {
        return 37.0;
    }
    if (tick < kChainLongLossStartTick) {
        const double progress = static_cast<double>(tick - kChainShortGapEndTick) / 320.0;
        return 37.0 - (12.0 * progress);
    }
    if (tick < kChainReacquireTick) {
        return 25.0;
    }
    if (tick < kChainRecoveryTick) {
        const double progress = static_cast<double>(tick - kChainReacquireTick) / 400.0;
        return 51.0 - (45.0 * progress);
    }
    const double progress = static_cast<double>(tick - kChainRecoveryTick) / 1000.0;
    return 6.0 + (10.0 * std::sin(progress * kPi * 2.0));
}

common_native::Box2f production_chain_body_box(
    float target_x,
    bool bodylock_geometry_available) {
    const float width = bodylock_geometry_available ? 84.0f : 24.0f;
    const float height = bodylock_geometry_available ? 160.0f : 100.0f;
    return {
        target_x - (width * 0.5f),
        256.0f - (height * 0.4f),
        width,
        height,
    };
}

pipeline_contract::VisionCandidateSnapshot production_chain_candidate(
    std::uint64_t observation_id,
    float target_x,
    float confidence,
    bool bodylock_geometry_available) {
    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = observation_id;
    candidate.valid = true;
    candidate.body_box_px = production_chain_body_box(
        target_x,
        bodylock_geometry_available);
    candidate.aim_point_px = {target_x, 256.0f};
    candidate.has_aim_point = true;
    candidate.confidence = confidence;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    return candidate;
}

tracking_native::TrackerDetection production_chain_detection(
    std::uint64_t observation_id,
    float target_x,
    float confidence,
    bool bodylock_geometry_available) {
    tracking_native::TrackerDetection detection;
    detection.id = observation_id;
    detection.body_box_px = production_chain_body_box(
        target_x,
        bodylock_geometry_available);
    detection.aim_point_px = {target_x, 256.0f};
    detection.has_aim_point = true;
    detection.confidence = confidence;
    detection.target_tier = "observed_strong";
    return detection;
}

ControllerVisionSnapshot production_chain_snapshot(
    std::uint64_t frame_id,
    double capture_time_seconds,
    double error_x_px,
    bool include_selected_target,
    bool bodylock_geometry_available) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = frame_id;
    snapshot.capture_time_seconds = capture_time_seconds;
    snapshot.ready_time_seconds = capture_time_seconds;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.observed_at_seconds = capture_time_seconds;
    snapshot.user_intent.valid = true;
    snapshot.user_intent.aiming = true;
    snapshot.user_intent.timestamp = {capture_time_seconds};

    const std::uint64_t primary_observation_id = (frame_id << 32u) | 1u;
    const std::uint64_t distractor_observation_id = (frame_id << 32u) | 2u;
    const float target_x = 320.0f + static_cast<float>(error_x_px);

    if (include_selected_target) {
        snapshot.selected_observation_id = primary_observation_id;
        snapshot.state.has_target = true;
        snapshot.state.aim_authority = true;
        snapshot.state.fire_authority = false;
        snapshot.state.target_tier = "observed_strong";
        snapshot.state.target_x = target_x;
        snapshot.state.target_y = 256.0f;
        snapshot.state.dx = static_cast<float>(error_x_px);
        snapshot.state.dy = 0.0f;
        snapshot.state.has_body_box = true;
        const common_native::Box2f body_box = production_chain_body_box(
            target_x,
            bodylock_geometry_available);
        snapshot.state.body_x1 = body_box.x;
        snapshot.state.body_y1 = body_box.y;
        snapshot.state.body_x2 = body_box.x + body_box.w;
        snapshot.state.body_y2 = body_box.y + body_box.h;
        snapshot.candidates.push_back(production_chain_candidate(
            primary_observation_id,
            target_x,
            0.82f,
            bodylock_geometry_available));
        snapshot.tracker_detections.push_back(production_chain_detection(
            primary_observation_id,
            target_x,
            0.82f,
            bodylock_geometry_available));
    } else {
        snapshot.state.has_target = false;
        snapshot.state.aim_authority = false;
        snapshot.state.fire_authority = false;
        snapshot.state.target_tier = "none";
    }

    snapshot.candidates.push_back(production_chain_candidate(
        distractor_observation_id,
        535.0f,
        0.68f,
        true));
    snapshot.tracker_detections.push_back(production_chain_detection(
        distractor_observation_id,
        535.0f,
        0.68f,
        true));
    if (!include_selected_target) {
        const std::uint64_t second_distractor_id = (frame_id << 32u) | 3u;
        snapshot.candidates.push_back(production_chain_candidate(
            second_distractor_id,
            105.0f,
            0.64f,
            true));
        snapshot.tracker_detections.push_back(production_chain_detection(
            second_distractor_id,
            105.0f,
            0.64f,
            true));
    }
    return snapshot;
}

double clamp_unit(double value) {
    return std::max(-1.0, std::min(1.0, value));
}

double shaped_left(float left_x) {
    constexpr double kDeadzone = 0.08;
    const double magnitude = std::fabs(static_cast<double>(left_x));
    if (magnitude <= kDeadzone) {
        return 0.0;
    }
    const double normalized = std::min(1.0, (magnitude - kDeadzone) / (1.0 - kDeadzone));
    const double shaped = std::pow(normalized, 1.25);
    return left_x < 0.0f ? -shaped : shaped;
}

double percentile95(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

double event_peak_error(
    const std::vector<double>& errors,
    int event_tick,
    int window_ticks = 400) {
    double peak = 0.0;
    const int end_tick = std::min(
        static_cast<int>(errors.size()),
        event_tick + window_ticks);
    for (int tick = event_tick; tick < end_tick; ++tick) {
        peak = std::max(peak, errors[static_cast<std::size_t>(tick)]);
    }
    return peak;
}

double event_response_latency_ms(
    const std::vector<double>& outputs,
    const std::vector<double>& oracle_outputs,
    int event_tick) {
    if (event_tick <= 0 || event_tick + 100 >= static_cast<int>(outputs.size())) {
        return -1.0;
    }
    const double baseline = outputs[static_cast<std::size_t>(event_tick - 1)];
    const double desired_change =
        oracle_outputs[static_cast<std::size_t>(event_tick + 100)] - baseline;
    if (std::fabs(desired_change) < 0.02) {
        return -1.0;
    }
    const double direction = desired_change < 0.0 ? -1.0 : 1.0;
    const double threshold = std::min(0.03, std::fabs(desired_change) * 0.50);
    const int end_tick = std::min(
        static_cast<int>(outputs.size()),
        event_tick + 310);
    for (int tick = event_tick; tick < end_tick; ++tick) {
        const double moved =
            (outputs[static_cast<std::size_t>(tick)] - baseline) * direction;
        if (moved >= threshold) {
            return static_cast<double>(tick - event_tick) * kDtSeconds * 1000.0;
        }
    }
    return static_cast<double>(end_tick - event_tick) * kDtSeconds * 1000.0;
}

bool should_record_event(int tick) {
    for (const int boundary : {kOnsetTick, kReversalTick, kReleaseTick}) {
        if (tick >= boundary - 50 && tick <= boundary + 150 && tick % 10 == 0) {
            return true;
        }
    }
    return tick % 600 == 0 || tick == kTicksPerScenario - 1;
}

ScenarioMetrics run_closed_loop_fixture(
    const ClosedLoopFixture& fixture,
    int vision_hz,
    bool feed_left_intent,
    FrequencyRunMetrics* frequency_metrics = nullptr) {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = true;
    config.ai_aim.ads_snap_window_ms = 0;
    if (!feed_left_intent) {
        // A/B baseline: the pre-fix controller had neither the relative-motion
        // lead nor the left-intent mobility predictor.
        config.ai_aim.body_lock_lead_seconds = 0.0f;
        config.ai_aim.body_lock_lateral_motion_lead_seconds = 0.0f;
    }

    double now = 1.0;
    NativeGamepadController controller(config, [&now] { return now; });
    std::deque<PendingVision> pending_vision;

    ScenarioMetrics metrics;
    metrics.name = fixture.name;
    metrics.mobility = fixture.mobility;
    metrics.target_relation = target_relation_name(fixture.relation);
    metrics.body_height_px = kBodyHeightPx;

    double true_error_px = 44.0;
    double last_observed_error_px = true_error_px;
    double last_observed_capture_time_seconds = 1.0;
    double player_velocity_body_s = 0.0;
    double target_velocity_body_s = 0.0;
    double previous_final_output = 0.0;
    bool has_previous_output = false;
    double previous_ai_output = 0.0;
    bool has_previous_ai_output = false;
    float previous_left_x = 0.0f;
    int vision_phase = kControllerHz;
    std::uint64_t last_consumed_sequence = 0;

    std::vector<double> abs_errors;
    std::vector<double> evaluation_errors;
    std::vector<double> final_outputs;
    std::vector<double> oracle_outputs;
    abs_errors.reserve(kTicksPerScenario);
    evaluation_errors.reserve(kTicksPerScenario - kEvaluationStartTick);
    final_outputs.reserve(kTicksPerScenario);
    oracle_outputs.reserve(kTicksPerScenario);

    for (int tick = 0; tick < kTicksPerScenario; ++tick) {
        now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const float left_x = left_profile_for_tick(tick);
        const double shaped_input = shaped_left(left_x);
        const double desired_player_velocity =
            fixture.player_top_speed_body_s * shaped_input;
        const double player_alpha = 1.0 - std::exp(
            -kDtSeconds / fixture.player_time_constant_s);
        player_velocity_body_s +=
            player_alpha * (desired_player_velocity - player_velocity_body_s);

        double desired_target_velocity = 0.0;
        if (fixture.relation == TargetRelation::SameDirection) {
            desired_target_velocity =
                fixture.player_top_speed_body_s * shaped_input * 0.58;
        } else if (fixture.relation == TargetRelation::OppositeDirection) {
            desired_target_velocity =
                -fixture.player_top_speed_body_s * shaped_input * 0.58;
        }
        constexpr double kTargetTimeConstantSeconds = 0.14;
        const double target_alpha =
            1.0 - std::exp(-kDtSeconds / kTargetTimeConstantSeconds);
        target_velocity_body_s +=
            target_alpha * (desired_target_velocity - target_velocity_body_s);

        const double relative_velocity_px_s =
            (target_velocity_body_s - player_velocity_body_s) * kBodyHeightPx;
        true_error_px += relative_velocity_px_s * kDtSeconds;

        if (vision_phase >= kControllerHz) {
            pending_vision.push_back(PendingVision{
                tick + kVisionDelayTicks,
                now,
                true_error_px,
                relative_velocity_px_s,
            });
            vision_phase -= kControllerHz;
        }
        vision_phase += vision_hz;
        while (!pending_vision.empty() &&
               pending_vision.front().delivery_tick <= tick) {
            const PendingVision vision = pending_vision.front();
            pending_vision.pop_front();
            last_observed_error_px = vision.error_x_px;
            last_observed_capture_time_seconds = vision.capture_time_seconds;
            controller.submit_vision_state(intent_probe_vision(
                vision.capture_time_seconds,
                vision.error_x_px,
                vision.relative_velocity_x_px_per_sec,
                true));
            if (frequency_metrics != nullptr) {
                ++frequency_metrics->delivered_vision_sequences;
            }
        }

        const double oracle_output = clamp_unit(
            (relative_velocity_px_s + (true_error_px * 7.0)) /
            kReticleSpeedPxPerSecond);
        const float manual_right = fixture.manual_correction
            ? static_cast<float>(std::clamp(oracle_output * 0.72, -0.24, 0.24))
            : 0.0f;
        const GamepadOutputState output = controller.build_output(
            intent_probe_input(feed_left_intent ? left_x : 0.0f, manual_right));
        const NativeControllerOutputComponents& components =
            controller.last_output_components();
        const double ai_output = components.shaped_assist_stick.x;
        const double final_output = output.right_x;
        const NativeControllerVisionState& frame_vision =
            controller.last_frame_vision_state();
        if (components.axis_intent_intervention.x > 0.5f) {
            ++metrics.axis_intent_intervention_frames;
        }
        metrics.minimum_manual_retention = std::min(
            metrics.minimum_manual_retention,
            static_cast<double>(components.axis_manual_retention.x));
        if (frequency_metrics != nullptr) {
            const double left_delta = std::fabs(static_cast<double>(
                left_x - previous_left_x));
            if (left_delta > 1.0e-6 && !frame_vision.fresh_observation) {
                ++frequency_metrics->interframe_left_transition_frames;
                frequency_metrics->max_interframe_left_delta = std::max(
                    frequency_metrics->max_interframe_left_delta,
                    left_delta);
            }
            const RelativeMotionEstimate estimate =
                controller.body_lock_relative_motion_estimate();
            frequency_metrics->max_relative_lead_px = std::max(
                frequency_metrics->max_relative_lead_px,
                std::fabs(static_cast<double>(estimate.lead_x_px)));
            frequency_metrics->max_strafe_gain = std::max(
                frequency_metrics->max_strafe_gain,
                std::fabs(static_cast<double>(estimate.strafe_gain)));
            if (estimate.state == RelativeMotionState::Warm) {
                ++frequency_metrics->warm_relative_motion_frames;
            } else if (estimate.state == RelativeMotionState::Rejected) {
                ++frequency_metrics->rejected_relative_motion_frames;
            }
        }
        if (frequency_metrics != nullptr && frame_vision.fresh_observation &&
            frame_vision.vision_sequence != 0 &&
            frame_vision.vision_sequence != last_consumed_sequence) {
            last_consumed_sequence = frame_vision.vision_sequence;
            ++frequency_metrics->fresh_sequences_consumed;
        }
        if (frequency_metrics != nullptr && has_previous_ai_output) {
            const double ai_delta = std::fabs(ai_output - previous_ai_output);
            frequency_metrics->max_lifecycle_ai_delta = std::max(
                frequency_metrics->max_lifecycle_ai_delta,
                ai_delta);
            if (ai_output * previous_ai_output < 0.0 && ai_delta > 0.07) {
                ++frequency_metrics->large_sign_flip_count;
            }
        }
        previous_ai_output = ai_output;
        has_previous_ai_output = true;
        previous_left_x = left_x;

        const bool evaluation_tick = tick >= kEvaluationStartTick;
        if (evaluation_tick && std::fabs(ai_output) >= 0.28) {
            ++metrics.high_ai_output_frames;
        }
        if (evaluation_tick && std::fabs(manual_right) >= 0.05 &&
            std::fabs(ai_output) >= 0.05 &&
            static_cast<double>(manual_right) * ai_output < 0.0) {
            ++metrics.ai_opposes_manual_frames;
        }
        if (evaluation_tick && std::fabs(oracle_output) >= 0.05 &&
            std::fabs(final_output) >= 0.05 &&
            oracle_output * final_output < 0.0) {
            ++metrics.final_opposes_oracle_frames;
        }
        if (has_previous_output && evaluation_tick) {
            const double output_delta = std::fabs(final_output - previous_final_output);
            metrics.max_final_output_delta = std::max(
                metrics.max_final_output_delta,
                output_delta);
            if (output_delta >= 0.20) {
                ++metrics.output_spike_frames;
            }
        }
        previous_final_output = final_output;
        has_previous_output = true;

        if (evaluation_tick) {
            metrics.max_ai_output = std::max(metrics.max_ai_output, std::fabs(ai_output));
            metrics.max_final_output = std::max(
                metrics.max_final_output,
                std::fabs(final_output));
        }

        true_error_px -=
            final_output * kReticleSpeedPxPerSecond * kDtSeconds;
        const double abs_error = std::fabs(true_error_px);
        abs_errors.push_back(abs_error);
        if (evaluation_tick) {
            evaluation_errors.push_back(abs_error);
        }
        final_outputs.push_back(final_output);
        oracle_outputs.push_back(oracle_output);
        if (evaluation_tick && abs_error <= 8.0) {
            ++metrics.settled_frames;
        }

        if (should_record_event(tick)) {
            PhaseEvent event;
            event.phase = phase_name(tick);
            event.tick = tick;
            event.time_seconds = now;
            event.left_x = feed_left_intent ? left_x : 0.0f;
            event.manual_right_x = manual_right;
            event.ai_right_x = static_cast<float>(ai_output);
            event.final_right_x = static_cast<float>(final_output);
            event.oracle_right_x = static_cast<float>(oracle_output);
            event.observed_error_px = last_observed_error_px;
            event.true_error_px = true_error_px;
            event.vision_age_ms =
                (now - last_observed_capture_time_seconds) * 1000.0;
            event.player_velocity_body_s = player_velocity_body_s;
            event.target_velocity_body_s = target_velocity_body_s;
            event.aim_mode = controller.last_ai_aim_mode();
            metrics.events.push_back(event);
        }
    }

    metrics.behavior_populated = true;
    metrics.mean_abs_error_px = evaluation_errors.empty()
        ? 0.0
        : std::accumulate(evaluation_errors.begin(), evaluation_errors.end(), 0.0) /
            static_cast<double>(evaluation_errors.size());
    metrics.p95_abs_error_px = percentile95(evaluation_errors);
    metrics.max_abs_error_px = evaluation_errors.empty()
        ? 0.0
        : *std::max_element(evaluation_errors.begin(), evaluation_errors.end());
    metrics.final_abs_error_px = evaluation_errors.empty()
        ? 0.0
        : evaluation_errors.back();
    metrics.onset_response_latency_ms =
        event_response_latency_ms(final_outputs, oracle_outputs, kOnsetTick);
    metrics.reversal_response_latency_ms =
        event_response_latency_ms(final_outputs, oracle_outputs, kReversalTick);
    metrics.release_response_latency_ms =
        event_response_latency_ms(final_outputs, oracle_outputs, kReleaseTick);
    metrics.onset_peak_error_px = event_peak_error(abs_errors, kOnsetTick);
    metrics.reversal_peak_error_px = event_peak_error(abs_errors, kReversalTick);
    metrics.release_peak_error_px = event_peak_error(abs_errors, kReleaseTick);

    if (metrics.onset_response_latency_ms > 20.0) {
        metrics.defect_reasons.push_back("onset_response_lag");
    }
    if (metrics.reversal_response_latency_ms > 20.0) {
        metrics.defect_reasons.push_back("reversal_response_lag");
    }
    if (metrics.release_response_latency_ms > 20.0) {
        metrics.defect_reasons.push_back("release_response_lag");
    }
    if (metrics.ai_opposes_manual_frames > 0) {
        metrics.defect_reasons.push_back("ai_opposes_manual_correction");
    }
    if (metrics.final_opposes_oracle_frames > 0) {
        metrics.defect_reasons.push_back("final_output_opposes_oracle");
    }
    if (metrics.output_spike_frames > 0) {
        metrics.defect_reasons.push_back("post_acquisition_output_spike");
    }
    if (metrics.max_abs_error_px > 55.0) {
        metrics.defect_reasons.push_back("excessive_tracking_error");
    }
    metrics.desired_gate_pass = metrics.defect_reasons.empty();
    metrics.defect_reproduced = !metrics.desired_gate_pass;
    return metrics;
}

PhysicalGamepadState intent_probe_input(float left_x, float right_x) {
    PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.left_x = left_x;
    state.right_x = right_x;
    return state;
}

IntentInvarianceMetrics run_intent_invariance_probe() {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    config.ai_aim.ads_snap_window_ms = 0;

    double now = 1.0;
    NativeGamepadController active_left(config, [&now] { return now; });
    NativeGamepadController neutral_left(config, [&now] { return now; });
    IntentInvarianceMetrics metrics;

    int vision_phase = kControllerHz;
    for (int tick = 0; tick < kTicksPerScenario; ++tick) {
        now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        constexpr int kWarmupStartTick = 600;
        constexpr int kIntentSplitTick = 800;
        const bool mobility_warmup =
            tick >= kWarmupStartTick && tick < kIntentSplitTick;
        const double error_x = tick < kWarmupStartTick
            ? 38.0
            : (tick < kIntentSplitTick
                ? 38.0 - (0.30 * static_cast<double>(tick - kWarmupStartTick + 1))
                : -22.0);
        const bool vision_tick = vision_phase >= kControllerHz;
        if (vision_tick) {
            vision_phase -= kControllerHz;
        }
        vision_phase += 100;
        if (vision_tick && tick != kIntentSplitTick) {
            const NativeControllerVisionState vision = intent_probe_vision(
                now,
                error_x,
                mobility_warmup ? -240.0 : 0.0,
                mobility_warmup);
            active_left.submit_vision_state(vision);
            neutral_left.submit_vision_state(vision);
        }

        const float manual_right = static_cast<float>(
            std::clamp(error_x / 420.0, -0.16, 0.16));
        const float left_x = mobility_warmup ? 0.80f : left_profile_for_tick(tick);
        const float neutral_left_x = mobility_warmup ? 0.80f : 0.0f;
        const GamepadOutputState active_output =
            active_left.build_output(intent_probe_input(left_x, manual_right));
        const GamepadOutputState neutral_output =
            neutral_left.build_output(intent_probe_input(neutral_left_x, manual_right));
        const NativeControllerOutputComponents& active_components =
            active_left.last_output_components();
        const NativeControllerOutputComponents& neutral_components =
            neutral_left.last_output_components();

        metrics.max_ai_trace_delta = std::max(
            metrics.max_ai_trace_delta,
            std::fabs(
                static_cast<double>(active_components.ai_aim_stick.x) -
                static_cast<double>(neutral_components.ai_aim_stick.x)));
        metrics.max_final_right_trace_delta = std::max(
            metrics.max_final_right_trace_delta,
            std::fabs(
                static_cast<double>(active_output.right_x) -
                static_cast<double>(neutral_output.right_x)));
        metrics.max_left_passthrough_error = std::max(
            metrics.max_left_passthrough_error,
            std::fabs(
                static_cast<double>(active_output.left_x) -
                static_cast<double>(left_x)));
        if (is_phase_event_sample(tick)) {
            ++metrics.phase_event_samples;
        }
    }

    constexpr double kIntentSensitivityEpsilon = 1.0e-4;
    metrics.left_intent_ignored =
        metrics.phase_event_samples > 0 &&
        metrics.max_ai_trace_delta <= kIntentSensitivityEpsilon &&
        metrics.max_final_right_trace_delta <= kIntentSensitivityEpsilon;
    metrics.desired_gate_pass = !metrics.left_intent_ignored;
    return metrics;
}

ProductionChainMetrics run_production_chain_probe() {
    GamepadRuntimeConfig config;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = true;

    double now = 1.0;
    NativeGamepadController controller(config, [&now] { return now; });
    ProductionChainMetrics metrics;
    metrics.total_frames = kChainTicks;

    std::string previous_mode;
    std::string previous_lifecycle;
    std::string previous_limit_reason;
    std::uint64_t previous_track_id = 0;
    int current_drift_only_frames = 0;
    int maximum_drift_only_frames = 0;
    int first_reacquired_tick = -1;
    int vision_phase = kControllerHz;
    std::uint64_t frame_id = 0;
    int short_gap_coast_frames = 0;
    int blind_candidate_follow_frames = 0;
    bool long_loss_released = false;
    bool reacquire_useful = false;
    double reacquire_max_delta = 0.0;
    double previous_final_x = 0.0;
    bool has_previous_final = false;

    for (int tick = 0; tick < kChainTicks; ++tick) {
        now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const bool short_gap =
            tick >= kChainSelectedGapStartTick && tick < kChainShortGapEndTick;
        const bool long_loss =
            tick >= kChainLongLossStartTick && tick < kChainReacquireTick;
        const bool selected_gap = short_gap || long_loss;
        const bool bodylock_geometry_available =
            tick < kChainUnavailableStartTick ||
            tick >= kChainUnavailableStartTick + 40;
        const bool detector_candidates_present = true;
        const double scheduled_error_px = production_chain_error_px(tick);

        if (vision_phase >= kControllerHz) {
            vision_phase -= kControllerHz;
            controller.submit_vision_snapshot(production_chain_snapshot(
                ++frame_id,
                now,
                scheduled_error_px,
                !selected_gap,
                bodylock_geometry_available));
        }
        vision_phase += 100;

        PhysicalGamepadState physical;
        physical.connected = true;
        physical.left_trigger = 1.0f;
        physical.left_x = tick >= kChainStableStartTick ? 0.80f : 0.0f;
        physical.right_x = kDriftRightX;
        const GamepadOutputState output = controller.build_output(physical);
        const NativeControllerOutputComponents& components =
            controller.last_output_components();
        const NativeControllerVisionState& vision_state =
            controller.last_frame_vision_state();
        const std::string& mode = components.aim_mode;

        if (mode == "body_lock") {
            ++metrics.body_lock_frames;
        } else if (mode == "ads_snap") {
            ++metrics.ads_snap_frames;
        } else if (mode == "manual") {
            ++metrics.manual_frames;
        }
        if (!previous_mode.empty() && mode != previous_mode) {
            ++metrics.mode_transitions;
        }
        metrics.max_abs_manual_right = std::max(
            metrics.max_abs_manual_right,
            std::fabs(static_cast<double>(physical.right_x)));
        if (std::fabs(physical.right_x) > kDriftManualThreshold) {
            ++metrics.drift_manual_correction_frames;
        }
        if (selected_gap && detector_candidates_present) {
            ++metrics.detector_candidate_gap_frames;
            if (!vision_state.has_target) {
                ++metrics.production_target_missing_frames;
            }
        }
        if (tick >= kChainUnavailableStartTick &&
            tick < kChainSelectedGapStartTick && vision_state.has_target &&
            mode != "body_lock") {
            ++metrics.target_present_bodylock_unavailable_frames;
        }

        const double delivered_assist =
            static_cast<double>(output.right_x) - physical.right_x;
        const bool bounded_short_gap_release =
            !has_previous_final ||
            std::fabs(static_cast<double>(output.right_x) - previous_final_x) <= 0.07;
        const bool existing_assist_release =
            components.bodylock_lifecycle == "coast" ||
            components.assist_limit_reason == "ads_identity_hold_release";
        if (short_gap && existing_assist_release && bounded_short_gap_release) {
            ++short_gap_coast_frames;
        }
        const double observation_age_ms = vision_state.observed_at_seconds > 0.0
            ? (now - vision_state.observed_at_seconds) * 1000.0
            : 1.0e9;
        const double hold_budget_ms = std::max(
            80.0f,
            std::max(config.ai_aim.target_max_age_ms,
                     config.ai_aim.target_projection_max_age_ms));
        if (selected_gap && vision_state.has_target &&
            observation_age_ms > hold_budget_ms + 1.0) {
            ++blind_candidate_follow_frames;
        }
        if (tick >= kChainLongLossStartTick + 225 && tick < kChainReacquireTick &&
            std::fabs(delivered_assist) <= 0.001) {
            long_loss_released = true;
        }
        if (tick >= kChainReacquireTick && tick < kChainReacquireTick + 60) {
            if (has_previous_final) {
                reacquire_max_delta = std::max(
                    reacquire_max_delta,
                    std::fabs(static_cast<double>(output.right_x) - previous_final_x));
            }
            if (std::fabs(delivered_assist) >= 0.02) {
                reacquire_useful = true;
                if (metrics.reacquire_useful_latency_ms < 0.0) {
                    metrics.reacquire_useful_latency_ms =
                        static_cast<double>(tick - kChainReacquireTick) *
                        kDtSeconds * 1000.0;
                }
            }
        }
        const bool strafe_active = tick >= kChainStableStartTick;
        const bool drift_only =
            strafe_active && detector_candidates_present &&
            std::fabs(physical.right_x) <= kDriftManualThreshold &&
            std::fabs(delivered_assist) <= kDriftOnlyAssistThreshold;
        if (drift_only) {
            ++metrics.drift_only_final_frames;
            ++current_drift_only_frames;
            maximum_drift_only_frames = std::max(
                maximum_drift_only_frames,
                current_drift_only_frames);
        } else {
            current_drift_only_frames = 0;
        }
        if (std::fabs(components.requested_assist_stick.x) >= 0.05f &&
            std::fabs(delivered_assist) <= kDriftOnlyAssistThreshold) {
            ++metrics.requested_suppressed_frames;
        }

        if (tick == kChainSelectedGapStartTick - 1) {
            metrics.pre_loss_error_px = vision_state.dx;
        }
        if (tick >= kChainReacquireTick && first_reacquired_tick < 0 &&
            vision_state.has_target) {
            first_reacquired_tick = tick;
            metrics.post_reacquire_error_px = vision_state.dx;
            metrics.reacquire_latency_ms =
                static_cast<double>(tick - kChainReacquireTick) *
                kDtSeconds * 1000.0;
        }

        const std::uint64_t track_id = vision_state.selected_track_id;
        if (track_id != 0 && previous_track_id != 0 && track_id != previous_track_id) {
            ++metrics.selected_track_changes;
        }
        const bool state_changed =
            mode != previous_mode ||
            components.bodylock_lifecycle != previous_lifecycle ||
            components.assist_limit_reason != previous_limit_reason ||
            track_id != previous_track_id;
        if (is_production_chain_boundary(tick) || state_changed) {
            ProductionChainEvent event;
            event.phase = production_chain_phase_name(tick);
            event.tick = tick;
            event.detector_candidates_present = detector_candidates_present;
            event.production_target_present = vision_state.has_target;
            event.selected_track_id = track_id;
            event.manual_right_x = physical.right_x;
            event.requested_ai_x = components.requested_assist_stick.x;
            event.final_right_x = output.right_x;
            event.target_error_px = vision_state.dx;
            event.aim_mode = mode;
            event.lifecycle = components.bodylock_lifecycle;
            event.limit_reason = components.assist_limit_reason;
            metrics.events.push_back(std::move(event));
        }

        previous_mode = mode;
        previous_lifecycle = components.bodylock_lifecycle;
        previous_limit_reason = components.assist_limit_reason;
        if (track_id != 0) {
            previous_track_id = track_id;
        }
        previous_final_x = output.right_x;
        has_previous_final = true;
    }

    metrics.max_continuous_drift_only_ms =
        static_cast<double>(maximum_drift_only_frames) * kDtSeconds * 1000.0;
    metrics.behavior_populated =
        metrics.total_frames == kChainTicks && !metrics.events.empty();
    metrics.short_gap_coast_pass = short_gap_coast_frames > 0;
    metrics.long_loss_release_pass = long_loss_released;
    metrics.reacquire_bumpless_pass =
        first_reacquired_tick >= 0 && reacquire_useful && reacquire_max_delta <= 0.07;
    metrics.reacquire_max_output_delta = reacquire_max_delta;
    metrics.no_blind_candidate_follow_pass = blind_candidate_follow_frames == 0;
    if (!metrics.short_gap_coast_pass)
        metrics.defect_reasons.push_back("short_gap_did_not_coast");
    if (!metrics.long_loss_release_pass)
        metrics.defect_reasons.push_back("long_loss_did_not_release");
    if (!metrics.reacquire_bumpless_pass)
        metrics.defect_reasons.push_back("reacquire_not_bumpless");
    if (!metrics.no_blind_candidate_follow_pass)
        metrics.defect_reasons.push_back("blind_candidate_follow");
    metrics.desired_gate_pass = metrics.defect_reasons.empty();
    metrics.defect_reproduced = !metrics.desired_gate_pass;
    return metrics;
}

double improvement_ratio(double baseline, double candidate) {
    if (baseline <= 0.000001) {
        return 0.0;
    }
    return (baseline - candidate) / baseline;
}

bool relative_motion_quality_passes(const FrequencyRunMetrics& metrics) {
    return metrics.fast_mean_improvement_ratio >= 0.20 &&
        metrics.fast_p95_improvement_ratio >= 0.20 &&
        metrics.same_direction_regression_ratio <= 0.05;
}

bool absolute_motion_quality_passes(const FrequencyRunMetrics& metrics) {
    return metrics.fast_mean_error_px <= 4.0 &&
        metrics.fast_p95_error_px <= 10.0 &&
        metrics.same_direction_mean_error_px <= 5.0;
}

std::vector<FrequencyRunMetrics> run_frequency_matrix() {
    std::vector<FrequencyRunMetrics> runs;
    for (const int vision_hz : {80, 100, 50, 160}) {
        FrequencyRunMetrics metrics;
        metrics.vision_hz = vision_hz;
        metrics.primary_rate = vision_hz == 80 || vision_hz == 100;

        const ScenarioMetrics baseline_fast = run_closed_loop_fixture(
            kClosedLoopFixtures[1],
            vision_hz,
            false);
        const ScenarioMetrics fixed_fast = run_closed_loop_fixture(
            kClosedLoopFixtures[1],
            vision_hz,
            true,
            &metrics);
        const ScenarioMetrics baseline_same = run_closed_loop_fixture(
            kClosedLoopFixtures[2],
            vision_hz,
            false);
        const ScenarioMetrics fixed_same = run_closed_loop_fixture(
            kClosedLoopFixtures[2],
            vision_hz,
            true);

        metrics.baseline_fast_mean_error_px = baseline_fast.mean_abs_error_px;
        metrics.fast_mean_error_px = fixed_fast.mean_abs_error_px;
        metrics.baseline_fast_p95_error_px = baseline_fast.p95_abs_error_px;
        metrics.fast_p95_error_px = fixed_fast.p95_abs_error_px;
        metrics.fast_mean_improvement_ratio = improvement_ratio(
            metrics.baseline_fast_mean_error_px,
            metrics.fast_mean_error_px);
        metrics.fast_p95_improvement_ratio = improvement_ratio(
            metrics.baseline_fast_p95_error_px,
            metrics.fast_p95_error_px);
        metrics.same_direction_regression_ratio = std::max(
            0.0,
            -improvement_ratio(
                baseline_same.mean_abs_error_px,
                fixed_same.mean_abs_error_px));
        metrics.same_direction_mean_error_px = fixed_same.mean_abs_error_px;
        const bool frequency_contract =
            metrics.delivered_vision_sequences > 0 &&
            metrics.delivered_vision_sequences == metrics.fresh_sequences_consumed;
        const bool smooth = metrics.max_lifecycle_ai_delta <= 0.07 &&
            metrics.large_sign_flip_count == 0;
        const bool interframe_transition_coverage = !metrics.primary_rate ||
            (metrics.interframe_left_transition_frames > 0 &&
             metrics.max_interframe_left_delta > 0.0);
        metrics.desired_gate_pass = frequency_contract &&
            interframe_transition_coverage && smooth &&
            (!metrics.primary_rate ||
             relative_motion_quality_passes(metrics) ||
             absolute_motion_quality_passes(metrics));
        runs.push_back(metrics);
    }
    return runs;
}

struct AdsHandoffCaseMetrics {
    double overshoot_px = 0.0;
    double transition_delta = 0.0;
};

AdsHandoffCaseMetrics run_ads_handoff_case(bool moving_relative_motion) {
    GamepadRuntimeConfig config;
    config.ai_aim.target_max_age_ms = 0.0f;
    config.ai_aim.max_pixels = 130.0f;
    config.ai_aim.deadzone_inner = 0.0f;
    config.ai_aim.deadzone_outer = 0.0f;
    config.ai_aim.x_deadzone_outer = 0.0f;
    config.ai_aim.piecewise_mid_pixels = 0.0f;
    config.ai_aim.piecewise_mid_pixels_y = 0.0f;
    config.ai_aim.ads_snap_window_ms = 1000;
    config.ai_aim.ads_max_acquisition_ms = 1000.0f;
    config.ai_aim.ads_completion_radius_px = 8.0f;
    config.ai_aim.ads_completion_fresh_frames = 3;
    config.ai_aim.ads_snap_smoothing = 0.80f;
    config.ai_aim.ads_snap_time_to_go_gain = 0.0f;
    config.ai_aim.body_lock_confidence_frames = 1;
    config.tracker.aim_height_ratio = 0.50f;
    config.ai_aim.body_lock_activation_box_px = 180.0f;
    config.ai_aim.target_projection_reticle_speed_px_per_sec = 1500.0f;
    config.aim_assist_dynamics.enabled = true;
    config.recoil.enabled = false;

    double now = 20.0;
    NativeGamepadController controller(config, [&now] { return now; });
    PhysicalGamepadState physical = intent_probe_input(
        moving_relative_motion ? 0.80f : 0.0f,
        0.0f);
    controller.build_output(physical);

    float previous_ai = 0.0f;
    bool has_previous_ai = false;
    bool transitioned = false;
    AdsHandoffCaseMetrics result;
    const auto submit = [&](float error_x) {
        NativeControllerVisionState target = intent_probe_vision(now, error_x);
        target.selected_track_id = 77;
        target.has_camera_attributed_velocity = moving_relative_motion;
        target.camera_attributed_velocity_x_px_per_sec =
            moving_relative_motion ? 180.0f : 0.0f;
        controller.submit_vision_state(target);
    };
    const auto run_fresh_frame = [&](float error_x) {
        submit(error_x);
        for (int tick = 0; tick < 10; ++tick) {
            now += kDtSeconds;
            controller.build_output(physical);
            const float ai = controller.last_output_components().before_recoil_stick.x;
            if (!transitioned && controller.last_ai_aim_mode() == "body_lock") {
                transitioned = true;
                result.transition_delta = has_previous_ai
                    ? std::fabs(static_cast<double>(ai - previous_ai))
                    : 0.0;
                constexpr double kTerminalHorizonSeconds = 0.050;
                result.overshoot_px = std::max(
                    0.0,
                    (std::fabs(static_cast<double>(ai)) *
                     config.ai_aim.target_projection_reticle_speed_px_per_sec *
                     kTerminalHorizonSeconds) -
                        std::fabs(static_cast<double>(error_x)));
            }
            previous_ai = ai;
            has_previous_ai = true;
        }
    };

    for (int frame = 0; frame < 16; ++frame)
        run_fresh_frame(60.0f);
    for (const float error : {20.0f, 11.0f, 7.0f, 5.0f, 3.0f})
        run_fresh_frame(error);
    for (int frame = 0; frame < 20 && !transitioned; ++frame)
        run_fresh_frame(3.0f);
    if (!transitioned) {
        result.overshoot_px = 1.0e9;
        result.transition_delta = 1.0e9;
    }
    return result;
}

AdsHandoffMetrics run_ads_handoff_probe() {
    const AdsHandoffCaseMetrics stationary = run_ads_handoff_case(false);
    const AdsHandoffCaseMetrics moving = run_ads_handoff_case(true);
    AdsHandoffMetrics metrics;
    metrics.stationary_transition_overshoot_px = stationary.overshoot_px;
    metrics.moving_transition_overshoot_px = moving.overshoot_px;
    metrics.max_transition_overshoot_px = std::max(
        stationary.overshoot_px,
        moving.overshoot_px);
    metrics.max_transition_ai_delta = std::max(
        stationary.transition_delta,
        moving.transition_delta);
    metrics.desired_gate_pass = metrics.max_transition_overshoot_px <= 2.0 &&
        metrics.max_transition_ai_delta <= 0.07;
    return metrics;
}

void write_bool(std::ostream& output, bool value) {
    output << (value ? "true" : "false");
}

}  // namespace

BenchmarkReport run_benchmark() {
    BenchmarkReport report;
    report.intent_invariance = run_intent_invariance_probe();
    report.frequency_runs = run_frequency_matrix();
    report.primary.fast_mean_improvement_ratio = 1.0;
    report.primary.fast_p95_improvement_ratio = 1.0;
    report.primary.same_direction_regression_ratio = 0.0;
    report.primary.max_fast_mean_error_px = 0.0;
    report.primary.max_fast_p95_error_px = 0.0;
    report.primary.max_same_direction_mean_error_px = 0.0;
    report.primary.max_lifecycle_ai_delta = 0.0;
    report.primary.large_sign_flip_count = 0;
    bool all_frequency_runs_pass = true;
    for (const FrequencyRunMetrics& run : report.frequency_runs) {
        all_frequency_runs_pass = all_frequency_runs_pass && run.desired_gate_pass;
        if (!run.desired_gate_pass)
            ++report.defect_count;
        if (!run.primary_rate)
            continue;
        report.primary.fast_mean_improvement_ratio = std::min(
            report.primary.fast_mean_improvement_ratio,
            run.fast_mean_improvement_ratio);
        report.primary.fast_p95_improvement_ratio = std::min(
            report.primary.fast_p95_improvement_ratio,
            run.fast_p95_improvement_ratio);
        report.primary.same_direction_regression_ratio = std::max(
            report.primary.same_direction_regression_ratio,
            run.same_direction_regression_ratio);
        report.primary.max_fast_mean_error_px = std::max(
            report.primary.max_fast_mean_error_px,
            run.fast_mean_error_px);
        report.primary.max_fast_p95_error_px = std::max(
            report.primary.max_fast_p95_error_px,
            run.fast_p95_error_px);
        report.primary.max_same_direction_mean_error_px = std::max(
            report.primary.max_same_direction_mean_error_px,
            run.same_direction_mean_error_px);
        report.primary.max_lifecycle_ai_delta = std::max(
            report.primary.max_lifecycle_ai_delta,
            run.max_lifecycle_ai_delta);
        report.primary.large_sign_flip_count += run.large_sign_flip_count;
    }
    const bool primary_relative_quality =
        report.primary.fast_mean_improvement_ratio >= 0.20 &&
        report.primary.fast_p95_improvement_ratio >= 0.20 &&
        report.primary.same_direction_regression_ratio <= 0.05;
    const bool primary_absolute_quality =
        report.primary.max_fast_mean_error_px <= 4.0 &&
        report.primary.max_fast_p95_error_px <= 10.0 &&
        report.primary.max_same_direction_mean_error_px <= 5.0;
    report.primary.desired_gate_pass =
        (primary_relative_quality || primary_absolute_quality) &&
        report.primary.max_lifecycle_ai_delta <= 0.07 &&
        report.primary.large_sign_flip_count == 0;
    report.scenarios.reserve(kClosedLoopFixtures.size());
    for (const ClosedLoopFixture& fixture_config : kClosedLoopFixtures) {
        report.scenarios.push_back(run_closed_loop_fixture(
            fixture_config,
            100,
            true));
    }
    report.production_chain = run_production_chain_probe();
    report.ads_handoff = run_ads_handoff_probe();
    if (report.intent_invariance.left_intent_ignored) {
        ++report.defect_count;
    }
    if (report.production_chain.defect_reproduced) {
        ++report.defect_count;
    }
    if (!report.ads_handoff.desired_gate_pass) {
        ++report.defect_count;
    }
    report.desired_gate_pass = report.intent_invariance.desired_gate_pass &&
        all_frequency_runs_pass && report.primary.desired_gate_pass &&
        report.production_chain.desired_gate_pass &&
        report.ads_handoff.desired_gate_pass;
    return report;
}

bool validate_report(const BenchmarkReport& report, std::string* reason) {
    const auto fail = [&](const char* message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };

    if (report.schema_version != 3) {
        return fail("unexpected schema version");
    }
    if (report.controller_hz != kControllerHz || report.vision_hz != 100 ||
        report.vision_delay_ms != 30 ||
        report.ticks_per_scenario != kTicksPerScenario ||
        report.evaluation_start_tick != kEvaluationStartTick) {
        return fail("unexpected deterministic timing contract");
    }
    if (report.frequency_runs.size() != 4) {
        return fail("expected four live-rate frequency runs");
    }
    std::set<int> primary_rates;
    std::set<int> stress_rates;
    for (const FrequencyRunMetrics& run : report.frequency_runs) {
        const bool frequency_contract = run.vision_hz > 0 &&
            run.delivered_vision_sequences > 0 &&
            run.fresh_sequences_consumed == run.delivered_vision_sequences;
        if (!frequency_contract) {
            return fail("vision sequence consumption mismatch");
        }
        for (const double value : {
                 run.baseline_fast_mean_error_px,
                 run.fast_mean_error_px,
                 run.baseline_fast_p95_error_px,
                 run.fast_p95_error_px,
                 run.fast_mean_improvement_ratio,
                 run.fast_p95_improvement_ratio,
                 run.same_direction_regression_ratio,
                 run.max_lifecycle_ai_delta,
                 run.max_relative_lead_px,
                 run.max_interframe_left_delta}) {
            if (!std::isfinite(value)) {
                return fail("frequency run contains non-finite metric");
            }
        }
        const bool expected_frequency_gate =
            (!run.primary_rate ||
             (run.interframe_left_transition_frames > 0 &&
              run.max_interframe_left_delta > 0.0)) &&
            run.max_lifecycle_ai_delta <= 0.07 &&
            run.large_sign_flip_count == 0 &&
            (!run.primary_rate ||
             relative_motion_quality_passes(run) ||
             absolute_motion_quality_passes(run));
        if (run.desired_gate_pass != expected_frequency_gate) {
            return fail("frequency desired gate does not match metrics");
        }
        (run.primary_rate ? primary_rates : stress_rates).insert(run.vision_hz);
    }
    if (primary_rates != std::set<int>({80, 100}) ||
        stress_rates != std::set<int>({50, 160})) {
        return fail("unexpected live-rate frequency matrix");
    }
    if (report.scenarios.size() != kClosedLoopFixtures.size()) {
        return fail("expected five closed-loop scenarios");
    }
    if (report.intent_invariance.phase_event_samples != 15) {
        return fail("intent probe did not sample every phase boundary");
    }
    if (report.intent_invariance.max_left_passthrough_error > 1.0e-6) {
        return fail("left output passthrough did not match physical input");
    }
    if (report.intent_invariance.desired_gate_pass ==
        report.intent_invariance.left_intent_ignored) {
        return fail("intent desired gate does not match metrics");
    }

    for (const double value : {
             report.primary.fast_mean_improvement_ratio,
             report.primary.fast_p95_improvement_ratio,
             report.primary.same_direction_regression_ratio,
             report.primary.max_fast_mean_error_px,
             report.primary.max_fast_p95_error_px,
             report.primary.max_same_direction_mean_error_px,
             report.primary.max_lifecycle_ai_delta}) {
        if (!std::isfinite(value)) {
            return fail("primary summary contains non-finite metric");
        }
    }
    const bool expected_primary_relative_quality =
        report.primary.fast_mean_improvement_ratio >= 0.20 &&
        report.primary.fast_p95_improvement_ratio >= 0.20 &&
        report.primary.same_direction_regression_ratio <= 0.05;
    const bool expected_primary_absolute_quality =
        report.primary.max_fast_mean_error_px <= 4.0 &&
        report.primary.max_fast_p95_error_px <= 10.0 &&
        report.primary.max_same_direction_mean_error_px <= 5.0;
    const bool expected_primary_gate =
        (expected_primary_relative_quality || expected_primary_absolute_quality) &&
        report.primary.max_lifecycle_ai_delta <= 0.07 &&
        report.primary.large_sign_flip_count == 0;
    if (report.primary.desired_gate_pass != expected_primary_gate) {
        return fail("primary desired gate does not match metrics");
    }

    const ProductionChainMetrics& chain = report.production_chain;
    if (chain.name != "production_chain_strafe_reacquire" ||
        chain.total_frames != kChainTicks || !chain.behavior_populated ||
        chain.events.empty()) {
        return fail("invalid production-chain fixture");
    }
    if (chain.body_lock_frames <= 0 || chain.ads_snap_frames <= 0 ||
        chain.manual_frames <= 0 ||
        chain.mode_transitions <= 0) {
        return fail("missing production-chain mode coverage");
    }
    if (chain.detector_candidate_gap_frames <= 0 ||
        chain.production_target_missing_frames <= 0 ||
        chain.production_target_missing_frames >
            chain.detector_candidate_gap_frames) {
        return fail("missing candidate-present gap");
    }
    if (chain.drift_manual_correction_frames != 0 ||
        chain.max_abs_manual_right > kDriftManualThreshold) {
        return fail("invalid drift classification");
    }
    for (const double value : {
             chain.max_abs_manual_right,
             chain.max_continuous_drift_only_ms,
             chain.reacquire_latency_ms,
             chain.reacquire_useful_latency_ms,
             chain.reacquire_max_output_delta,
             chain.pre_loss_error_px,
             chain.post_reacquire_error_px}) {
        if (!std::isfinite(value)) {
            return fail("production-chain contains non-finite metric");
        }
    }
    if (chain.reacquire_latency_ms < 0.0) {
        return fail("production-chain reacquisition was not observed");
    }
    const bool expected_reacquire_gate = chain.reacquire_latency_ms >= 0.0 &&
        chain.reacquire_useful_latency_ms >= 0.0 &&
        chain.reacquire_useful_latency_ms <= 60.0 &&
        chain.reacquire_max_output_delta <= 0.07;
    if (chain.reacquire_bumpless_pass != expected_reacquire_gate) {
        return fail("production-chain reacquisition envelope is invalid");
    }
    if (chain.selected_track_changes <= 0) {
        return fail("missing selected-track rebind");
    }
    const bool expected_chain_gate = chain.short_gap_coast_pass &&
        chain.long_loss_release_pass && chain.reacquire_bumpless_pass &&
        chain.no_blind_candidate_follow_pass;
    if (chain.desired_gate_pass != expected_chain_gate ||
        chain.defect_reproduced == expected_chain_gate) {
        return fail("production-chain desired gate does not match metrics");
    }

    for (const double value : {
             report.ads_handoff.stationary_transition_overshoot_px,
             report.ads_handoff.moving_transition_overshoot_px,
             report.ads_handoff.max_transition_overshoot_px,
             report.ads_handoff.max_transition_ai_delta}) {
        if (!std::isfinite(value)) {
            return fail("ADS handoff contains non-finite metric");
        }
    }
    const bool expected_ads_gate =
        report.ads_handoff.max_transition_overshoot_px <= 2.0 &&
        report.ads_handoff.max_transition_ai_delta <= 0.07;
    if (report.ads_handoff.desired_gate_pass != expected_ads_gate) {
        return fail("ADS handoff desired gate does not match metrics");
    }

    std::set<std::string> names;
    for (const ScenarioMetrics& scenario : report.scenarios) {
        if (scenario.name.empty() || scenario.body_height_px <= 0.0 ||
            !scenario.behavior_populated || scenario.events.empty()) {
            return fail("invalid scenario fixture");
        }
        for (const double value : {
                 scenario.mean_abs_error_px,
                 scenario.p95_abs_error_px,
                 scenario.max_abs_error_px,
                 scenario.final_abs_error_px,
                 scenario.max_ai_output,
                 scenario.max_final_output,
                 scenario.max_final_output_delta,
                 scenario.minimum_manual_retention}) {
            if (!std::isfinite(value)) {
                return fail("scenario contains non-finite metric");
            }
        }
        if (scenario.minimum_manual_retention < 0.5 ||
            scenario.minimum_manual_retention > 1.0) {
            return fail("scenario contains invalid manual retention");
        }
        names.insert(scenario.name);
    }
    int expected_defect_count = report.intent_invariance.left_intent_ignored ? 1 : 0;
    bool expected_gate_pass = report.intent_invariance.desired_gate_pass &&
        report.primary.desired_gate_pass;
    for (const FrequencyRunMetrics& run : report.frequency_runs) {
        if (!run.desired_gate_pass)
            ++expected_defect_count;
        expected_gate_pass = expected_gate_pass && run.desired_gate_pass;
    }
    if (chain.defect_reproduced) {
        ++expected_defect_count;
    }
    expected_gate_pass = expected_gate_pass && chain.desired_gate_pass;
    if (!report.ads_handoff.desired_gate_pass)
        ++expected_defect_count;
    expected_gate_pass = expected_gate_pass && report.ads_handoff.desired_gate_pass;
    if (report.defect_count != expected_defect_count ||
        report.desired_gate_pass != expected_gate_pass) {
        return fail("report summary does not match scenario gates");
    }
    for (const ClosedLoopFixture& expected : kClosedLoopFixtures) {
        if (names.find(expected.name) == names.end()) {
            return fail("missing required scenario fixture");
        }
    }
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

void write_json(std::ostream& output, const BenchmarkReport& report) {
    output << "{\n"
           << "  \"schema_version\": " << report.schema_version << ",\n"
           << "  \"controller_hz\": " << report.controller_hz << ",\n"
           << "  \"vision_hz\": " << report.vision_hz << ",\n"
           << "  \"vision_delay_ms\": " << report.vision_delay_ms << ",\n"
           << "  \"ticks_per_scenario\": " << report.ticks_per_scenario << ",\n"
           << "  \"evaluation_start_tick\": " << report.evaluation_start_tick << ",\n"
           << "  \"intent_invariance\": {\n"
           << "    \"max_ai_trace_delta\": "
           << report.intent_invariance.max_ai_trace_delta << ",\n"
           << "    \"max_final_right_trace_delta\": "
           << report.intent_invariance.max_final_right_trace_delta << ",\n"
           << "    \"max_left_passthrough_error\": "
           << report.intent_invariance.max_left_passthrough_error << ",\n"
           << "    \"phase_event_samples\": "
           << report.intent_invariance.phase_event_samples << ",\n"
           << "    \"left_intent_ignored\": ";
    write_bool(output, report.intent_invariance.left_intent_ignored);
    output << ",\n    \"desired_gate_pass\": ";
    write_bool(output, report.intent_invariance.desired_gate_pass);
    const ProductionChainMetrics& chain = report.production_chain;
    output << "\n  },\n"
           << "  \"primary\": {\n"
           << "    \"fast_mean_improvement_ratio\": "
           << report.primary.fast_mean_improvement_ratio << ",\n"
           << "    \"fast_p95_improvement_ratio\": "
           << report.primary.fast_p95_improvement_ratio << ",\n"
           << "    \"same_direction_regression_ratio\": "
           << report.primary.same_direction_regression_ratio << ",\n"
           << "    \"max_fast_mean_error_px\": "
           << report.primary.max_fast_mean_error_px << ",\n"
           << "    \"max_fast_p95_error_px\": "
           << report.primary.max_fast_p95_error_px << ",\n"
           << "    \"max_same_direction_mean_error_px\": "
           << report.primary.max_same_direction_mean_error_px << ",\n"
           << "    \"max_lifecycle_ai_delta\": "
           << report.primary.max_lifecycle_ai_delta << ",\n"
           << "    \"large_sign_flip_count\": "
           << report.primary.large_sign_flip_count << ",\n"
           << "    \"desired_gate_pass\": ";
    write_bool(output, report.primary.desired_gate_pass);
    output << "\n  },\n  \"frequency_runs\": [\n";
    for (std::size_t index = 0; index < report.frequency_runs.size(); ++index) {
        const FrequencyRunMetrics& run = report.frequency_runs[index];
        output << "    {\"vision_hz\": " << run.vision_hz
               << ", \"primary_rate\": ";
        write_bool(output, run.primary_rate);
        output << ", \"delivered_vision_sequences\": "
               << run.delivered_vision_sequences
               << ", \"fresh_sequences_consumed\": "
               << run.fresh_sequences_consumed
               << ", \"baseline_fast_mean_error_px\": "
               << run.baseline_fast_mean_error_px
               << ", \"fast_mean_error_px\": " << run.fast_mean_error_px
               << ", \"baseline_fast_p95_error_px\": "
               << run.baseline_fast_p95_error_px
               << ", \"fast_p95_error_px\": " << run.fast_p95_error_px
               << ", \"fast_mean_improvement_ratio\": "
               << run.fast_mean_improvement_ratio
               << ", \"fast_p95_improvement_ratio\": "
               << run.fast_p95_improvement_ratio
               << ", \"same_direction_regression_ratio\": "
               << run.same_direction_regression_ratio
               << ", \"same_direction_mean_error_px\": "
               << run.same_direction_mean_error_px
               << ", \"max_lifecycle_ai_delta\": "
               << run.max_lifecycle_ai_delta
               << ", \"max_relative_lead_px\": "
               << run.max_relative_lead_px
               << ", \"max_strafe_gain\": " << run.max_strafe_gain
               << ", \"warm_relative_motion_frames\": "
               << run.warm_relative_motion_frames
               << ", \"rejected_relative_motion_frames\": "
               << run.rejected_relative_motion_frames
               << ", \"interframe_left_transition_frames\": "
               << run.interframe_left_transition_frames
               << ", \"max_interframe_left_delta\": "
               << run.max_interframe_left_delta
               << ", \"large_sign_flip_count\": "
               << run.large_sign_flip_count
               << ", \"desired_gate_pass\": ";
        write_bool(output, run.desired_gate_pass);
        output << (index + 1 == report.frequency_runs.size() ? "}\n" : "},\n");
    }
    output << "  ],\n  \"ads_handoff\": {\n"
           << "    \"stationary_transition_overshoot_px\": "
           << report.ads_handoff.stationary_transition_overshoot_px << ",\n"
           << "    \"moving_transition_overshoot_px\": "
           << report.ads_handoff.moving_transition_overshoot_px << ",\n"
           << "    \"max_transition_overshoot_px\": "
           << report.ads_handoff.max_transition_overshoot_px << ",\n"
           << "    \"max_transition_ai_delta\": "
           << report.ads_handoff.max_transition_ai_delta << ",\n"
           << "    \"desired_gate_pass\": ";
    write_bool(output, report.ads_handoff.desired_gate_pass);
    output << "\n  },\n"
           << "  \"production_chain\": {\n"
           << "    \"name\": \"" << chain.name << "\",\n"
           << "    \"total_frames\": " << chain.total_frames << ",\n"
           << "    \"body_lock_frames\": " << chain.body_lock_frames << ",\n"
           << "    \"ads_snap_frames\": " << chain.ads_snap_frames << ",\n"
           << "    \"manual_frames\": " << chain.manual_frames << ",\n"
           << "    \"mode_transitions\": " << chain.mode_transitions << ",\n"
           << "    \"detector_candidate_gap_frames\": "
           << chain.detector_candidate_gap_frames << ",\n"
           << "    \"production_target_missing_frames\": "
           << chain.production_target_missing_frames << ",\n"
           << "    \"target_present_bodylock_unavailable_frames\": "
           << chain.target_present_bodylock_unavailable_frames << ",\n"
           << "    \"drift_manual_correction_frames\": "
           << chain.drift_manual_correction_frames << ",\n"
           << "    \"drift_only_final_frames\": "
           << chain.drift_only_final_frames << ",\n"
           << "    \"requested_suppressed_frames\": "
           << chain.requested_suppressed_frames << ",\n"
           << "    \"selected_track_changes\": "
           << chain.selected_track_changes << ",\n"
           << "    \"max_abs_manual_right\": "
           << chain.max_abs_manual_right << ",\n"
           << "    \"max_continuous_drift_only_ms\": "
           << chain.max_continuous_drift_only_ms << ",\n"
           << "    \"reacquire_latency_ms\": "
           << chain.reacquire_latency_ms << ",\n"
           << "    \"reacquire_useful_latency_ms\": "
           << chain.reacquire_useful_latency_ms << ",\n"
           << "    \"reacquire_max_output_delta\": "
           << chain.reacquire_max_output_delta << ",\n"
           << "    \"pre_loss_error_px\": " << chain.pre_loss_error_px << ",\n"
           << "    \"post_reacquire_error_px\": "
           << chain.post_reacquire_error_px << ",\n"
           << "    \"behavior_populated\": ";
    write_bool(output, chain.behavior_populated);
    output << ",\n    \"defect_reproduced\": ";
    write_bool(output, chain.defect_reproduced);
    output << ",\n    \"short_gap_coast_pass\": ";
    write_bool(output, chain.short_gap_coast_pass);
    output << ",\n    \"long_loss_release_pass\": ";
    write_bool(output, chain.long_loss_release_pass);
    output << ",\n    \"reacquire_bumpless_pass\": ";
    write_bool(output, chain.reacquire_bumpless_pass);
    output << ",\n    \"no_blind_candidate_follow_pass\": ";
    write_bool(output, chain.no_blind_candidate_follow_pass);
    output << ",\n    \"desired_gate_pass\": ";
    write_bool(output, chain.desired_gate_pass);
    output << ",\n    \"defect_reasons\": [";
    for (std::size_t index = 0; index < chain.defect_reasons.size(); ++index) {
        output << "\"" << chain.defect_reasons[index] << "\"";
        if (index + 1 != chain.defect_reasons.size()) {
            output << ", ";
        }
    }
    output << "],\n    \"events\": [\n";
    for (std::size_t index = 0; index < chain.events.size(); ++index) {
        const ProductionChainEvent& event = chain.events[index];
        output << "      {\"phase\": \"" << event.phase
               << "\", \"tick\": " << event.tick
               << ", \"detector_candidates_present\": ";
        write_bool(output, event.detector_candidates_present);
        output << ", \"production_target_present\": ";
        write_bool(output, event.production_target_present);
        output << ", \"selected_track_id\": " << event.selected_track_id
               << ", \"manual_right_x\": " << event.manual_right_x
               << ", \"requested_ai_x\": " << event.requested_ai_x
               << ", \"final_right_x\": " << event.final_right_x
               << ", \"target_error_px\": " << event.target_error_px
               << ", \"aim_mode\": \"" << event.aim_mode
               << "\", \"lifecycle\": \"" << event.lifecycle
               << "\", \"limit_reason\": \"" << event.limit_reason
               << "\"}";
        output << (index + 1 == chain.events.size() ? "\n" : ",\n");
    }
    output << "    ]\n  },\n"
           << "  \"closed_loop_behavior_populated\": true,\n"
           << "  \"defect_count\": " << report.defect_count << ",\n"
           << "  \"desired_gate_pass\": ";
    write_bool(output, report.desired_gate_pass);
    output << ",\n  \"scenarios\": [\n";
    for (std::size_t index = 0; index < report.scenarios.size(); ++index) {
        const ScenarioMetrics& scenario = report.scenarios[index];
        output << "    {\n"
               << "      \"name\": \"" << scenario.name << "\",\n"
               << "      \"mobility\": \"" << scenario.mobility << "\",\n"
               << "      \"target_relation\": \"" << scenario.target_relation << "\",\n"
               << "      \"body_height_px\": " << scenario.body_height_px << ",\n"
               << "      \"mean_abs_error_px\": " << scenario.mean_abs_error_px << ",\n"
               << "      \"p95_abs_error_px\": " << scenario.p95_abs_error_px << ",\n"
               << "      \"max_abs_error_px\": " << scenario.max_abs_error_px << ",\n"
               << "      \"final_abs_error_px\": " << scenario.final_abs_error_px << ",\n"
               << "      \"max_ai_output\": " << scenario.max_ai_output << ",\n"
               << "      \"max_final_output\": " << scenario.max_final_output << ",\n"
               << "      \"max_final_output_delta\": " << scenario.max_final_output_delta << ",\n"
               << "      \"axis_intent_intervention_frames\": "
               << scenario.axis_intent_intervention_frames << ",\n"
               << "      \"minimum_manual_retention\": "
               << scenario.minimum_manual_retention << ",\n"
               << "      \"onset_response_latency_ms\": " << scenario.onset_response_latency_ms << ",\n"
               << "      \"reversal_response_latency_ms\": " << scenario.reversal_response_latency_ms << ",\n"
               << "      \"release_response_latency_ms\": " << scenario.release_response_latency_ms << ",\n"
               << "      \"onset_peak_error_px\": " << scenario.onset_peak_error_px << ",\n"
               << "      \"reversal_peak_error_px\": " << scenario.reversal_peak_error_px << ",\n"
               << "      \"release_peak_error_px\": " << scenario.release_peak_error_px << ",\n"
               << "      \"ai_opposes_manual_frames\": " << scenario.ai_opposes_manual_frames << ",\n"
               << "      \"final_opposes_oracle_frames\": " << scenario.final_opposes_oracle_frames << ",\n"
               << "      \"high_ai_output_frames\": " << scenario.high_ai_output_frames << ",\n"
               << "      \"output_spike_frames\": " << scenario.output_spike_frames << ",\n"
               << "      \"settled_frames\": " << scenario.settled_frames << ",\n"
               << "      \"defect_reproduced\": ";
        write_bool(output, scenario.defect_reproduced);
        output << ",\n      \"desired_gate_pass\": ";
        write_bool(output, scenario.desired_gate_pass);
        output << ",\n      \"defect_reasons\": [";
        for (std::size_t reason_index = 0;
             reason_index < scenario.defect_reasons.size();
             ++reason_index) {
            output << "\"" << scenario.defect_reasons[reason_index] << "\"";
            if (reason_index + 1 != scenario.defect_reasons.size()) {
                output << ", ";
            }
        }
        output << "],\n      \"events\": [\n";
        for (std::size_t event_index = 0; event_index < scenario.events.size(); ++event_index) {
            const PhaseEvent& event = scenario.events[event_index];
            output << "        {\"phase\": \"" << event.phase
                   << "\", \"tick\": " << event.tick
                   << ", \"time_seconds\": " << event.time_seconds
                   << ", \"left_x\": " << event.left_x
                   << ", \"manual_right_x\": " << event.manual_right_x
                   << ", \"ai_right_x\": " << event.ai_right_x
                   << ", \"final_right_x\": " << event.final_right_x
                   << ", \"oracle_right_x\": " << event.oracle_right_x
                   << ", \"observed_error_px\": " << event.observed_error_px
                   << ", \"true_error_px\": " << event.true_error_px
                   << ", \"vision_age_ms\": " << event.vision_age_ms
                   << ", \"player_velocity_body_s\": " << event.player_velocity_body_s
                   << ", \"target_velocity_body_s\": " << event.target_velocity_body_s
                   << ", \"aim_mode\": \"" << event.aim_mode << "\"}";
            output << (event_index + 1 == scenario.events.size() ? "\n" : ",\n");
        }
        output << "      ]\n    }";
        output << (index + 1 == report.scenarios.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

}  // namespace controller_native::left_stick_defect
