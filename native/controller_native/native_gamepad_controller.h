#pragma once

#include "ads_acquisition_controller.h"
#include "adaptive_recoil_feedback.h"
#include "aim_response_estimator.h"
#include "aim_activation.h"
#include "aim_dynamics_shaper.h"
#include "assist_control_state_machine.h"
#include "auto_fire_gate.h"
#include "bodylock_follow_controller.h"
#include "controller_pipeline.h"
#include "controller_vision_snapshot.h"
#include "intent_filter.h"
#include "output_mixer.h"
#include "runtime_config.h"
#include "target_coordinator.h"
#include "virtual_gamepad.h"
#include "xinput_reader.h"

#include "../recoil_native/recoil_compensation.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace controller_native {

struct NativeControllerStageTrace {
    std::string stage_name;
    float before_right_y = 0.0f;
    float after_right_y = 0.0f;
    float delta_right_y = 0.0f;
    bool before_auto_fire_active = false;
    bool after_auto_fire_active = false;
};

// Fixed-size controller-side aggregate for one accepted source frame.  It is
// copied into telemetry only when telemetry is enabled; the control path does
// not perform serialization or file I/O here.
struct NativeControllerAcquisitionTrace {
    bool valid = false;
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t physical_ads_epoch = 0;
    std::uint64_t target_acquisition_id = 0;
    bool plan_admitted = false;
    bool acquisition_active = false;
    bool acquisition_exists = false;
    std::uint64_t controller_tick_ns = 0;
    std::uint64_t plan_decision_ns = 0;
    std::uint64_t final_output_ready_ns = 0;
    pipeline_contract::AdsAcquisitionState acquisition_state =
        pipeline_contract::AdsAcquisitionState::Idle;
    bool source_decision_available = false;
    pipeline_contract::SourceDecisionOutcome source_decision_outcome =
        pipeline_contract::SourceDecisionOutcome::NoDecision;
    pipeline_contract::AdsDecisionReason source_decision_reason =
        pipeline_contract::AdsDecisionReason::None;
    pipeline_contract::AdsDecisionReason acquisition_terminal_reason =
        pipeline_contract::AdsDecisionReason::None;
    pipeline_contract::AdsDecisionReason decision_reason =
        pipeline_contract::AdsDecisionReason::None;
    std::uint32_t candidate_count = 0;
    std::uint64_t preferred_source_id = 0;
    std::uint64_t selected_source_id = 0;
    float effective_activation_radius_px = 0.0f;
    pipeline_contract::Vec2f raw_error_px{};
    pipeline_contract::Vec2f target_size_px{};
    std::uint64_t ads_acquisition_begin_ns = 0;
    std::uint64_t ads_acquisition_complete_ns = 0;
    std::uint64_t selector_target_generation = 0;
    bool selector_target_changed = false;
    pipeline_contract::Vec2f requested_ai{};
    pipeline_contract::Vec2f shaped_ai{};
    pipeline_contract::Vec2f fused_output{};
    pipeline_contract::Vec2f post_output{};
    bool has_first_requested_ai = false;
    bool has_first_shaped_ai = false;
    bool has_first_fused_output = false;
    std::uint64_t first_requested_ai_ns = 0;
    std::uint64_t first_shaped_ai_ns = 0;
    std::uint64_t first_fused_output_ns = 0;
    pipeline_contract::Vec2f first_requested_ai{};
    pipeline_contract::Vec2f first_shaped_ai{};
    pipeline_contract::Vec2f first_fused_output{};
};

class NativeGamepadController {
public:
    explicit NativeGamepadController(
        GamepadRuntimeConfig config = {},
        std::function<double()> clock = {});

    void reset();
    void submit_vision_snapshot(const ControllerVisionSnapshot& snapshot);
    const pipeline_contract::IntentState& sample_input(
        const PhysicalGamepadState& physical);
    GamepadOutputState build_output_from_sampled_input();
    GamepadOutputState build_output(const PhysicalGamepadState& physical);
    NativeAutoFireCounters auto_fire_counters() const;
    const std::vector<NativeControllerStageTrace>& last_pipeline_traces() const;
    const NativeControllerAcquisitionTrace& last_acquisition_trace() const noexcept;
    const NativeControllerOutputComponents& last_output_components() const;
    const NativeControllerVisionState& last_frame_vision_state() const;
    const pipeline_contract::TargetPlan& last_target_plan() const;
    std::uint64_t ads_epoch() const noexcept;
    const std::string& last_ai_aim_mode() const;

private:
    pipeline_contract::VisionObservationBatch observation_batch_from(
        const ControllerVisionSnapshot& snapshot) const noexcept;
    NativeControllerVisionState vision_state_from_plan(
        const pipeline_contract::TargetPlan& plan,
        double now_seconds,
        bool capture_fresh,
        pipeline_contract::Vec2f observed_error_px) const;
    bool manual_fire_pressed(const PhysicalGamepadState& physical) const noexcept;
    void apply_recoil(
        GamepadOutputState& output,
        const PhysicalGamepadState& physical,
        const pipeline_contract::TargetPlan& plan,
        pipeline_contract::Vec2f observed_error_px,
        bool capture_fresh,
        bool aiming,
        bool auto_fire_active,
        double now_seconds);
    void record_stage_trace(
        const std::string& stage_name,
        float before_right_y,
        const GamepadOutputState& output,
        bool before_auto_fire_active,
        bool after_auto_fire_active);
    double now_seconds() const;

    GamepadRuntimeConfig config_{};
    IntentFilter intent_filter_{};
    TargetCoordinator target_coordinator_{};
    AimResponseEstimator aim_response_estimator_{};
    AdsAcquisitionController ads_controller_{};
    BodylockFollowController bodylock_controller_{};
    AimDynamicsShaper dynamics_shaper_{};
    AssistControlStateMachine assist_control_state_machine_{};
    recoil_native::RecoilCompensationPolicy recoil_;
    AdaptiveRecoilFeedback adaptive_recoil_feedback_;
    AimActivationTracker aim_activation_tracker_{};
    AutoFireGate auto_fire_gate_;
    ControllerVisionSnapshot pending_snapshot_{};
    bool has_pending_snapshot_ = false;
    bool aiming_ = false;
    bool previous_aiming_ = false;
    double last_firing_activity_seconds_ = -1.0;
    std::uint64_t ads_epoch_ = 0;
    double last_tick_seconds_ = 0.0;
    pipeline_contract::Vec2f aim_response_command_sum_{};
    std::uint32_t aim_response_command_count_ = 0;
    std::uint64_t last_aim_response_frame_id_ = 0;
    double last_aim_response_observed_seconds_ = 0.0;
    bool aim_response_manual_ambiguous_ = false;
    PhysicalGamepadState sampled_physical_{};
    pipeline_contract::IntentState sampled_intent_{};
    double sampled_now_seconds_ = 0.0;
    float sampled_dt_seconds_ = 0.001f;
    bool has_sampled_input_ = false;
    std::vector<NativeControllerStageTrace> last_pipeline_traces_;
    NativeControllerAcquisitionTrace last_acquisition_trace_{};
    std::uint64_t acquisition_trace_target_id_ = 0;
    NativeControllerOutputComponents last_output_components_{};
    NativeControllerVisionState last_frame_vision_state_{};
    pipeline_contract::TargetPlan last_target_plan_{};
    std::string last_ai_aim_mode_ = "manual";
    std::function<double()> clock_;
};

}  // namespace controller_native
