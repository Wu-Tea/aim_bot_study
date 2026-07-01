#pragma once

#include "ads_state_tracker.h"
#include "aim_assist_dynamics.h"
#include "ai_aim.h"
#include "auto_fire_gate.h"
#include "body_lock_short_plan_policy.h"
#include "controller_tick_context.h"
#include "controller_vision_snapshot.h"
#include "output_validation_policy.h"
#include "runtime_config.h"
#include "target_snapshot_provider.h"
#include "virtual_gamepad.h"
#include "xinput_reader.h"

#include "../recoil_native/recoil_compensation.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace vision_native {
struct VisionResult;
}

namespace controller_native {

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
    explicit NativeGamepadController(
        GamepadRuntimeConfig config = {},
        std::function<double()> clock = {});

    void reset();
    void submit_vision_state(const NativeControllerVisionState& state);
    void submit_vision_snapshot(const ControllerVisionSnapshot& snapshot);
    void submit_vision_result(const vision_native::VisionResult& result);
    GamepadOutputState build_output(const PhysicalGamepadState& physical);
    NativeAutoFireCounters auto_fire_counters() const;
    const std::vector<NativeControllerStageTrace>& last_pipeline_traces() const;
    GamepadOutputState last_tracker_motion_output() const;
    const NativeControllerOutputComponents& last_output_components() const;
    const NativeControllerVisionState& last_frame_vision_state() const;
    const std::string& last_ai_aim_mode() const;

private:
    bool is_aiming(const PhysicalGamepadState& physical) const;
    bool has_fresh_aim_target(
        const NativeControllerVisionState& vision_state,
        double now_seconds) const;
    void update_ads_state(bool aiming, double now_seconds);
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
    void apply_ai_aim(
        GamepadOutputState& output,
        const PhysicalGamepadState& physical,
        const NativeControllerVisionState& vision_state,
        double now_seconds);
    void apply_body_lock_short_plan(
        GamepadOutputState& output,
        float manual_right_x,
        float manual_right_y,
        bool vertical_plan_allowed,
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
        double now_seconds);
    void record_target_tracker_output(
        const NativeControllerOutputComponents& components,
        double now_seconds);
    void record_stage_trace(
        const std::string& stage_name,
        float before_right_y,
        const GamepadOutputState& output,
        bool before_auto_fire_active,
        bool after_auto_fire_active);
    double now_seconds() const;

    GamepadRuntimeConfig config_;
    NativeAiAim ai_aim_;
    NativeAimAssistDynamics aim_assist_dynamics_;
    recoil_native::RecoilCompensationPolicy recoil_;
    AdsStateTracker ads_state_tracker_;
    AutoFireGate auto_fire_gate_;
    BodyLockShortPlanPolicy body_lock_short_plan_policy_;
    OutputValidationPolicy output_validation_policy_;
    TargetSnapshotProvider target_snapshot_provider_;
    std::vector<NativeControllerStageTrace> last_pipeline_traces_;
    GamepadOutputState last_tracker_motion_output_;
    NativeControllerOutputComponents last_output_components_;
    NativeControllerVisionState last_frame_vision_state_;
    std::function<double()> clock_;
};

}  // namespace controller_native
