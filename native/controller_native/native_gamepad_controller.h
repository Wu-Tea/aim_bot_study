#pragma once

#include "aim_assist_dynamics.h"
#include "ai_aim.h"
#include "controller_tick_context.h"
#include "recoil_compensation.h"
#include "runtime_config.h"
#include "target_tracker.h"
#include "virtual_gamepad.h"
#include "vision_native/types.h"
#include "xinput_reader.h"

#include <cstdint>
#include <vector>

namespace controller_native {

struct NativeAutoFireCounters {
    std::uint64_t requested = 0;
    std::uint64_t allowed = 0;
    std::uint64_t blocked = 0;
};

struct NativeControllerStageTrace {
    std::string stage_name;
    float before_right_y = 0.0f;
    float after_right_y = 0.0f;
    float delta_right_y = 0.0f;
    bool before_auto_fire_active = false;
    bool after_auto_fire_active = false;
};

class NativeGamepadController {
public:
    explicit NativeGamepadController(GamepadRuntimeConfig config = {});

    void reset();
    void submit_vision_state(const NativeControllerVisionState& state);
    void submit_vision_result(const vision_native::VisionResult& result);
    GamepadOutputState build_output(const PhysicalGamepadState& physical);
    NativeAutoFireCounters auto_fire_counters() const;
    const std::vector<NativeControllerStageTrace>& last_pipeline_traces() const;
    GamepadOutputState last_tracker_motion_output() const;
    const NativeControllerOutputComponents& last_output_components() const;

private:
    bool is_aiming(const PhysicalGamepadState& physical) const;
    NativeControllerVisionState vision_state_for_frame(double now_seconds) const;
    bool auto_fire_allowed(
        const NativeControllerVisionState& vision_state,
        bool aiming,
        double now_seconds,
        bool aim_ready) const;
    bool has_fresh_auto_fire_source(
        const NativeControllerVisionState& vision_state,
        double now_seconds) const;
    bool has_fresh_aim_target(
        const NativeControllerVisionState& vision_state,
        double now_seconds) const;
    void update_ads_state(bool aiming, double now_seconds);
    bool auto_fire_aim_ready(
        const NativeControllerVisionState& vision_state,
        bool aiming,
        double now_seconds,
        float manual_right_x,
        float manual_right_y,
        const GamepadOutputState& output);
    void reset_auto_fire_readiness_tracking();
    bool is_strong_fire_target(const NativeControllerVisionState& vision_state) const;
    bool is_strong_aim_target(const NativeControllerVisionState& vision_state) const;
    bool ads_snap_active_for_frame(
        const NativeControllerVisionState& vision_state,
        bool aiming,
        double now_seconds) const;
    float ads_snap_progress_ratio(double now_seconds) const;
    float ads_snap_remaining_seconds(double now_seconds) const;
    bool body_lock_error_for_state(
        const NativeControllerVisionState& vision_state,
        float* out_dx,
        float* out_dy) const;
    bool manual_fire_pressed(const PhysicalGamepadState& physical) const;
    double manual_takeover_elapsed(double now_seconds) const;
    double manual_takeover_total_seconds() const;
    void apply_auto_fire(GamepadOutputState& output, bool should_fire) const;
    void release_fire_output(GamepadOutputState& output) const;
    void apply_ai_aim(
        GamepadOutputState& output,
        const PhysicalGamepadState& physical,
        const NativeControllerVisionState& vision_state,
        double now_seconds);
    void apply_aim_assist_dynamics(
        GamepadOutputState& output,
        float manual_right_x,
        float manual_right_y,
        const PhysicalGamepadState& physical,
        bool auto_fire_active);
    void apply_recoil(
        GamepadOutputState& output,
        const PhysicalGamepadState& physical,
        bool auto_fire_active,
        const NativeControllerVisionState& vision_state,
        double now_seconds);
    void record_target_tracker_output(const GamepadOutputState& output, double now_seconds);
    void record_stage_trace(
        const std::string& stage_name,
        float before_right_y,
        const GamepadOutputState& output,
        bool before_auto_fire_active,
        bool after_auto_fire_active);

    GamepadRuntimeConfig config_;
    NativeAiAim ai_aim_;
    NativeAimAssistDynamics aim_assist_dynamics_;
    NativeRecoilCompensation recoil_;
    NativeGamepadTargetTracker target_tracker_;
    NativeControllerVisionState latest_vision_state_;
    NativeAutoFireCounters auto_fire_counters_;
    std::vector<NativeControllerStageTrace> last_pipeline_traces_;
    GamepadOutputState last_tracker_motion_output_;
    NativeControllerOutputComponents last_output_components_;
    bool manual_fire_was_pressed_ = false;
    bool auto_fire_was_active_ = false;
    double manual_takeover_started_at_seconds_ = -1.0;
    double last_output_at_seconds_ = 0.0;
    bool ads_active_ = false;
    double ads_started_at_seconds_ = 0.0;
    int auto_fire_ready_frames_ = 0;
};

}  // namespace controller_native
