#pragma once

#include "pending_control_motion.h"
#include "sustained_aimlab_scenario.h"
#include "sustained_aimlab_score.h"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace controller_native::sustained_aimlab {

struct ControllerObservation {
    int now_ms = 0;
    bool target_present = false;
    bool primary_candidate_visible = true;
    bool decoy_candidate_present = false;
    std::uint64_t decoy_target_id = 0;
    Vec2d decoy_observed_error_px;
    bool fresh_vision = false;
    std::uint64_t frame_id = 0;
    std::uint64_t target_id = 0;
    double capture_time_seconds = std::numeric_limits<double>::quiet_NaN();
    double ready_time_seconds = std::numeric_limits<double>::quiet_NaN();
    Vec2d observed_error_px;
    bool has_body_box = false;
    double body_box_x = 0.0;
    double body_box_y = 0.0;
    double body_box_width = 0.0;
    double body_box_height = 0.0;
    bool has_motion_anchor = false;
    Vec2d motion_anchor_px;
    Vec2d manual_stick;
    double left_x = 0.0;
    bool jump_action = false;
    bool slide_action = false;
    bool fire_action = false;
    bool player_motion_oracle = false;
    bool player_motion_rate_oracle = false;
    Vec2d player_error_delta_px;
    Vec2d player_error_rate_px_per_second;
};

struct ControllerStepResult {
    Vec2d final_stick;
    Vec2d requested_assist_stick;
    Vec2d shaped_assist_stick;
    Vec2d predicted_terminal_error_px;
    double radial_closing_velocity_px_per_sec = 0.0;
    bool bodylock_mode = false;
    bool target_observed = false;
    bool tracker_reliable = false;
    int intent_fusion_candidate = 0;
    float intent_fusion_manual_weight = 1.0f;
    float intent_fusion_ai_weight = 0.0f;
    bool intent_fusion_fallback = false;
    bool intent_fusion_manual_escape = false;
    std::uint64_t controller_target_id = 0;
    bool remaining_work_valid = false;
    Vec2d remaining_work_px;
    Vec2d pre_recoil_stick;
    bool has_pre_recoil_stick = false;
    bool causal_memory_valid = false;
    bool causal_memory_realized_valid = false;
    Vec2d causal_memory_realized_px;
    Vec2d causal_memory_in_flight_px;
    Vec2d causal_memory_scheduled_px;
    Vec2d causal_memory_pending_total_px;
    CausalMotionLedgerStatus causal_memory_status =
        CausalMotionLedgerStatus::Empty;
    std::uint64_t controller_ads_epoch = 0;
    float causal_memory_realized_response_confidence = 0.0f;
    float causal_memory_pending_response_confidence = 0.0f;
    bool causal_memory_realized_response_confidence_valid = false;
    bool causal_memory_pending_response_confidence_valid = false;
};

struct SimulationTraceFrame {
    int absolute_ms = 0;
    int target_elapsed_ms = -1;
    bool target_active = false;
    bool fresh_vision = false;
    bool vision_occluded = false;
    std::uint64_t target_id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    Vec2d true_error_before_px;
    Vec2d true_error_after_px;
    Vec2d target_velocity_px_per_second;
    double player_velocity_x_px_per_second = 0.0;
    double player_vertical_offset_y_px = 0.0;
    double player_vertical_velocity_y_px_per_second = 0.0;
    ControllerObservation input;
    ControllerStepResult output;
    // Independent known-plant truth. These fields are populated by the
    // simulator's delayed actuator, not by CausalMotionLedger.
    Vec2d plant_applied_camera_velocity_px_per_second;
    Vec2d plant_applied_displacement_px;
    double plant_applied_command_delivery_seconds =
        std::numeric_limits<double>::quiet_NaN();
    std::uint64_t plant_applied_target_id = 0;
    std::uint64_t plant_applied_ads_epoch = 0;
    bool plant_applied_valid = false;
};

using SimulationTraceObserver =
    std::function<void(const SimulationTraceFrame&)>;

using ControllerStep = std::function<ControllerStepResult(
    const ControllerObservation&)>;

constexpr std::size_t kCausalMemoryLedgerStatusCount = 12;

struct CausalMotionPhaseAccuracy {
    std::uint64_t predicted_valid_count = 0;
    std::uint64_t truth_valid_count = 0;
    std::uint64_t scored_count = 0;
    double residual_mean_px = 0.0;
    double residual_p95_px = 0.0;
    double residual_max_px = 0.0;
    // Residual divided by the independently measured truth magnitude for
    // nontrivial windows.  Absolute px thresholds alone can hide a large
    // response-model error when the command is small.
    std::uint64_t normalized_residual_count = 0;
    double normalized_residual_mean = 0.0;
    double normalized_residual_p95 = 0.0;
    double normalized_residual_max = 0.0;
    std::uint64_t sign_component_count = 0;
    std::uint64_t sign_agreement_count = 0;
    double sign_agreement = 0.0;
    std::uint64_t cosine_count = 0;
    double cosine_mean = 0.0;
    double cosine_min = 0.0;
};

struct CausalMemoryAccuracyGate {
    // Offline evidence thresholds, not control thresholds. These limits allow
    // one simulator tick of quantization plus a small response-model error
    // while still rejecting a wrong phase or sign.
    double max_mean_residual_px = 1.0;
    double max_p95_residual_px = 2.5;
    double max_max_residual_px = 8.0;
    double max_normalized_mean_residual = 0.20;
    double max_normalized_p95_residual = 0.50;
    double max_normalized_max_residual = 1.00;
    double normalized_truth_min_px = 0.25;
    double min_sign_agreement = 0.95;
    double min_vector_cosine = 0.95;
    double min_valid_prediction_ratio = 0.80;
};

struct CausalActuatorTruthSummary {
    Vec2d total_applied_displacement_px;
    Vec2d targetless_applied_displacement_px;
    // Targetless/manual-origin work physically applied before the first
    // target-owned controller state was observed.
    Vec2d pre_acquisition_carry_displacement_px;
    // Targetless/manual-origin work physically applied after a target-owned
    // state had already been observed.  This is the dangerous carry-in at a
    // first acquisition or after loss/reacquire.
    Vec2d targetless_after_acquisition_carry_displacement_px;
    Vec2d cross_boundary_old_owner_displacement_px;
    double cross_boundary_old_owner_abs_displacement_px = 0.0;
    std::uint64_t applied_sample_count = 0;
    std::uint64_t targetless_sample_count = 0;
    std::uint64_t pre_acquisition_carry_sample_count = 0;
    std::uint64_t targetless_after_acquisition_carry_sample_count = 0;
    std::uint64_t cross_boundary_old_owner_sample_count = 0;
    std::uint64_t lifecycle_boundary_count = 0;
};

struct CausalMemoryAccuracySummary {
    std::uint64_t fresh_capture_count = 0;
    std::uint64_t paired_capture_count = 0;
    std::uint64_t prediction_valid_count = 0;
    std::uint64_t prediction_invalid_count = 0;
    std::uint64_t lifecycle_reset_count = 0;
    std::uint64_t truth_incomplete_window_count = 0;
    std::uint64_t horizon_or_boundary_count = 0;
    std::array<std::uint64_t, kCausalMemoryLedgerStatusCount> status_counts{};
    CausalMotionPhaseAccuracy realized;
    CausalMotionPhaseAccuracy in_flight;
    CausalMotionPhaseAccuracy scheduled;
    CausalMotionPhaseAccuracy pending_total;
    CausalActuatorTruthSummary actuator_truth;
    double valid_prediction_ratio = 0.0;
    bool gate_pass = false;
};

const char* causal_motion_status_name(
    CausalMotionLedgerStatus status) noexcept;

CausalActuatorTruthSummary summarize_causal_actuator_truth(
    const std::vector<SimulationTraceFrame>& trace);

CausalMemoryAccuracySummary score_causal_memory_trace(
    const std::vector<SimulationTraceFrame>& trace,
    double ledger_response_delay_ms,
    double plant_response_delay_ms,
    const CausalMemoryAccuracyGate& gate = {});

BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire,
    SimulationTraceObserver trace_observer = {},
    PlayerStrafeMode player_strafe_mode = PlayerStrafeMode::Off,
    PlayerVerticalMotionMode player_vertical_motion_mode =
        PlayerVerticalMotionMode::Off);

}  // namespace controller_native::sustained_aimlab
