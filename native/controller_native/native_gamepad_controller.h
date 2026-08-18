#pragma once

#include "ads_acquisition_controller.h"
#include "ads_reacquisition_reducer.h"
#include "aim_response_estimator.h"
#include "aim_scope_reducer.h"
#include "aim_dynamics_shaper.h"
#include "assist_control_state_machine.h"
#include "auto_fire_gate.h"
#include "bodylock_follow_controller.h"
#include "bodylock_target_motion_observer.h"
#include "control_frame.h"
#include "controller_vision_snapshot.h"
#include "intent_filter.h"
#include "operation_intent.h"
#include "output_diagnostics.h"
#include "recoil_reducer.h"
#include "runtime_config.h"
#include "target_coordinator.h"
#include "virtual_gamepad.h"
#include "xinput_reader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace controller_native {

struct NativeControllerStageTrace {
    std::string_view stage_name;
    float before_right_y = 0.0f;
    float after_right_y = 0.0f;
    float delta_right_y = 0.0f;
    bool before_auto_fire_active = false;
    bool after_auto_fire_active = false;
};

class NativeControllerStageTraceBuffer {
public:
    static constexpr std::size_t kCapacity = 4;

    void clear() noexcept {
        size_ = 0;
        overflowed_ = false;
    }
    bool push_back(const NativeControllerStageTrace& trace) noexcept {
        if (size_ >= traces_.size()) {
            overflowed_ = true;
            return false;
        }
        traces_[size_++] = trace;
        return true;
    }
    const NativeControllerStageTrace* begin() const noexcept {
        return traces_.data();
    }
    const NativeControllerStageTrace* end() const noexcept {
        return traces_.data() + size_;
    }
    const NativeControllerStageTrace& operator[](
        std::size_t index) const noexcept {
        return traces_[index];
    }
    std::size_t size() const noexcept { return size_; }
    bool overflowed() const noexcept { return overflowed_; }

private:
    std::array<NativeControllerStageTrace, kCapacity> traces_{};
    std::size_t size_ = 0;
    bool overflowed_ = false;
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

struct NativeControlTickPreparation {
    std::uint64_t tick_id = 0;
    double now_seconds = 0.0;
    float dt_seconds = 0.001f;
    AimScopeSnapshot scope{};
    pipeline_contract::IntentState intent{};
    pipeline_contract::EventSequence input_cause{};
    bool acquisition_rearmed = false;
};

class NativeGamepadController {
public:
    explicit NativeGamepadController(
        GamepadRuntimeConfig config = {},
        const double* injected_clock_seconds = nullptr);

    void reset();
    void submit_vision_snapshot(const ControllerVisionSnapshot& snapshot);
    const NativeControlTickPreparation& begin_tick(
        const PhysicalGamepadState& physical,
        std::uint64_t tick_id = 0);
    ControlFrame resolve_control_frame();
    void observe_composed_output(const GamepadOutputState& output);
    // Compatibility facade for replay and focused controller tests. It uses
    // the same ControlFrame -> OutputComposer path as RuntimeLoop.
    GamepadOutputState build_output_from_sampled_input();
    GamepadOutputState build_output(const PhysicalGamepadState& physical);
    NativeAutoFireCounters auto_fire_counters() const;
    const NativeControllerStageTraceBuffer& last_pipeline_traces() const;
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
    void record_stage_trace(
        std::string_view stage_name,
        float before_right_y,
        float after_right_y,
        bool before_auto_fire_active,
        bool after_auto_fire_active);
    void record_aim_response_command(
        double at_seconds,
        pipeline_contract::Vec2f stick,
        bool manual_ambiguous) noexcept;
    bool average_aim_response_command(
        double begin_seconds,
        double end_seconds,
        pipeline_contract::Vec2f* average_stick,
        bool* manual_ambiguous) const noexcept;
    double now_seconds() const;

    struct TimedAimResponseCommand {
        double at_seconds = 0.0;
        pipeline_contract::Vec2f stick{};
        bool manual_ambiguous = false;
    };

    // 128 ms at the 1 kHz controller cadence. This is long enough to cover
    // the accepted Vision interval plus the measured game/capture response
    // delay without allocating in the hot path.
    static constexpr std::size_t kAimResponseHistoryCapacity = 128;

    GamepadRuntimeConfig config_{};
    IntentFilter intent_filter_{};
    OperationIntentClassifier operation_intent_classifier_{};
    OperationIntentOutput last_operation_intent_{};
    TargetCoordinator target_coordinator_{};
    AimResponseEstimator aim_response_estimator_{};
    AdsAcquisitionController ads_controller_{};
    AdsReacquisitionReducer ads_reacquisition_reducer_{};
    BodylockFollowController bodylock_controller_{};
    BodylockTargetMotionObserver bodylock_target_motion_observer_{};
    AimDynamicsShaper dynamics_shaper_{};
    AssistControlStateMachine assist_control_state_machine_{};
    RecoilReducer recoil_;
    InputEdgeReducer input_edge_reducer_{};
    AimScopeReducer aim_scope_reducer_{};
    AutoFireGate auto_fire_gate_;
    ControllerVisionSnapshot pending_snapshot_{};
    bool has_pending_snapshot_ = false;
    bool physical_aiming_ = false;
    bool aiming_ = false;
    double last_firing_activity_seconds_ = -1.0;
    std::uint64_t ads_epoch_ = 0;
    double last_tick_seconds_ = 0.0;
    double aim_response_effect_delay_seconds_ = 0.009;
    std::array<TimedAimResponseCommand, kAimResponseHistoryCapacity>
        aim_response_command_history_{};
    std::size_t aim_response_history_begin_ = 0;
    std::size_t aim_response_history_count_ = 0;
    std::uint64_t last_aim_response_frame_id_ = 0;
    std::uint64_t last_aim_response_target_id_ = 0;
    double last_aim_response_capture_seconds_ = 0.0;
    pipeline_contract::Vec2f last_aim_response_source_error_px_{};
    bool has_last_aim_response_observation_ = false;
    PhysicalGamepadState sampled_physical_{};
    pipeline_contract::IntentState sampled_intent_{};
    NativeControlTickPreparation last_tick_preparation_{};
    double sampled_now_seconds_ = 0.0;
    float sampled_dt_seconds_ = 0.001f;
    bool has_sampled_input_ = false;
    std::uint64_t next_controller_tick_id_ = 1;
    std::uint64_t next_command_sequence_ = 1;
    bool composed_output_pending_ = false;
    bool pending_auto_fire_active_ = false;
    NativeControllerStageTraceBuffer last_pipeline_traces_{};
    NativeControllerAcquisitionTrace last_acquisition_trace_{};
    std::uint64_t acquisition_trace_target_id_ = 0;
    NativeControllerOutputComponents last_output_components_{};
    NativeControllerVisionState last_frame_vision_state_{};
    pipeline_contract::TargetPlan last_target_plan_{};
    std::string last_ai_aim_mode_ = "manual";
    const double* injected_clock_seconds_ = nullptr;
};

}  // namespace controller_native
