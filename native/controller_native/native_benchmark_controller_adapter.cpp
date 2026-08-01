#include "native_benchmark_controller_adapter.h"

#include "common_native/authority_types.h"
#include "native_benchmark_physical_input.h"
#include "pipeline_contract/target_snapshot.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native::benchmark_adapter {
using namespace sustained_aimlab;

ControllerVisionSnapshot snapshot_from(
    const ControllerObservation& input,
    double now_seconds) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = input.frame_id;
    snapshot.selected_observation_id = input.target_present &&
            input.primary_candidate_visible
        ? input.target_id
        : input.decoy_candidate_present ? input.decoy_target_id : 0;
    snapshot.capture_time_seconds = std::isfinite(input.capture_time_seconds)
        ? input.capture_time_seconds : now_seconds;
    snapshot.ready_time_seconds = std::isfinite(input.ready_time_seconds)
        ? input.ready_time_seconds : now_seconds;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.fresh_observation = true;
    if (!input.target_present && !input.decoy_candidate_present) return snapshot;

    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = input.target_id;
    candidate.valid = input.target_present && input.primary_candidate_visible;
    candidate.has_aim_point = true;
    if (input.has_body_box) {
        candidate.body_box_px = {
            static_cast<float>(input.body_box_x),
            static_cast<float>(input.body_box_y),
            static_cast<float>(input.body_box_width),
            static_cast<float>(input.body_box_height),
        };
        candidate.aim_point_px = {
            static_cast<float>(
                input.body_box_x + input.body_box_width * 0.5),
            static_cast<float>(
                input.body_box_y + input.body_box_height * 0.40),
        };
    } else {
        candidate.aim_point_px = {
            static_cast<float>(320.0 + input.observed_error_px.x),
            static_cast<float>(256.0 + input.observed_error_px.y),
        };
    }
    candidate.has_motion_anchor = input.has_motion_anchor;
    candidate.motion_anchor_px = {
        static_cast<float>(input.motion_anchor_px.x),
        static_cast<float>(input.motion_anchor_px.y),
    };
    candidate.motion_anchor_score = input.has_motion_anchor ? 1.0f : 0.0f;
    candidate.confidence = 0.95f;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    if (candidate.valid) snapshot.candidates.push_back(candidate);
    if (input.decoy_candidate_present) {
        pipeline_contract::VisionCandidateSnapshot decoy;
        decoy.id = input.decoy_target_id;
        decoy.valid = true;
        decoy.has_aim_point = true;
        decoy.aim_point_px = {
            static_cast<float>(320.0 + input.decoy_observed_error_px.x),
            static_cast<float>(256.0 + input.decoy_observed_error_px.y),
        };
        decoy.body_box_px = {
            decoy.aim_point_px.x - 24.0f,
            decoy.aim_point_px.y - 44.8f,
            48.0f,
            112.0f,
        };
        decoy.confidence = 0.93f;
        decoy.suggested_authority_state =
            common_native::TargetAuthorityState::StrongAssist;
        snapshot.candidates.push_back(decoy);
    }
    return snapshot;
}

NativeReplayAdapter::NativeReplayAdapter(
    GamepadRuntimeConfig source_config,
    BranchSchedule schedule,
    BenchmarkCohort cohort,
    BenchmarkIntentFusionMode intent_fusion_mode,
    std::shared_ptr<AssistedModeCoverage> coverage,
    double assist_scale,
    double tracker_velocity_alpha,
    bool causal_player_motion_state_enabled,
    bool causal_player_motion_forecast_enabled,
    BenchmarkRemainingWorkMode remaining_work_mode,
    double remaining_work_scale,
    bool firing_body_geometry_stabilizer_enabled,
    bool firing_disturbance_observer_enabled)
    : schedule_(schedule),
      cohort_(cohort),
      config_(std::move(source_config)),
      controller_(config_, [this] { return now_seconds_; }),
      coverage_(std::move(coverage)),
      assist_scale_(std::clamp(assist_scale, 0.0, 1.0)) {
    physical_.connected = true;
    physical_.left_trigger = cohort_ == BenchmarkCohort::BodyLockFollow
        ? 1.0f : 0.0f;
    controller_.set_benchmark_intent_fusion_mode(intent_fusion_mode);
    controller_.set_benchmark_remaining_work_mode(remaining_work_mode);
    controller_.set_benchmark_remaining_work_scale(
        static_cast<float>(remaining_work_scale));
    controller_.set_benchmark_firing_body_geometry_stabilizer_enabled(
        firing_body_geometry_stabilizer_enabled);
    controller_.set_benchmark_firing_disturbance_observer_enabled(
        firing_disturbance_observer_enabled);
    if (tracker_velocity_alpha >= 0.0) {
        controller_.set_benchmark_tracker_velocity_alpha(
            static_cast<float>(tracker_velocity_alpha));
    }
    controller_.set_benchmark_causal_player_motion_enabled(
        causal_player_motion_state_enabled,
        causal_player_motion_forecast_enabled);
    controller_.set_benchmark_mix_transform(
        [this](float manual_x, float manual_y,
               float mixed_x, float mixed_y,
               const NativeControllerOutputComponents& components) {
            const bool active = now_ms_ >= schedule_.start_ms &&
                now_ms_ < schedule_.start_ms + schedule_.duration_ms;
            if (!active) {
                if (assist_scale_ == 1.0) {
                    return pipeline_contract::Vec2f{mixed_x, mixed_y};
                }
                return pipeline_contract::Vec2f{
                    manual_x + (mixed_x - manual_x) *
                        static_cast<float>(assist_scale_),
                    manual_y + (mixed_y - manual_y) *
                        static_cast<float>(assist_scale_)};
            }
            const auto ai = components.shaped_assist_stick;
            switch (schedule_.policy) {
            case BranchPolicy::ActualMix:
                return pipeline_contract::Vec2f{mixed_x, mixed_y};
            case BranchPolicy::Neutral:
                return pipeline_contract::Vec2f{};
            case BranchPolicy::ManualOnly:
                return pipeline_contract::Vec2f{manual_x, manual_y};
            case BranchPolicy::AiOnly:
                return pipeline_contract::Vec2f{ai.x, ai.y};
            case BranchPolicy::ManualPlusAi25:
                return pipeline_contract::Vec2f{
                    manual_x + ai.x * 0.25f,
                    manual_y + ai.y * 0.25f};
            case BranchPolicy::ManualPlusAi50:
                return pipeline_contract::Vec2f{
                    manual_x + ai.x * 0.50f,
                    manual_y + ai.y * 0.50f};
            case BranchPolicy::ManualPlusAi75:
                return pipeline_contract::Vec2f{
                    manual_x + ai.x * 0.75f,
                    manual_y + ai.y * 0.75f};
            }
            return pipeline_contract::Vec2f{mixed_x, mixed_y};
        });
}

ControllerStepResult NativeReplayAdapter::step(
    const ControllerObservation& input) {
    now_ms_ = input.now_ms;
    now_seconds_ = static_cast<double>(input.now_ms) / 1000.0;
    if (cohort_ == BenchmarkCohort::AdsAcquire) {
        if (!input.target_present) {
            physical_.left_trigger = 0.0f;
            last_ads_target_id_ = 0;
        } else if (input.target_id != last_ads_target_id_) {
            physical_.left_trigger = 0.0f;
            last_ads_target_id_ = input.target_id;
        } else {
            physical_.left_trigger = 1.0f;
        }
    }
    apply_benchmark_physical_input(input, physical_);
    controller_.set_benchmark_player_motion_oracle(
        input.player_motion_oracle,
        input.player_motion_rate_oracle,
        {static_cast<float>(input.player_error_delta_px.x),
         static_cast<float>(input.player_error_delta_px.y)},
        {static_cast<float>(input.player_error_rate_px_per_second.x),
         static_cast<float>(input.player_error_rate_px_per_second.y)});
    if (input.fresh_vision) {
        controller_.submit_vision_snapshot(snapshot_from(input, now_seconds_));
    }
    const auto output = controller_.build_output(physical_);
    controller_.report_output_delivery(
        true, true, now_seconds_ + 0.000001);
    const auto& components = controller_.last_output_components();
    const std::string& mode = controller_.last_ai_aim_mode();
    if (coverage_ && (mode == "ads_snap" || mode == "body_lock")) {
        coverage_->saw_assisted_mode = true;
    }
    if (coverage_ && mode == "ads_snap" && input.target_id != 0 &&
        std::find(coverage_->ads_target_ids.begin(),
                  coverage_->ads_target_ids.end(),
                  input.target_id) == coverage_->ads_target_ids.end()) {
        coverage_->ads_target_ids.push_back(input.target_id);
    }
    const auto& vision = controller_.last_frame_vision_state();
    const auto& plan = controller_.last_target_plan();
    return ControllerStepResult{
        {output.right_x, output.right_y},
        {components.requested_assist_stick.x,
         components.requested_assist_stick.y},
        {components.shaped_assist_stick.x,
         components.shaped_assist_stick.y},
        {plan.predicted_terminal_error_px.x,
         plan.predicted_terminal_error_px.y},
        plan.radial_closing_velocity_px_per_sec,
        mode == "body_lock",
        vision.current_observed_target_present,
        vision.has_target,
        components.intent_fusion_candidate,
        components.intent_fusion_manual_weight,
        components.intent_fusion_ai_weight,
        components.intent_fusion_fallback,
        components.intent_fusion_manual_escape,
        plan.target_id,
        plan.remaining_work_valid,
        {plan.remaining_work_px.x, plan.remaining_work_px.y},
        {components.before_recoil_stick.x,
         components.before_recoil_stick.y},
        true,
    };
}

ReplayControllerFactory make_native_factory(
    GamepadRuntimeConfig config,
    BenchmarkCohort cohort,
    BenchmarkIntentFusionMode intent_fusion_mode,
    std::shared_ptr<AssistedModeCoverage> coverage,
    double assist_scale,
    double tracker_velocity_alpha,
    bool causal_player_motion_state_enabled,
    bool causal_player_motion_forecast_enabled,
    BenchmarkRemainingWorkMode remaining_work_mode,
    double remaining_work_scale,
    bool recoil_enabled,
    bool firing_body_geometry_stabilizer_enabled,
    bool firing_disturbance_observer_enabled) {
    config.recoil.enabled = recoil_enabled;
    return [config = std::move(config), cohort, intent_fusion_mode,
            coverage = std::move(coverage), assist_scale,
            tracker_velocity_alpha,
            causal_player_motion_state_enabled,
            causal_player_motion_forecast_enabled,
            remaining_work_mode,
            remaining_work_scale,
            firing_body_geometry_stabilizer_enabled,
            firing_disturbance_observer_enabled](
                const BranchSchedule& schedule) {
        auto state = std::make_shared<NativeReplayAdapter>(
            config, schedule, cohort, intent_fusion_mode, coverage,
            assist_scale, tracker_velocity_alpha,
            causal_player_motion_state_enabled,
            causal_player_motion_forecast_enabled,
            remaining_work_mode,
            remaining_work_scale,
            firing_body_geometry_stabilizer_enabled,
            firing_disturbance_observer_enabled);
        return [state](const ControllerObservation& input) {
            return state->step(input);
        };
    };
}

}  // namespace controller_native::benchmark_adapter
