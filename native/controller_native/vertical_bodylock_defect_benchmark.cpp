#include "vertical_bodylock_defect_benchmark.h"

#include "native_gamepad_controller.h"
#include "runtime_config.h"
#include "vision_native/target_selector.h"

#include <algorithm>
#include <cmath>

namespace controller_native::vertical_defect {
namespace {

constexpr double kCenterY = 256.0;
constexpr double kDt = 0.001;
constexpr double kReticleSpeed = 900.0;

double distance_to_interval(double value, double top, double bottom) {
    if (value < top) return top - value;
    if (value > bottom) return value - bottom;
    return 0.0;
}

vision_native::VisionResult select_box(
    float left, float top, float right, float bottom, float cue_y) {
    vision_native::VisionTargetSelector selector(640, 512);
    vision_native::DetectionBatch batch;
    batch.frame_width = 640;
    batch.frame_height = 512;
    vision_native::Detection detection;
    detection.x1 = left;
    detection.y1 = top;
    detection.x2 = right;
    detection.y2 = bottom;
    detection.conf = 0.95f;
    detection.color_bonus = 0.25f;
    detection.has_cue_point = true;
    detection.cue_x = (left + right) * 0.5f;
    detection.cue_y = cue_y;
    detection.cue_score = 0.95f;
    batch.detections.push_back(detection);
    vision_native::VisionResult result;
    for (int frame = 0; frame < 4; ++frame) {
        batch.frame_id = static_cast<std::uint64_t>(frame + 1);
        result = selector.select(batch);
    }
    return result;
}

PhysicalGamepadState aiming(float manual_y) {
    PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.right_y = manual_y;
    return state;
}

NativeControllerVisionState vision_state(
    double now,
    double reticle_y,
    double target_y,
    double box_top,
    double box_bottom,
    bool visible) {
    NativeControllerVisionState state;
    if (!visible) {
        state.observed_at_seconds = now;
        return state;
    }
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = false;
    state.target_tier = "observed_strong";
    state.screen_center_x = 320.0f;
    state.screen_center_y = static_cast<float>(kCenterY);
    state.target_x = 320.0f;
    state.target_y = static_cast<float>(kCenterY + target_y - reticle_y);
    state.dx = 0.0f;
    state.dy = state.target_y - state.screen_center_y;
    state.has_body_box = true;
    state.body_x1 = 220.0f;
    state.body_x2 = 420.0f;
    state.body_y1 = static_cast<float>(kCenterY + box_top - reticle_y);
    state.body_y2 = static_cast<float>(kCenterY + box_bottom - reticle_y);
    state.observed_at_seconds = now;
    return state;
}

void record_event(
    Metrics& metrics,
    int frame,
    double reticle_y,
    double target_y,
    double visible_top,
    double visible_bottom,
    float manual_y,
    const NativeGamepadController& controller) {
    const auto& components = controller.last_output_components();
    FrameEvent event;
    event.frame = frame;
    event.reticle_y = reticle_y;
    event.target_y = target_y;
    event.visible_body_top = visible_top;
    event.visible_body_bottom = visible_bottom;
    event.manual_y = manual_y;
    event.ai_y = components.ai_aim_stick.y;
    event.final_y = components.final_stick.y;
    event.vision_authority = controller.last_frame_vision_state().aim_authority;
    event.mode = controller.last_ai_aim_mode();
    metrics.events.push_back(event);
}

Metrics run_air_lock(bool moving_stairs) {
    Metrics metrics;
    metrics.name = moving_stairs ? "stairs_low_target_manual_escape" : "prone_air_lock_manual_escape";
    metrics.detection_top = 180.0;
    metrics.detection_bottom = 300.0;
    metrics.visible_body_top = 252.0;
    metrics.visible_body_bottom = 300.0;
    metrics.cue_y = 170.0;
    const auto selected = select_box(210.0f, 180.0f, 430.0f, 300.0f, 170.0f);
    metrics.selector_target_y = selected.target_y;
    metrics.bodylock_target_y = metrics.detection_top +
        ((metrics.detection_bottom - metrics.detection_top) * 0.40);
    metrics.target_distance_to_body_px = distance_to_interval(
        metrics.selector_target_y, metrics.visible_body_top, metrics.visible_body_bottom);
    metrics.target_outside_visible_body = metrics.target_distance_to_body_px > 0.0;

    double now = 1.0;
    GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    NativeGamepadController controller(config, [&now] { return now; });
    double reticle_y = metrics.detection_top +
        ((metrics.detection_bottom - metrics.detection_top) * 0.50);
    for (int frame = 0; frame < 180; ++frame) {
        now += kDt;
        const double shift = moving_stairs ? std::min(42.0, frame * 0.30) : 0.0;
        const double box_top = metrics.detection_top + shift;
        const double box_bottom = metrics.detection_bottom + shift;
        const double visible_top = metrics.visible_body_top + shift;
        const double visible_bottom = metrics.visible_body_bottom + shift;
        const double target_y = metrics.selector_target_y + shift;
        controller.submit_vision_state(vision_state(
            now, reticle_y, target_y, box_top, box_bottom, true));
        constexpr float manual_y = -0.38f;
        controller.build_output(aiming(manual_y));
        const auto& components = controller.last_output_components();
        if (metrics.manual_escape_frame < 0 &&
            components.ai_aim_stick.y > 0.02f && manual_y < 0.0f)
            ++metrics.ai_opposes_recovery_frames;
        reticle_y += -static_cast<double>(components.final_stick.y) * kReticleSpeed * kDt;
        if (reticle_y < visible_top || reticle_y > visible_bottom)
            ++metrics.outside_body_frames;
        if (metrics.manual_escape_frame < 0 && reticle_y >= visible_top && reticle_y <= visible_bottom)
            metrics.manual_escape_frame = frame;
        if (frame < 8 || frame % 20 == 0 || frame == metrics.manual_escape_frame)
            record_event(metrics, frame, reticle_y, target_y, visible_top, visible_bottom, manual_y, controller);
    }
    metrics.defect_reproduced = metrics.target_outside_visible_body &&
        (metrics.ai_opposes_recovery_frames >= 20 || metrics.manual_escape_frame < 0);
    return metrics;
}

NativeControllerVisionState horizontal_vision_state(
    double now,
    double,
    double controller_target_x) {
    NativeControllerVisionState state;
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = false;
    state.target_tier = "observed_strong";
    state.screen_center_x = 320.0f;
    state.screen_center_y = 256.0f;
    state.target_x = static_cast<float>(320.0 + controller_target_x);
    state.target_y = 256.0f;
    state.dx = state.target_x - state.screen_center_x;
    state.dy = 0.0f;
    state.has_body_box = true;
    state.body_x1 = state.target_x - 55.0f;
    state.body_x2 = state.target_x + 55.0f;
    state.body_y1 = 176.0f;
    state.body_y2 = 336.0f;
    state.observed_at_seconds = now;
    return state;
}

PhysicalGamepadState horizontal_aiming(float manual_x) {
    PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.right_x = manual_x;
    return state;
}

enum class HorizontalScenario { Takeover, Cooperative, ShortNoise, CrossingContinuity };

ManualTakeoverMetrics run_horizontal_scenario(
    HorizontalScenario scenario,
    bool takeover_enabled = true) {
    ManualTakeoverMetrics metrics;
    metrics.name = scenario == HorizontalScenario::Takeover
        ? "single_visible_target_identity_churn_manual_takeover"
        : scenario == HorizontalScenario::Cooperative
            ? "single_visible_target_cooperative_tracking"
            : scenario == HorizontalScenario::ShortNoise
                ? "single_visible_target_short_manual_noise"
                : "bodylock_repeated_error_crossing_continuity";
    double now = 4.0;
    GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    config.ai_aim.body_lock_manual_takeover_enabled = takeover_enabled;
    NativeGamepadController controller(config, [&now] { return now; });
    double reticle_x = 0.0;
    double requested_integral = 0.0;
    double preserved_integral = 0.0;
    int continuous_reversal = 0;
    int max_continuous_reversal = 0;
    int stall_frames = 0;
    std::string previous_mode;

    for (int frame = 0; frame < 360; ++frame) {
        now += kDt;
        float manual_x = 0.0f;
        double controller_target_x = 28.0;
        if (frame >= 40) {
            if (scenario == HorizontalScenario::Takeover) {
                const float ramp = std::min(1.0f, static_cast<float>(frame - 40) / 60.0f);
                manual_x = -0.14f + ((-0.38f + 0.14f) * ramp);
                // The only visible target is to the left, but association briefly
                // carries the previous +X bodylock correction before reacquiring it.
                if (frame >= 210) controller_target_x = -36.0;
            } else if (scenario == HorizontalScenario::Cooperative) {
                manual_x = 0.35f;
            } else if (scenario == HorizontalScenario::ShortNoise) {
                manual_x = frame < 60 ? -0.35f : 0.0f;
            } else {
                manual_x = 0.92f;
                controller_target_x = ((frame / 24) % 2 == 0) ? 8.0 : -8.0;
            }
        }
        controller.submit_vision_state(horizontal_vision_state(
            now, reticle_x, controller_target_x));
        controller.build_output(horizontal_aiming(manual_x));
        const auto& components = controller.last_output_components();
        const float pre_recoil_x = components.before_recoil_stick.x;
        const std::string mode = controller.last_ai_aim_mode();
        if (!previous_mode.empty() && mode != previous_mode) ++metrics.mode_transitions;
        previous_mode = mode;
        if (mode == "body_lock") ++metrics.body_lock_frames;

        if (scenario == HorizontalScenario::Takeover && frame >= 40) {
            const float requested = std::fabs(manual_x);
            const float projected = -pre_recoil_x;
            requested_integral += requested * kDt;
            preserved_integral += projected * kDt;
            metrics.old_target_resistance_integral +=
                std::max(0.0f, components.ai_aim_stick.x) * kDt;
            if (requested >= 0.25f && manual_x * pre_recoil_x < 0.0f) {
                ++metrics.manual_reversal_frames;
                ++continuous_reversal;
                max_continuous_reversal = std::max(max_continuous_reversal, continuous_reversal);
            } else {
                continuous_reversal = 0;
            }
            if (requested >= 0.25f && projected <= 0.05f) ++stall_frames;
            if (metrics.manual_takeover_latency_ms < 0.0 &&
                requested >= 0.25f && projected >= requested * 0.50f) {
                metrics.manual_takeover_latency_ms = static_cast<double>(frame - 40);
            }
        }
        if (scenario == HorizontalScenario::CrossingContinuity &&
            frame >= 170 && mode == "body_lock") {
            metrics.min_committed_output = std::min(
                metrics.min_committed_output,
                static_cast<double>(pre_recoil_x));
            if (pre_recoil_x < 0.80f) ++metrics.downstream_brake_frames;
            if (components.ads_carry_brake_active) ++metrics.ads_carry_brake_frames;
        }
        reticle_x += static_cast<double>(pre_recoil_x) * kReticleSpeed * kDt;
    }
    metrics.manual_direction_preservation_ratio = requested_integral > 0.0
        ? preserved_integral / requested_integral : 0.0;
    metrics.max_continuous_reversal_ms = static_cast<double>(max_continuous_reversal);
    metrics.manual_stall_ms = static_cast<double>(stall_frames);
    metrics.cooperative_assist_preserved = scenario == HorizontalScenario::Cooperative &&
        reticle_x > 30.0 && metrics.body_lock_frames > 0;
    metrics.short_noise_kept_body_lock = scenario == HorizontalScenario::ShortNoise &&
        metrics.body_lock_frames >= 300;
    metrics.defect_reproduced = scenario == HorizontalScenario::Takeover &&
        (metrics.manual_direction_preservation_ratio < 0.75 ||
         metrics.max_continuous_reversal_ms > 20.0 || metrics.manual_stall_ms > 40.0 ||
         metrics.manual_takeover_latency_ms < 0.0 || metrics.manual_takeover_latency_ms > 60.0);
    return metrics;
}

} // namespace

Metrics run_prone_air_lock() { return run_air_lock(false); }
Metrics run_stairs_air_lock() { return run_air_lock(true); }

Metrics run_cooperative_overshoot_occlusion() {
    Metrics metrics;
    metrics.name = "cooperative_upward_overshoot_occlusion";
    metrics.detection_top = 170.0;
    metrics.detection_bottom = 290.0;
    metrics.visible_body_top = 205.0;
    metrics.visible_body_bottom = 245.0;
    metrics.cue_y = 160.0;
    metrics.selector_target_y = select_box(270.0f, 170.0f, 370.0f, 290.0f, 160.0f).target_y;
    metrics.bodylock_target_y = 218.0;

    double now = 2.0;
    GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    NativeGamepadController controller(config, [&now] { return now; });
    double reticle_y = 285.0;
    bool crossed = false;
    for (int frame = 0; frame < 240; ++frame) {
        now += kDt;
        const bool dropout = frame >= 70 && frame < 150;
        controller.submit_vision_state(vision_state(
            now, reticle_y, metrics.selector_target_y,
            metrics.detection_top, metrics.detection_bottom, !dropout));
        const float manual_y = frame < 70 ? 0.55f : (frame < 160 ? 1.0f : -0.45f);
        controller.build_output(aiming(manual_y));
        const auto& components = controller.last_output_components();
        const double previous = reticle_y;
        reticle_y += -static_cast<double>(components.final_stick.y) * kReticleSpeed * kDt;
        if (!crossed && reticle_y <= metrics.selector_target_y) crossed = true;
        if (crossed) {
            metrics.max_overshoot_px = std::max(
                metrics.max_overshoot_px,
                std::max(0.0, metrics.visible_body_top - reticle_y));
            if (reticle_y < metrics.visible_body_top || reticle_y > metrics.visible_body_bottom)
                ++metrics.outside_body_frames;
        }
        if (frame >= 160 && components.ai_aim_stick.y > 0.02f)
            ++metrics.ai_opposes_recovery_frames;
        if (frame >= 160 && metrics.recovery_start_frame < 0 && reticle_y > previous)
            metrics.recovery_start_frame = frame;
        if (frame >= 150 && metrics.reacquire_frame < 0 &&
            controller.last_frame_vision_state().aim_authority)
            metrics.reacquire_frame = frame;
        if (frame < 8 || frame == 69 || frame == 70 || frame == 149 || frame == 150 ||
            frame == 159 || frame == 160 || frame % 20 == 0)
            record_event(metrics, frame, reticle_y, metrics.selector_target_y,
                metrics.visible_body_top, metrics.visible_body_bottom, manual_y, controller);
    }
    metrics.defect_reproduced = metrics.max_overshoot_px > 2.0 &&
        (metrics.ai_opposes_recovery_frames > 0 || metrics.recovery_start_frame < 0 ||
         metrics.recovery_start_frame > 180);
    return metrics;
}

Metrics run_large_vertical_cooperative_acquisition() {
    Metrics metrics;
    metrics.name = "large_vertical_cooperative_acquisition";
    constexpr double target_y = 20.0;
    constexpr double visible_top = target_y - 24.0;
    constexpr double visible_bottom = target_y + 36.0;
    double reticle_y = 260.0;
    metrics.target_distance_to_body_px = std::fabs(reticle_y - target_y);
    metrics.visible_body_top = visible_top;
    metrics.visible_body_bottom = visible_bottom;
    metrics.selector_target_y = target_y;

    double now = 6.0;
    GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 120;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    NativeGamepadController controller(config, [&now] { return now; });
    int crossing_frame = -1;

    for (int frame = 0; frame < 520; ++frame) {
        now += kDt;
        controller.submit_vision_state(vision_state(
            now, reticle_y, target_y, visible_top, visible_bottom, true));
        const bool late_upward_commit =
            crossing_frame < 0 || frame < crossing_frame + 30;
        const float manual_y = late_upward_commit ? 0.62f : -0.38f;
        controller.build_output(aiming(manual_y));
        const auto& components = controller.last_output_components();
        const double previous = reticle_y;
        reticle_y += -static_cast<double>(components.final_stick.y) *
            kReticleSpeed * kDt;
        if (crossing_frame < 0 && previous > target_y && reticle_y <= target_y) {
            crossing_frame = frame;
        }
        if (crossing_frame >= 0) {
            metrics.max_overshoot_px = std::max(
                metrics.max_overshoot_px, std::max(0.0, target_y - reticle_y));
            if (reticle_y < visible_top || reticle_y > visible_bottom) {
                ++metrics.outside_body_frames;
            }
            if (components.final_stick.y > 0.02f) {
                ++metrics.ai_opposes_recovery_frames;
            }
            if (!late_upward_commit && metrics.recovery_start_frame < 0 &&
                reticle_y > previous) {
                metrics.recovery_start_frame = frame;
            }
        }
        if (frame < 5 || frame % 40 == 0 || frame == crossing_frame ||
            frame == crossing_frame + 30) {
            record_event(metrics, frame, reticle_y, target_y,
                visible_top, visible_bottom, manual_y, controller);
        }
    }
    metrics.reacquire_frame = crossing_frame;
    metrics.defect_reproduced = crossing_frame >= 0 &&
        (metrics.max_overshoot_px > 20.0 ||
         metrics.ai_opposes_recovery_frames > 20);
    return metrics;
}

Metrics run_slide_recoil_dropout() {
    Metrics metrics;
    metrics.name = "slide_down_forward_recoil_dropout";
    double now = 7.0;
    double target_y = 230.0;
    double last_observed_target_y = target_y;
    double reticle_y = target_y;
    metrics.selector_target_y = target_y;

    GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    NativeGamepadController controller(config, [&now] { return now; });

    for (int frame = 0; frame < 300; ++frame) {
        now += kDt;
        const bool sliding = frame >= 40 && frame < 180;
        if (sliding) target_y += 0.45;
        const double slide_progress = std::clamp(
            static_cast<double>(frame - 40) / 140.0, 0.0, 1.0);
        const double half_height = 70.0 - slide_progress * 35.0;
        const double visible_top = target_y - half_height;
        const double visible_bottom = target_y + half_height;
        const bool dropout = frame >= 60 && frame < 140;
        if (dropout) ++metrics.outside_body_frames;
        if (!dropout) {
            controller.submit_vision_state(vision_state(
                now, reticle_y, target_y, visible_top, visible_bottom, true));
            last_observed_target_y = target_y;
        }
        constexpr float manual_y = 0.0f;
        controller.build_output(aiming(manual_y));
        const auto& components = controller.last_output_components();
        reticle_y += -static_cast<double>(components.final_stick.y) *
            kReticleSpeed * kDt;
        if (frame >= 40 && frame < 120) {
            reticle_y -= 0.30;
        }
        const double relative_error = std::fabs(target_y - reticle_y);
        metrics.max_overshoot_px = std::max(
            metrics.max_overshoot_px, relative_error);
        if (dropout && target_y - last_observed_target_y > 5.0) {
            ++metrics.ai_opposes_recovery_frames;
        }
        if (frame >= 140 && metrics.reacquire_frame < 0 &&
            controller.last_frame_vision_state().aim_authority) {
            metrics.reacquire_frame = frame;
        }
        if (frame < 5 || frame == 39 || frame == 40 || frame == 59 ||
            frame == 60 || frame == 139 || frame == 140 || frame % 40 == 0) {
            record_event(metrics, frame, reticle_y, target_y,
                visible_top, visible_bottom, manual_y, controller);
        }
    }
    metrics.defect_reproduced = metrics.outside_body_frames == 80 &&
        metrics.ai_opposes_recovery_frames >= 40 &&
        metrics.reacquire_frame >= 140 && metrics.max_overshoot_px > 35.0;
    return metrics;
}

Metrics run_player_pov_jump() {
    Metrics metrics;
    metrics.name = "player_pov_jump_vertical_reversal";
    constexpr double base_target_y = 230.0;
    constexpr double jump_amplitude_px = 80.0;
    constexpr int jump_start_frame = 40;
    constexpr int jump_duration_frames = 480;
    constexpr int jump_apex_frame =
        jump_start_frame + jump_duration_frames / 2;
    metrics.target_distance_to_body_px = jump_amplitude_px;

    double now = 8.0;
    double reticle_y = base_target_y;
    double apparent_target_y = base_target_y;
    GamepadRuntimeConfig config;
    config.ai_aim.ads_snap_window_ms = 0;
    config.recoil.enabled = false;
    config.auto_fire.require_aim_ready = false;
    config.aim_assist_dynamics.enabled = false;
    NativeGamepadController controller(config, [&now] { return now; });

    for (int frame = 0; frame < 640; ++frame) {
        now += kDt;
        const int jump_elapsed = frame - jump_start_frame;
        double jump_offset = 0.0;
        if (jump_elapsed >= 0 && jump_elapsed <= jump_duration_frames) {
            const double phase = static_cast<double>(jump_elapsed) /
                static_cast<double>(jump_duration_frames);
            jump_offset = jump_amplitude_px *
                std::sin(3.14159265358979323846 * phase);
        }
        apparent_target_y = base_target_y + jump_offset;
        const double visible_top = apparent_target_y - 70.0;
        const double visible_bottom = apparent_target_y + 70.0;
        if (frame % 10 == 0) {
            controller.submit_vision_state(vision_state(
                now, reticle_y, apparent_target_y,
                visible_top, visible_bottom, true));
        }
        constexpr float manual_y = 0.0f;
        controller.build_output(aiming(manual_y));
        const auto& components = controller.last_output_components();
        reticle_y += -static_cast<double>(components.final_stick.y) *
            kReticleSpeed * kDt;

        const double relative_error = std::fabs(apparent_target_y - reticle_y);
        metrics.max_overshoot_px = std::max(
            metrics.max_overshoot_px, relative_error);
        if (relative_error > 24.0) ++metrics.outside_body_frames;
        if (frame > jump_apex_frame &&
            apparent_target_y < reticle_y &&
            components.final_stick.y < -0.02f) {
            ++metrics.ai_opposes_recovery_frames;
        }
        if (frame > jump_apex_frame && metrics.recovery_start_frame < 0 &&
            components.final_stick.y > 0.02f) {
            metrics.recovery_start_frame = frame;
        }
        if (frame < 5 || frame == jump_start_frame ||
            frame == jump_apex_frame || frame == jump_start_frame + jump_duration_frames ||
            frame % 80 == 0) {
            record_event(metrics, frame, reticle_y, apparent_target_y,
                visible_top, visible_bottom, manual_y, controller);
        }
    }
    metrics.selector_target_y = apparent_target_y;
    metrics.reacquire_frame = jump_start_frame + jump_duration_frames;
    metrics.defect_reproduced = metrics.max_overshoot_px > 20.0 ||
        metrics.ai_opposes_recovery_frames > 10 ||
        metrics.outside_body_frames > 40;
    return metrics;
}

ManualTakeoverMetrics run_single_target_manual_takeover() {
    return run_horizontal_scenario(HorizontalScenario::Takeover);
}

ManualTakeoverMetrics run_single_target_manual_takeover_legacy() {
    auto metrics = run_horizontal_scenario(HorizontalScenario::Takeover, false);
    metrics.name = "single_visible_target_identity_churn_manual_takeover_legacy";
    return metrics;
}

ManualTakeoverMetrics run_single_target_cooperative_tracking() {
    return run_horizontal_scenario(HorizontalScenario::Cooperative);
}

ManualTakeoverMetrics run_single_target_short_noise() {
    return run_horizontal_scenario(HorizontalScenario::ShortNoise);
}

ManualTakeoverMetrics run_bodylock_crossing_continuity() {
    return run_horizontal_scenario(HorizontalScenario::CrossingContinuity);
}

} // namespace controller_native::vertical_defect
