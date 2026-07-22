#include "native_benchmark_controller_adapter.h"

#include "common_native/authority_types.h"
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
    snapshot.selected_observation_id = input.target_present ? input.target_id : 0;
    snapshot.capture_time_seconds = std::isfinite(input.capture_time_seconds)
        ? input.capture_time_seconds : now_seconds;
    snapshot.ready_time_seconds = std::isfinite(input.ready_time_seconds)
        ? input.ready_time_seconds : now_seconds;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.fresh_observation = true;
    if (!input.target_present) return snapshot;

    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = input.target_id;
    candidate.valid = true;
    candidate.has_aim_point = true;
    candidate.aim_point_px = {
        static_cast<float>(320.0 + input.observed_error_px.x),
        static_cast<float>(256.0 + input.observed_error_px.y),
    };
    candidate.confidence = 0.95f;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    snapshot.candidates.push_back(candidate);
    return snapshot;
}

NativeReplayAdapter::NativeReplayAdapter(
    GamepadRuntimeConfig source_config,
    BranchSchedule schedule,
    BenchmarkCohort cohort,
    BenchmarkIntentFusionMode intent_fusion_mode,
    std::shared_ptr<AssistedModeCoverage> coverage,
    double assist_scale)
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
    physical_.right_x = static_cast<float>(input.manual_stick.x);
    physical_.right_y = static_cast<float>(input.manual_stick.y);
    if (input.fresh_vision) {
        controller_.submit_vision_snapshot(snapshot_from(input, now_seconds_));
    }
    const auto output = controller_.build_output(physical_);
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
    };
}

ReplayControllerFactory make_native_factory(
    GamepadRuntimeConfig config,
    BenchmarkCohort cohort,
    BenchmarkIntentFusionMode intent_fusion_mode,
    std::shared_ptr<AssistedModeCoverage> coverage,
    double assist_scale) {
    config.recoil.enabled = false;
    return [config = std::move(config), cohort, intent_fusion_mode,
            coverage = std::move(coverage), assist_scale](const BranchSchedule& schedule) {
        auto state = std::make_shared<NativeReplayAdapter>(
            config, schedule, cohort, intent_fusion_mode, coverage, assist_scale);
        return [state](const ControllerObservation& input) {
            return state->step(input);
        };
    };
}

}  // namespace controller_native::benchmark_adapter
