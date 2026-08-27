#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>

namespace controller_native::sustained_aimlab {
namespace {

bool finite(Vec2d value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

void require_finite(const ControllerStepResult& output) {
    if (!finite(output.final_stick) ||
        !finite(output.requested_assist_stick) ||
        !finite(output.shaped_assist_stick) ||
        !finite(output.predicted_terminal_error_px) ||
        !std::isfinite(output.radial_closing_velocity_px_per_sec)) {
        throw std::runtime_error("controller produced non-finite output");
    }
}

Vec2d normalized_control_direction(Vec2d error) noexcept {
    error.y = -error.y;
    const double magnitude = length(error);
    if (magnitude <= 1e-9) return {};
    return {error.x / magnitude, error.y / magnitude};
}

Vec2d mixed_manual_input(
    const TargetScript& target,
    int target_elapsed_ms,
    Vec2d error,
    Vec2d target_velocity) noexcept {
    const Vec2d helpful = normalized_control_direction(error);
    Vec2d tangent{-helpful.y, helpful.x};
    const Vec2d control_velocity{target_velocity.x, -target_velocity.y};
    if (control_velocity.x * tangent.x + control_velocity.y * tangent.y < 0.0) {
        tangent.x = -tangent.x;
        tangent.y = -tangent.y;
    }
    if (target.id % 14 == 7) {
        return {
            helpful.x * -0.30 + tangent.x * 0.24,
            helpful.y * -0.30 + tangent.y * 0.24};
    }
    if (target.id % 14 == 8) {
        return {
            helpful.x * 0.24 - tangent.x * 0.30,
            helpful.y * 0.24 - tangent.y * 0.30};
    }
    switch (target.id % 7) {
    case 0:
        return {0.012, -0.008};
    case 1:
        return {helpful.x * 0.25, helpful.y * 0.25};
    case 2:
        if (target_elapsed_ms >= 80 && target_elapsed_ms < 140) {
            return {-helpful.x * 0.32, -helpful.y * 0.32};
        }
        return {};
    case 3:
        if (target_elapsed_ms >= 120) {
            return {helpful.x * 0.30, helpful.y * 0.30};
        }
        return {};
    case 4:
        return {helpful.x * 0.35, -helpful.y * 0.20};
    case 5:
        if (target_elapsed_ms >= 180 && target_elapsed_ms < 240) {
            return {-helpful.x * 0.44, -helpful.y * 0.44};
        }
        return {helpful.x * 0.18, helpful.y * 0.18};
    case 6:
        if (target_elapsed_ms >= 500 && target_elapsed_ms < 560) {
            return {-helpful.x * 0.60, -helpful.y * 0.60};
        }
        return {};
    }
    return {};
}

Vec2d scripted_manual_input(
    const TargetScript& target,
    int target_elapsed_ms) noexcept {
    // Unlike mixed_manual_input(), this direction is anchored at spawn and is
    // therefore identical for every controller replay of the same script.
    return mixed_manual_input(
        target,
        target_elapsed_ms,
        target.initial_error_px,
        target.initial_velocity_px_per_second);
}

Vec2d runtime_manual_input(
    const BenchmarkConfig& config,
    const TargetScript& target,
    BenchmarkCohort cohort,
    int target_elapsed_ms,
    Vec2d error) noexcept {
    if (config.runtime_manual_segments.empty() || target_elapsed_ms < 0) {
        return {};
    }
    const bool has_segment = cohort == BenchmarkCohort::AdsAcquire
        ? target.has_runtime_ads_manual_segment
        : target.has_runtime_bodylock_manual_segment;
    if (!has_segment) {
        return {};
    }
    const std::size_t segment_index = cohort == BenchmarkCohort::AdsAcquire
        ? target.runtime_ads_manual_segment_index
        : target.runtime_bodylock_manual_segment_index;
    const auto& segment = config.runtime_manual_segments[
        segment_index % config.runtime_manual_segments.size()];
    int remaining_ms = target_elapsed_ms;
    for (const auto& sample : segment.samples) {
        if (remaining_ms < sample.duration_ms) {
            const Vec2d helpful = normalized_control_direction(error);
            const Vec2d tangent{-helpful.y, helpful.x};
            return {
                helpful.x * sample.radial + tangent.x * sample.tangential,
                helpful.y * sample.radial + tangent.y * sample.tangential,
            };
        }
        remaining_ms -= sample.duration_ms;
    }
    return {};
}

Vec2d wrong_then_correct_manual_input(
    const TargetScript& target,
    int target_elapsed_ms) noexcept {
    const Vec2d helpful = normalized_control_direction(
        target.initial_error_px);
    const double magnitude =
        0.65 + static_cast<double>((target.id * 29u) % 31u) / 100.0;
    const int wrong_ms =
        70 + static_cast<int>((target.id * 17u) % 61u);
    const int correction_ms =
        160 + static_cast<int>((target.id * 19u) % 81u);
    const double direction =
        target_elapsed_ms < wrong_ms ? -1.0 :
        target_elapsed_ms < wrong_ms + correction_ms ? 1.0 : 0.0;
    return {
        helpful.x * magnitude * direction,
        helpful.y * magnitude * direction,
    };
}

Vec2d arc_recovery_manual_input(
    const TargetScript& target,
    int target_elapsed_ms) noexcept {
    constexpr double kPi = 3.14159265358979323846;
    const Vec2d helpful = normalized_control_direction(
        target.initial_error_px);
    const Vec2d tangent{-helpful.y, helpful.x};
    const double magnitude =
        0.65 + static_cast<double>((target.id * 29u) % 31u) / 100.0;
    const int arc_ms =
        140 + static_cast<int>((target.id * 23u) % 81u);
    const int hold_ms =
        100 + static_cast<int>((target.id * 11u) % 61u);
    if (target_elapsed_ms >= arc_ms + hold_ms) return {};
    if (target_elapsed_ms >= arc_ms) {
        return {helpful.x * magnitude, helpful.y * magnitude};
    }
    const double progress = std::clamp(
        static_cast<double>(target_elapsed_ms) /
            static_cast<double>(arc_ms),
        0.0, 1.0);
    const double angle = 0.5 * kPi * (1.0 - progress);
    const double turn = target.id % 2u == 0u ? 1.0 : -1.0;
    return {
        magnitude * (
            helpful.x * std::cos(angle) +
            tangent.x * turn * std::sin(angle)),
        magnitude * (
            helpful.y * std::cos(angle) +
            tangent.y * turn * std::sin(angle)),
    };
}

Vec2d micro_correction_manual_input(
    const TargetScript& target,
    int target_elapsed_ms) noexcept {
    // Live reproduction: a 3% horizontal nudge followed by one correction.
    // It stays below the manual-escape threshold by construction.
    constexpr double kMagnitude = 0.03;
    const double initial_sign = target.id % 2u == 0u ? -1.0 : 1.0;
    if (target_elapsed_ms < 320) return {initial_sign * kMagnitude, 0.0};
    if (target_elapsed_ms < 640) return {-initial_sign * kMagnitude, 0.0};
    return {};
}

Vec2d recoil_controller_manual_input(
    const TargetScript& target,
    int target_elapsed_ms) noexcept {
    // A user who fires and actively counters recoil: a sustained downward pull
    // that begins with firing (the benchmark fires from ~80ms of tracking).
    // Purely downward: the assist owns the horizontal channel, exactly like a
    // real range session where the user's anti-recoil pull is along screen-down.
    // The pull stays under the manual-escape threshold.
    constexpr double kPull = 0.30;
    if (target_elapsed_ms < 80) return {};
    return {0.0, -kPull};
}

Vec2d recoil_flail_manual_input(
    const TargetScript& target,
    int target_elapsed_ms) noexcept {
    // A firing user in a degraded state (low blood sugar / fatigue): input
    // alternates directions fast enough that the intent filter lags, and the
    // downward phases are not genuine recoil control. This is the §7 mislabel
    // probe: the classifier's recoil_pull priority may label downward phases as
    // RecoilPull. Operation labels are telemetry only and must never change
    // the independent final-stage recoil output.
    constexpr double kPhaseMs = 25.0;
    const int phase = static_cast<int>(
        target_elapsed_ms / kPhaseMs) % 4;
    switch (phase) {
    case 0: return {0.20, -0.28};   // down-right
    case 1: return {-0.18, 0.12};   // up-left (reversal)
    case 2: return {-0.22, -0.24};  // down-left
    default: return {0.16, 0.10};   // up-right (reversal)
    }
}

double left_strafe_input(
    const PlayerStrafeScript& strafe,
    int elapsed_ms,
    PlayerStrafeMode mode) noexcept {
    if (mode != PlayerStrafeMode::FullReversal ||
        elapsed_ms < strafe.onset_ms ||
        elapsed_ms >= strafe.release_ms) {
        return 0.0;
    }
    const double direction = static_cast<double>(strafe.initial_direction);
    return elapsed_ms < strafe.reverse_ms ? direction : -direction;
}

bool has_physical_camera_recoil(VisionDisturbanceProfile profile) noexcept {
    return profile == VisionDisturbanceProfile::CameraRecoil ||
        profile == VisionDisturbanceProfile::GunKickAndCameraRecoil ||
        profile ==
            VisionDisturbanceProfile::BodyBoxDeformationAndCameraRecoil;
}

bool has_body_box_deformation(VisionDisturbanceProfile profile) noexcept {
    return profile == VisionDisturbanceProfile::BodyBoxDeformation ||
        profile ==
            VisionDisturbanceProfile::BodyBoxDeformationAndCameraRecoil;
}

Vec2d firing_body_box_edge_deformation(int tracking_ms) noexcept {
    constexpr int kFirstShotMs = 80;
    constexpr int kShotPeriodMs = 100;
    if (tracking_ms < kFirstShotMs) return {};
    const int shot_age = (tracking_ms - kFirstShotMs) % kShotPeriodMs;
    const int shot_index = (tracking_ms - kFirstShotMs) / kShotPeriodMs;
    constexpr int kRiseMs = 11;
    constexpr int kRecoverMs = 72;
    double envelope = 0.0;
    if (shot_age < kRiseMs) {
        envelope = static_cast<double>(shot_age) / kRiseMs;
    } else if (shot_age < kRiseMs + kRecoverMs) {
        envelope = 1.0 -
            static_cast<double>(shot_age - kRiseMs) / kRecoverMs;
    }
    const double horizontal_sign = (shot_index & 1) == 0 ? 1.0 : -1.0;
    // One horizontal edge and the lower vertical edge are reconstructed by
    // the detector as the weapon/optic crosses the person silhouette.
    return {horizontal_sign * 16.0 * envelope, -46.0 * envelope};
}

Vec2d physical_camera_recoil_offset(int tracking_ms) noexcept {
    constexpr int kFirstShotMs = 80;
    constexpr int kShotPeriodMs = 100;
    if (tracking_ms < kFirstShotMs) return {};
    Vec2d result;
    const int latest_shot = (tracking_ms - kFirstShotMs) / kShotPeriodMs;
    for (int shot = std::max(0, latest_shot - 3);
         shot <= latest_shot; ++shot) {
        const int age =
            tracking_ms - (kFirstShotMs + shot * kShotPeriodMs);
        if (age < 0 || age > 360) continue;
        const double rise = std::clamp(age / 12.0, 0.0, 1.0);
        const double vertical_envelope =
            rise * std::exp(-static_cast<double>(age) / 165.0);
        const double horizontal_envelope = age < 75
            ? rise * (1.0 - static_cast<double>(age) / 75.0)
            : 0.0;
        const double horizontal_sign = (shot & 1) == 0 ? 1.0 : -1.0;
        result.x += horizontal_sign * 3.0 * horizontal_envelope;
        result.y -= 4.5 * vertical_envelope;
    }
    return result;
}

double horizontal_aim_bias_px(
    VisionDisturbanceProfile profile,
    int tracking_ms) noexcept {
    if (profile != VisionDisturbanceProfile::HorizontalAimBiasRecovery ||
        tracking_ms < 0) {
        return 0.0;
    }
    // Reproduce an initially wrong target/BodyLock point. The selector holds
    // the point to one side, then fresh observations correct it over several
    // vision frames. A healthy controller should correct once, not turn the
    // obsolete offset into a left-right remaining-work oscillation.
    constexpr int kHoldMs = 150;
    constexpr int kRecoveryMs = 90;
    constexpr double kBiasPx = 22.0;
    if (tracking_ms < kHoldMs) return kBiasPx;
    if (tracking_ms >= kHoldMs + kRecoveryMs) return 0.0;
    const double recovery =
        static_cast<double>(tracking_ms - kHoldMs) / kRecoveryMs;
    return kBiasPx * (1.0 - recovery);
}

struct PlantCommand {
    Vec2d final_stick;
};

}  // namespace

BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort,
    SimulationTraceObserver trace_observer,
    PlayerStrafeMode player_strafe_mode,
    PlayerVerticalMotionMode player_vertical_motion_mode) {
    if (!controller_step) {
        throw std::invalid_argument("controller callback is required");
    }
    if (script.config.duration_ms <= 0 || script.config.tick_ms <= 0 ||
        script.config.controller_substeps_per_plant_tick <= 0 ||
        script.config.controller_substeps_per_plant_tick > 8 ||
        (script.config.tick_ms != 1 &&
         script.config.controller_substeps_per_plant_tick != 1)) {
        throw std::invalid_argument(
            "sustained AimLab runner requires a positive controller tick");
    }

    std::vector<TargetResult> target_results;
    std::size_t target_index = 0;
    int gap_remaining_ms = 0;
    bool target_active = false;
    bool waiting_for_fixed_slot_end = false;
    int fixed_slot_elapsed_ms = 0;
    bool pending_fresh_miss = false;
    bool tracking = false;
    int tracking_ticks = 0;
    int target_elapsed_ms = 0;
    std::size_t observation_index = 0;
    std::uint64_t frame_id = 0;
    Vec2d error;
    Vec2d target_velocity;
    std::vector<Vec2d> target_error_history;
    Vec2d carried_observation;
    bool carried_body_box_available = false;
    double carried_body_box_x = 0.0;
    double carried_body_box_y = 0.0;
    double carried_body_box_width = 0.0;
    double carried_body_box_height = 0.0;
    Vec2d previous_camera_recoil_offset;
    double player_velocity_x_px_per_second = 0.0;
    int left_strafe_active_ms = 0;
    int left_strafe_reversals = 0;
    double max_abs_left_x = 0.0;
    double min_sampled_player_top_speed_px_per_second =
        std::numeric_limits<double>::infinity();
    double max_sampled_player_top_speed_px_per_second = 0.0;
    double max_abs_player_speed_px_per_second = 0.0;
    double player_vertical_offset_y_px = 0.0;
    int player_vertical_active_ms = 0;
    int player_slide_events = 0;
    int player_jump_events = 0;
    double max_abs_player_vertical_offset_px = 0.0;
    double max_abs_player_vertical_speed_px_per_second = 0.0;
    Vec2d obsolete_manual_direction;
    Vec2d obsolete_cross_axis;
    double obsolete_manual_magnitude = 0.0;
    int obsolete_persistence_ms = 0;
    int obsolete_crossed_at_ms = -1;
    bool v1_vertical_crossed = false;
    double v1_maximum_vertical_overshoot_px = 0.0;
    double v1_post_cross_error_area_px_ms = 0.0;
    double v1_post_cross_wrong_way_output_integral = 0.0;
    bool previous_bodylock_mode = false;
    bool saw_ads_mode = false;
    bool pending_ads_to_bodylock_transition = false;
    int first_assist_output_ms = -1;
    std::unique_ptr<TargetScorer> scorer;
    int initial_idle_remaining_ms = std::max(0, script.config.initial_idle_ms);
    std::deque<PlantCommand> delayed_controls(
        static_cast<std::size_t>(
            std::max(0, script.config.control_response_delay_ms)),
        PlantCommand{});
    bool pending_controller_vision = false;
    ControllerObservation pending_vision_input;
    ControllerObservation held_controller_input;
    ControllerStepResult held_output;
    int controller_updates = 0;
    auto spawn_target = [&] {
        if (target_index >= script.targets.size()) {
            throw std::runtime_error("scenario script exhausted before duration");
        }
        const TargetScript& target = script.targets[target_index];
        target_active = true;
        waiting_for_fixed_slot_end = false;
        fixed_slot_elapsed_ms = 0;
        tracking = false;
        tracking_ticks = 0;
        target_elapsed_ms = 0;
        observation_index = 0;
        error = target.initial_error_px;
        if (cohort == BenchmarkCohort::BodyLockFollow) {
            const double initial_distance = length(error);
            const double warm_distance = std::min(
                8.0, target.visible_radius_px * 0.5);
            if (initial_distance > 1e-9) {
                error.x *= warm_distance / initial_distance;
                error.y *= warm_distance / initial_distance;
            }
        }
        target_velocity = target.initial_velocity_px_per_second;
        target_error_history.clear();
        player_velocity_x_px_per_second = 0.0;
        player_vertical_offset_y_px = 0.0;
        if (player_strafe_mode == PlayerStrafeMode::FullReversal) {
            min_sampled_player_top_speed_px_per_second = std::min(
                min_sampled_player_top_speed_px_per_second,
                target.player_strafe.top_speed_px_per_second);
            max_sampled_player_top_speed_px_per_second = std::max(
                max_sampled_player_top_speed_px_per_second,
                target.player_strafe.top_speed_px_per_second);
        }
        carried_observation = error;
        carried_body_box_available = false;
        carried_body_box_x = 0.0;
        carried_body_box_y = 0.0;
        carried_body_box_width = 0.0;
        carried_body_box_height = 0.0;
        previous_camera_recoil_offset = {};
        obsolete_manual_direction = normalized_control_direction(error);
        const double initial_distance = length(error);
        obsolete_cross_axis = initial_distance > 1e-9
            ? Vec2d{error.x / initial_distance, error.y / initial_distance}
            : Vec2d{};
        obsolete_manual_magnitude =
            0.60 + static_cast<double>((target.id * 37u) % 41u) / 100.0;
        obsolete_persistence_ms =
            80 + static_cast<int>((target.id * 17u) % 61u);
        obsolete_crossed_at_ms = -1;
        v1_vertical_crossed = false;
        v1_maximum_vertical_overshoot_px = 0.0;
        v1_post_cross_error_area_px_ms = 0.0;
        v1_post_cross_wrong_way_output_integral = 0.0;
        previous_bodylock_mode = false;
        saw_ads_mode = false;
        pending_ads_to_bodylock_transition = false;
        first_assist_output_ms = -1;
        scorer = std::make_unique<TargetScorer>(target, script.config);
    };

    auto finish_target = [&] {
        TargetResult target_result = scorer->finish();
        target_result.first_assist_output_ms = first_assist_output_ms;
        if (manual_profile == ManualProfile::ObsoleteAfterCrossing) {
            target_result.maximum_vertical_overshoot_px =
                v1_maximum_vertical_overshoot_px;
            target_result.post_cross_error_area_px_ms =
                v1_post_cross_error_area_px_ms;
            target_result.post_cross_wrong_way_output_integral =
                v1_post_cross_wrong_way_output_integral;
        }
        target_results.push_back(std::move(target_result));
        scorer.reset();
        target_active = false;
        waiting_for_fixed_slot_end = false;
        tracking = false;
        ++target_index;
        gap_remaining_ms = script.config.inter_target_gap_ms;
        pending_fresh_miss = true;
    };

    for (int now_ms = 0; now_ms < script.config.duration_ms; ++now_ms) {
        if (!target_active && !waiting_for_fixed_slot_end &&
            gap_remaining_ms == 0 &&
            initial_idle_remaining_ms == 0 &&
            target_index < script.targets.size()) {
            spawn_target();
        }

        ControllerObservation input;
        bool vision_occluded = false;
        int player_motion_elapsed_ms = -1;
        input.now_ms = now_ms;
        input.now_seconds = static_cast<double>(now_ms) / 1000.0;
        if (target_active) {
            const TargetScript& target = script.targets[target_index];
            const Vec2d camera_recoil_offset =
                has_physical_camera_recoil(
                    script.config.vision_disturbance) && tracking
                ? physical_camera_recoil_offset(tracking_ticks)
                : Vec2d{};
            error.x +=
                camera_recoil_offset.x - previous_camera_recoil_offset.x;
            error.y +=
                camera_recoil_offset.y - previous_camera_recoil_offset.y;
            previous_camera_recoil_offset = camera_recoil_offset;
            if (target_error_history.size() <=
                static_cast<std::size_t>(target_elapsed_ms)) {
                target_error_history.push_back(error);
            }
            input.target_present = true;
            input.target_id = target.id;
            input.fire_action =
                script.config.vision_disturbance !=
                    VisionDisturbanceProfile::Off &&
                tracking && tracking_ticks >= 80;
            const bool dropout_decoy_scenario =
                script.config.vision_disturbance ==
                    VisionDisturbanceProfile::TargetDropoutDecoy;
            if (tracking || dropout_decoy_scenario) {
                const int occlusion_clock_ms = std::max(
                    0,
                    (dropout_decoy_scenario ? target_elapsed_ms : tracking_ticks) -
                        script.config.vision_result_delay_ms);
                vision_occluded = std::any_of(
                    target.vision_occlusion_bursts.begin(),
                    target.vision_occlusion_bursts.end(),
                    [&](const VisionOcclusionBurst& burst) {
                        return occlusion_clock_ms >= burst.tracking_offset_ms &&
                            occlusion_clock_ms <
                                burst.tracking_offset_ms + burst.duration_ms;
                    });
            }
            const auto observation_ready_at = [&](std::size_t index) {
                return target.observation_ready_at_ms.empty()
                    ? target.observation_at_ms[index] +
                        script.config.vision_result_delay_ms
                    : target.observation_ready_at_ms[index];
            };
            while (observation_index < target.observation_at_ms.size() &&
                   observation_ready_at(observation_index) < target_elapsed_ms) {
                ++observation_index;
            }
            if (observation_index < target.observation_at_ms.size() &&
                observation_ready_at(observation_index) == target_elapsed_ms) {
                const int capture_elapsed_ms =
                    target.observation_at_ms[observation_index];
                const Vec2d captured_error =
                    capture_elapsed_ms >= 0 &&
                    static_cast<std::size_t>(capture_elapsed_ms) <
                        target_error_history.size()
                    ? target_error_history[static_cast<std::size_t>(
                          capture_elapsed_ms)]
                    : error;
                const bool dropout_decoy = vision_occluded &&
                    script.config.vision_disturbance ==
                        VisionDisturbanceProfile::TargetDropoutDecoy;
                if (!vision_occluded || dropout_decoy) {
                    input.fresh_vision = true;
                    ++frame_id;
                }
                if (dropout_decoy) {
                    input.primary_candidate_visible = false;
                    input.decoy_candidate_present = true;
                    input.decoy_target_id = target.id + 1'000'000u;
                    const double separation = target.id % 2u == 0u
                        ? 200.0 : -200.0;
                    input.decoy_observed_error_px = {
                        captured_error.x + separation,
                        captured_error.y,
                    };
                } else if (!vision_occluded) {
                    carried_observation = {
                        captured_error.x +
                            target.observation_noise_px[observation_index].x +
                            horizontal_aim_bias_px(
                                script.config.vision_disturbance,
                                tracking_ticks),
                        captured_error.y +
                            target.observation_noise_px[observation_index].y,
                    };
                    const double aim_height_ratio =
                        target.body_height_px / target.body_width_px < 0.65
                        ? 0.40
                        : script.config.body_aim_height_ratio;
                    carried_body_box_available = true;
                    carried_body_box_x =
                        320.0 + carried_observation.x -
                        target.body_width_px * 0.5;
                    carried_body_box_y =
                        256.0 + carried_observation.y -
                        target.body_height_px * aim_height_ratio;
                    carried_body_box_width = target.body_width_px;
                    carried_body_box_height = target.body_height_px;
                    if (has_body_box_deformation(
                            script.config.vision_disturbance) &&
                        tracking) {
                        const Vec2d edge_deformation =
                            firing_body_box_edge_deformation(tracking_ticks);
                        carried_body_box_width += edge_deformation.x;
                        carried_body_box_height += edge_deformation.y;
                    }
                }
                ++observation_index;
            }
            if (input.fresh_vision) {
                const int capture_elapsed_ms =
                    target.observation_at_ms[observation_index - 1];
                input.capture_time_seconds =
                    static_cast<double>(
                        now_ms - (target_elapsed_ms - capture_elapsed_ms)) /
                    1000.0;
                input.ready_time_seconds =
                    static_cast<double>(now_ms) / 1000.0;
            }
            input.frame_id = frame_id;
            input.observed_error_px = carried_observation;
            input.has_body_box = carried_body_box_available;
            input.body_box_x = carried_body_box_x;
            input.body_box_y = carried_body_box_y;
            input.body_box_width = carried_body_box_width;
            input.body_box_height = carried_body_box_height;
            input.has_motion_anchor =
                (has_body_box_deformation(
                     script.config.vision_disturbance)) &&
                (frame_id % 13u) != 0u;
            const double anchor_phase =
                static_cast<double>(frame_id % 97u) * 0.37;
            input.motion_anchor_px = {
                320.0 +
                    (script.config.vision_disturbance ==
                         VisionDisturbanceProfile::
                             HorizontalAimBiasRecovery
                     ? carried_observation.x
                     : error.x) +
                    std::sin(anchor_phase) * 0.35,
                256.0 +
                    (script.config.vision_disturbance ==
                         VisionDisturbanceProfile::
                             HorizontalAimBiasRecovery
                     ? carried_observation.y
                     : error.y) +
                    std::cos(anchor_phase) * 0.35,
            };
            player_motion_elapsed_ms =
                cohort == BenchmarkCohort::BodyLockFollow
                ? (tracking ? tracking_ticks : -1)
                : target_elapsed_ms;
            if (player_motion_elapsed_ms >= 0) {
                input.left_x = left_strafe_input(
                    target.player_strafe, player_motion_elapsed_ms,
                    player_strafe_mode);
                if (input.left_x != 0.0) {
                    ++left_strafe_active_ms;
                    max_abs_left_x = std::max(
                        max_abs_left_x, std::fabs(input.left_x));
                }
                if (player_strafe_mode == PlayerStrafeMode::FullReversal &&
                    player_motion_elapsed_ms ==
                        target.player_strafe.reverse_ms) {
                    ++left_strafe_reversals;
                }
                PlayerVerticalMotionMode selected_vertical =
                    player_vertical_motion_mode;
                if (selected_vertical == PlayerVerticalMotionMode::Random) {
                    selected_vertical =
                        target.player_vertical.random_event ==
                            PlayerVerticalEvent::Slide
                        ? PlayerVerticalMotionMode::Slide
                        : PlayerVerticalMotionMode::Jump;
                }
                if (selected_vertical == PlayerVerticalMotionMode::Slide &&
                    player_motion_elapsed_ms ==
                        target.player_vertical.slide_onset_ms) {
                    ++player_slide_events;
                } else if (
                    selected_vertical == PlayerVerticalMotionMode::Jump &&
                    player_motion_elapsed_ms ==
                        target.player_vertical.jump_onset_ms) {
                    ++player_jump_events;
                }
            }
            if (manual_profile == ManualProfile::Mixed &&
                (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = mixed_manual_input(
                    target, target_elapsed_ms, error, target_velocity);
            } else if (manual_profile == ManualProfile::Scripted &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = scripted_manual_input(
                    target, target_elapsed_ms);
            } else if (manual_profile == ManualProfile::RuntimeProfile &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = runtime_manual_input(
                    script.config, target, cohort, target_elapsed_ms, error);
            } else if (manual_profile == ManualProfile::WrongThenCorrect &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = wrong_then_correct_manual_input(
                    target, target_elapsed_ms);
            } else if (manual_profile == ManualProfile::ArcRecovery &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = arc_recovery_manual_input(
                    target, target_elapsed_ms);
            } else if (manual_profile == ManualProfile::MicroCorrection &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = micro_correction_manual_input(
                    target, target_elapsed_ms);
            } else if (manual_profile == ManualProfile::RecoilController &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = recoil_controller_manual_input(
                    target, target_elapsed_ms);
            } else if (manual_profile == ManualProfile::RecoilFlail &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                input.manual_stick = recoil_flail_manual_input(
                    target, target_elapsed_ms);
            } else if (manual_profile == ManualProfile::ObsoleteAfterCrossing &&
                       (cohort != BenchmarkCohort::BodyLockFollow || tracking)) {
                if (obsolete_crossed_at_ms < 0 &&
                    dot(error, obsolete_cross_axis) <= 0.0) {
                    obsolete_crossed_at_ms = target_elapsed_ms;
                }
                if (obsolete_crossed_at_ms < 0 ||
                    target_elapsed_ms - obsolete_crossed_at_ms <
                        obsolete_persistence_ms) {
                    input.manual_stick = {
                        obsolete_manual_direction.x * obsolete_manual_magnitude,
                        obsolete_manual_direction.y * obsolete_manual_magnitude,
                    };
                }
            }
            input.manual_stick.x *= script.config.manual_input_scale;
            input.manual_stick.y *= script.config.manual_input_scale;
        } else if (pending_fresh_miss) {
            input.fresh_vision = true;
            input.frame_id = ++frame_id;
            pending_fresh_miss = false;
        }

        if (input.fresh_vision) {
            // Runtime publication and controller consumption are separate
            // clocks. Retain the newest mailbox value until a controller tick
            // instead of dropping frames that arrive between updates.
            pending_controller_vision = true;
            pending_vision_input = input;
        }

        const bool controller_updated = now_ms % script.config.tick_ms == 0;
        const bool controller_consumed_fresh_vision =
            controller_updated && pending_controller_vision;
        ControllerObservation controller_input = input;
        controller_input.fresh_vision = false;
        ControllerStepResult output = held_output;
        if (controller_updated) {
            if (pending_controller_vision) {
                const ControllerObservation& published = pending_vision_input;
                controller_input.fresh_vision = true;
                controller_input.target_present = published.target_present;
                controller_input.target_id = published.target_id;
                controller_input.primary_candidate_visible =
                    published.primary_candidate_visible;
                controller_input.decoy_candidate_present =
                    published.decoy_candidate_present;
                controller_input.decoy_target_id = published.decoy_target_id;
                controller_input.decoy_observed_error_px =
                    published.decoy_observed_error_px;
                controller_input.frame_id = published.frame_id;
                controller_input.capture_time_seconds =
                    published.capture_time_seconds;
                controller_input.ready_time_seconds =
                    published.ready_time_seconds;
                controller_input.observed_error_px =
                    published.observed_error_px;
                controller_input.has_body_box = published.has_body_box;
                controller_input.body_box_x = published.body_box_x;
                controller_input.body_box_y = published.body_box_y;
                controller_input.body_box_width = published.body_box_width;
                controller_input.body_box_height = published.body_box_height;
                controller_input.has_motion_anchor =
                    published.has_motion_anchor;
                controller_input.motion_anchor_px =
                    published.motion_anchor_px;
            }
            const int substeps =
                script.config.controller_substeps_per_plant_tick;
            for (int substep = 0; substep < substeps; ++substep) {
                controller_input.now_seconds =
                    (static_cast<double>(now_ms) +
                     static_cast<double>(substep) /
                         static_cast<double>(substeps)) /
                    1000.0;
                controller_input.fresh_vision =
                    pending_controller_vision && substep == 0;
                output = controller_step(controller_input);
                require_finite(output);
                held_controller_input = controller_input;
                held_output = output;
                ++controller_updates;
            }
            pending_controller_vision = false;
        } else {
            controller_input = held_controller_input;
            controller_input.now_ms = now_ms;
            controller_input.now_seconds =
                static_cast<double>(now_ms) / 1000.0;
            controller_input.fresh_vision = false;
        }

        SimulationTraceFrame trace_frame;
        trace_frame.absolute_ms = now_ms;
        trace_frame.target_elapsed_ms = target_active ? target_elapsed_ms : -1;
        trace_frame.target_active = target_active;
        trace_frame.controller_updated = controller_updated;
        trace_frame.fresh_vision = controller_consumed_fresh_vision;
        trace_frame.vision_occluded = vision_occluded;
        trace_frame.target_id = input.target_id;
        trace_frame.input = controller_input;
        if (target_active) {
            trace_frame.motion = script.targets[target_index].motion;
            trace_frame.true_error_before_px = error;
        }

        const bool output_matches_active_target = target_active &&
            output.controller_target_id == script.targets[target_index].id;
        const bool active_target_bodylock =
            output_matches_active_target && output.bodylock_mode;
        if (controller_updated && output_matches_active_target &&
            first_assist_output_ms < 0 &&
            length(output.requested_assist_stick) > 0.001) {
            first_assist_output_ms = target_elapsed_ms;
        }
        if (output_matches_active_target &&
            cohort == BenchmarkCohort::AdsAcquire) {
            if (!output.bodylock_mode) saw_ads_mode = true;
            if (saw_ads_mode && !previous_bodylock_mode &&
                active_target_bodylock) {
                pending_ads_to_bodylock_transition = true;
            }
        }
        const PlantCommand delivered_command{output.final_stick};
        PlantCommand applied_command = delivered_command;
        if (!delayed_controls.empty()) {
            delayed_controls.push_back(delivered_command);
            applied_command = delayed_controls.front();
            delayed_controls.pop_front();
        }
        const Vec2d plant_control = applied_command.final_stick;
        const auto normalized_camera_response = forward_aim_response_curve(
            {static_cast<float>(plant_control.x),
             static_cast<float>(plant_control.y)},
            script.config.camera_response_curve);
        trace_frame.output = output;
        if (target_active) {
            const TargetScript& target = script.targets[target_index];
            if (cohort == BenchmarkCohort::AdsAcquire &&
                !previous_bodylock_mode && active_target_bodylock) {
                scorer->mark_ads_to_bodylock_handoff(
                    target_elapsed_ms,
                    error,
                    output.radial_closing_velocity_px_per_sec);
                pending_ads_to_bodylock_transition = true;
            }
            if (cohort != BenchmarkCohort::BodyLockFollow || tracking) {
                const int motion_elapsed_ms =
                    cohort == BenchmarkCohort::BodyLockFollow
                    ? tracking_ticks : target_elapsed_ms;
                advance_target(target, motion_elapsed_ms, 0.001, error, target_velocity);
            }
            const double time_constant_seconds =
                target.player_strafe.time_constant_ms / 1000.0;
            const double desired_player_velocity =
                input.left_x * target.player_strafe.top_speed_px_per_second;
            const double player_alpha = time_constant_seconds > 0.0
                ? 1.0 - std::exp(-0.001 / time_constant_seconds)
                : 1.0;
            player_velocity_x_px_per_second += player_alpha *
                (desired_player_velocity - player_velocity_x_px_per_second);
            error.x -= player_velocity_x_px_per_second * 0.001;
            max_abs_player_speed_px_per_second = std::max(
                max_abs_player_speed_px_per_second,
                std::fabs(player_velocity_x_px_per_second));
            const double next_vertical_offset_y_px =
                player_vertical_error_offset_y_px(
                    target.player_vertical,
                    player_motion_elapsed_ms,
                    player_vertical_motion_mode);
            const double vertical_delta =
                next_vertical_offset_y_px - player_vertical_offset_y_px;
            error.y += vertical_delta;
            if (next_vertical_offset_y_px != 0.0) {
                ++player_vertical_active_ms;
            }
            max_abs_player_vertical_offset_px = std::max(
                max_abs_player_vertical_offset_px,
                std::fabs(next_vertical_offset_y_px));
            max_abs_player_vertical_speed_px_per_second = std::max(
                max_abs_player_vertical_speed_px_per_second,
                std::fabs(vertical_delta) * 1000.0);
            player_vertical_offset_y_px = next_vertical_offset_y_px;
            const double response =
                script.config.camera_response_px_per_stick_second *
                script.config.sensitivity_multiplier *
                (target_active
                    ? aim_slowdown_multiplier(
                        length(error), target.visible_radius_px, script.config)
                    : 1.0);
            error.x -= normalized_camera_response.x * response * 0.001;
            error.y += normalized_camera_response.y * response * 0.001;
            if (manual_profile == ManualProfile::ObsoleteAfterCrossing) {
                if (!v1_vertical_crossed && error.y >= 0.0) {
                    v1_vertical_crossed = true;
                }
                if (v1_vertical_crossed) {
                    const double excursion = std::max(0.0, error.y);
                    v1_maximum_vertical_overshoot_px = std::max(
                        v1_maximum_vertical_overshoot_px, excursion);
                    v1_post_cross_error_area_px_ms += excursion;
                    v1_post_cross_wrong_way_output_integral += std::max(
                        0.0, output.final_stick.y);
                }
            }
            trace_frame.true_error_after_px = error;
            trace_frame.target_velocity_px_per_second = target_velocity;
            trace_frame.player_velocity_x_px_per_second =
                player_velocity_x_px_per_second;
            trace_frame.player_vertical_offset_y_px =
                player_vertical_offset_y_px;
            trace_frame.player_vertical_velocity_y_px_per_second =
                vertical_delta * 1000.0;

            if (cohort == BenchmarkCohort::BodyLockFollow && !tracking) {
                if (active_target_bodylock) {
                    scorer->mark_acquired(target_elapsed_ms);
                    scorer->mark_bodylock_entered(target_elapsed_ms);
                    tracking = true;
                    tracking_ticks = 0;
                } else if (target_elapsed_ms + 1 >=
                           script.config.bodylock_entry_timeout_ms) {
                    scorer->mark_bodylock_entry_failed();
                    if (script.config.fixed_target_slot_ms > 0) {
                        target_active = false;
                        tracking = false;
                        waiting_for_fixed_slot_end = true;
                        pending_fresh_miss = true;
                    } else {
                        finish_target();
                    }
                }
            }

            if (target_active && tracking) {
                ScoreFrame frame;
                frame.absolute_ms = now_ms;
                frame.target_elapsed_ms = target_elapsed_ms;
                frame.in_tracking_window = true;
                frame.target_observed =
                    output_matches_active_target && output.target_observed;
                frame.vision_occluded = trace_frame.vision_occluded;
                frame.fresh_vision = trace_frame.fresh_vision;
                frame.tracker_reliable =
                    output_matches_active_target && output.tracker_reliable;
                frame.manual_escape =
                    length(controller_input.manual_stick) >= 0.45;
                frame.bodylock_mode = active_target_bodylock;
                frame.target_id = target.id;
                frame.error_px = error;
                frame.target_velocity_px_per_second = target_velocity;
                frame.manual_stick = controller_input.manual_stick;
                frame.requested_assist_stick = output.requested_assist_stick;
                frame.shaped_assist_stick = output.shaped_assist_stick;
                frame.final_stick = output.final_stick;
                frame.pre_recoil_stick = output.pre_recoil_stick;
                frame.has_pre_recoil_stick = output.has_pre_recoil_stick;
                frame.predicted_terminal_error_px =
                    output.predicted_terminal_error_px;
                frame.radial_closing_velocity_px_per_sec =
                    output.radial_closing_velocity_px_per_sec;
                frame.ads_to_bodylock_transition =
                    pending_ads_to_bodylock_transition;
                scorer->add_frame(frame);
                if (frame.ads_to_bodylock_transition) {
                    pending_ads_to_bodylock_transition = false;
                }
                ++tracking_ticks;
                if (tracking_ticks >= script.config.tracking_window_ms) {
                    if (script.config.fixed_target_slot_ms > 0) {
                        target_active = false;
                        tracking = false;
                        waiting_for_fixed_slot_end = true;
                        pending_fresh_miss = true;
                    } else {
                        finish_target();
                    }
                }
            } else if (target_active && cohort == BenchmarkCohort::AdsAcquire &&
                       length(error) < target.visible_radius_px) {
                scorer->mark_acquired(target_elapsed_ms + 1);
                tracking = true;
                tracking_ticks = 0;
            } else if (target_active && cohort == BenchmarkCohort::AdsAcquire &&
                       target_elapsed_ms + 1 >=
                           script.config.ads_execution_timeout_ms) {
                scorer->mark_timed_out();
                if (script.config.fixed_target_slot_ms > 0) {
                    target_active = false;
                    tracking = false;
                    waiting_for_fixed_slot_end = true;
                    pending_fresh_miss = true;
                } else {
                    finish_target();
                }
            }
            if (target_active) ++target_elapsed_ms;
        } else {
            if (initial_idle_remaining_ms > 0) {
                --initial_idle_remaining_ms;
            } else if (gap_remaining_ms > 0) {
                --gap_remaining_ms;
            }
        }
        if (script.config.fixed_target_slot_ms > 0 && scorer) {
            ++fixed_slot_elapsed_ms;
            if (fixed_slot_elapsed_ms >= script.config.fixed_target_slot_ms) {
                finish_target();
            }
        }
        previous_bodylock_mode =
            target_active && output_matches_active_target &&
            output.bodylock_mode;
        if (trace_observer) trace_observer(trace_frame);
    }

    if (scorer) {
        TargetResult target_result = scorer->finish();
        target_result.first_assist_output_ms = first_assist_output_ms;
        if (manual_profile == ManualProfile::ObsoleteAfterCrossing) {
            target_result.maximum_vertical_overshoot_px =
                v1_maximum_vertical_overshoot_px;
            target_result.post_cross_error_area_px_ms =
                v1_post_cross_error_area_px_ms;
            target_result.post_cross_wrong_way_output_integral =
                v1_post_cross_wrong_way_output_integral;
        }
        target_results.push_back(std::move(target_result));
    }

    BenchmarkResult result = aggregate(
        script.seed, script.hash, std::move(target_results));
    result.manual_profile = manual_profile;
    result.cohort = cohort;
    result.player_strafe_mode = player_strafe_mode;
    result.player_vertical_motion_mode = player_vertical_motion_mode;
    result.ticks = script.config.duration_ms;
    result.controller_updates = controller_updates;
    result.left_strafe_active_ms = left_strafe_active_ms;
    result.left_strafe_reversals = left_strafe_reversals;
    result.max_abs_left_x = max_abs_left_x;
    result.min_sampled_player_top_speed_px_per_second =
        std::isfinite(min_sampled_player_top_speed_px_per_second)
        ? min_sampled_player_top_speed_px_per_second : 0.0;
    result.max_sampled_player_top_speed_px_per_second =
        max_sampled_player_top_speed_px_per_second;
    result.max_abs_player_speed_px_per_second =
        max_abs_player_speed_px_per_second;
    result.player_vertical_active_ms = player_vertical_active_ms;
    result.player_slide_events = player_slide_events;
    result.player_jump_events = player_jump_events;
    result.max_abs_player_vertical_offset_px =
        max_abs_player_vertical_offset_px;
    result.max_abs_player_vertical_speed_px_per_second =
        max_abs_player_vertical_speed_px_per_second;
    return result;
}

}  // namespace controller_native::sustained_aimlab
