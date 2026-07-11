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

} // namespace controller_native::vertical_defect
