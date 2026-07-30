#pragma once

#include "ads_acquisition_controller.h"
#include "aim_response_estimator.h"
#include "aim_activation.h"
#include "aim_dynamics_shaper.h"
#include "axis_intent_arbiter.h"
#include "auto_fire_gate.h"
#include "bodylock_follow_controller.h"
#include "bodylock_policy.h"
#include "controller_pipeline.h"
#include "controller_vision_snapshot.h"
#include "intent_filter.h"
#include "output_mixer.h"
#include "pending_control_motion.h"
#include "runtime_config.h"
#include "target_coordinator.h"
#include "virtual_gamepad.h"
#include "vector_intent_fuser.h"
#include "xinput_reader.h"

#include "../recoil_native/recoil_compensation.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace controller_native {

#if defined(COD_BENCHMARK_MIX_OVERRIDE)
enum class BenchmarkIntentFusionMode : unsigned char {
    LegacyAxis,
    CausalVector,
    CausalVectorBaseline,
};

enum class BenchmarkRemainingWorkMode : unsigned char {
    UseRuntimeConfig,
    CurrentError,
    DeliveredAdjusted,
    ControllerIntegrated,
    ControllerIntegratedAssistOnly,
};
#endif

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
    GamepadOutputState build_output(const PhysicalGamepadState& physical);
    void report_output_delivery(
        bool delivered,
        bool output_enabled,
        double delivered_at_seconds) noexcept;
    NativeAutoFireCounters auto_fire_counters() const;
    const std::vector<NativeControllerStageTrace>& last_pipeline_traces() const;
    GamepadOutputState last_tracker_motion_output() const;
    const NativeControllerOutputComponents& last_output_components() const;
    const NativeControllerVisionState& last_frame_vision_state() const;
    const pipeline_contract::TargetPlan& last_target_plan() const;
    std::uint64_t ads_epoch() const noexcept;
    const std::string& last_ai_aim_mode() const;
    bool body_lock_manual_takeover_active() const;
    RelativeMotionEstimate body_lock_relative_motion_estimate() const;
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    using BenchmarkMixTransform = std::function<pipeline_contract::Vec2f(
        float manual_x,
        float manual_y,
        float mixed_x,
        float mixed_y,
        const NativeControllerOutputComponents& components)>;
    void set_benchmark_mix_transform(BenchmarkMixTransform transform);
    void set_benchmark_intent_fusion_mode(BenchmarkIntentFusionMode mode);
    void set_benchmark_remaining_work_mode(BenchmarkRemainingWorkMode mode);
    void set_benchmark_remaining_work_scale(float scale) noexcept;
    void set_benchmark_tracker_velocity_alpha(float alpha);
    void set_benchmark_player_motion_oracle(
        bool valid,
        bool rate_valid,
        pipeline_contract::Vec2f error_delta_px,
        pipeline_contract::Vec2f error_rate_px_per_sec) noexcept;
    void set_benchmark_causal_player_motion_enabled(
        bool state_enabled,
        bool forecast_enabled) noexcept;
#endif

private:
    pipeline_contract::VisionObservationBatch observation_batch_from(
        const ControllerVisionSnapshot& snapshot) const noexcept;
    NativeControllerVisionState vision_state_from_plan(
        const pipeline_contract::TargetPlan& plan,
        double now_seconds,
        bool capture_fresh) const;
    bool manual_fire_pressed(const PhysicalGamepadState& physical) const noexcept;
    void apply_recoil(
        GamepadOutputState& output,
        const PhysicalGamepadState& physical,
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
    AxisIntentArbiter axis_intent_arbiter_{};
    VectorIntentFuser vector_intent_fuser_{};
    PendingControlMotion pending_control_motion_{};
    recoil_native::RecoilCompensationPolicy recoil_;
    AimActivationTracker aim_activation_tracker_{};
    AutoFireGate auto_fire_gate_;
    ControllerVisionSnapshot pending_snapshot_{};
    bool has_pending_snapshot_ = false;
    bool aiming_ = false;
    bool previous_aiming_ = false;
    bool previous_jump_button_ = false;
    bool previous_slide_button_ = false;
    double last_jump_action_seconds_ = -1.0;
    double last_slide_action_seconds_ = -1.0;
    std::uint64_t ads_epoch_ = 0;
    std::uint64_t legacy_vision_sequence_ = 0;
    double last_tick_seconds_ = 0.0;
    float previous_plan_normalized_size_ = 0.0f;
    std::uint64_t previous_plan_target_id_ = 0;
    pipeline_contract::Vec2f aim_response_command_sum_{};
    std::uint32_t aim_response_command_count_ = 0;
    std::uint64_t last_aim_response_frame_id_ = 0;
    double last_aim_response_observed_seconds_ = 0.0;
    bool aim_response_manual_ambiguous_ = false;
    std::vector<NativeControllerStageTrace> last_pipeline_traces_;
    GamepadOutputState last_tracker_motion_output_{};
    NativeControllerOutputComponents last_output_components_{};
    NativeControllerVisionState last_frame_vision_state_{};
    pipeline_contract::TargetPlan last_target_plan_{};
    double remaining_work_accounted_seconds_ = 0.0;
    std::uint64_t remaining_work_delivery_target_id_ = 0;
    std::uint64_t remaining_work_delivery_ads_epoch_ = 0;
    bool remaining_work_reset_pending_ = false;
    std::string last_ai_aim_mode_ = "manual";
    std::function<double()> clock_;
#if defined(COD_BENCHMARK_MIX_OVERRIDE)
    BenchmarkMixTransform benchmark_mix_transform_;
    BenchmarkIntentFusionMode benchmark_intent_fusion_mode_ =
        BenchmarkIntentFusionMode::LegacyAxis;
    BenchmarkRemainingWorkMode benchmark_remaining_work_mode_ =
        BenchmarkRemainingWorkMode::UseRuntimeConfig;
    float benchmark_remaining_work_scale_ = 1.0f;
    bool benchmark_player_motion_oracle_valid_ = false;
    bool benchmark_player_motion_rate_oracle_valid_ = false;
    pipeline_contract::Vec2f benchmark_player_error_delta_px_{};
    pipeline_contract::Vec2f benchmark_player_error_rate_px_per_sec_{};
#endif
};

}  // namespace controller_native
