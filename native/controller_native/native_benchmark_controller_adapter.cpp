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
    snapshot.selected_observation_id =
        input.target_present && input.primary_candidate_visible
        ? input.target_id
        : input.decoy_candidate_present ? input.decoy_target_id : 0;
    snapshot.capture_time_seconds = std::isfinite(input.capture_time_seconds)
        ? input.capture_time_seconds : now_seconds;
    snapshot.ready_time_seconds = std::isfinite(input.ready_time_seconds)
        ? input.ready_time_seconds : now_seconds;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.fresh_observation = true;
    if (!input.target_present && !input.decoy_candidate_present) {
        return snapshot;
    }

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
    BenchmarkCohort cohort,
    std::shared_ptr<AssistedModeCoverage> coverage)
    : cohort_(cohort),
      config_(std::move(source_config)),
      controller_(config_, [this] { return now_seconds_; }),
      coverage_(std::move(coverage)) {
    physical_.connected = true;
    physical_.left_trigger =
        cohort_ == BenchmarkCohort::BodyLockFollow ? 1.0f : 0.0f;
}

ControllerStepResult NativeReplayAdapter::step(
    const ControllerObservation& input) {
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
        std::find(
            coverage_->ads_target_ids.begin(),
            coverage_->ads_target_ids.end(),
            input.target_id) == coverage_->ads_target_ids.end()) {
        coverage_->ads_target_ids.push_back(input.target_id);
    }
    if (coverage_) {
        coverage_->total_frames += 1;
        if (physical_.right_trigger > 0.5f) coverage_->firing_frames += 1;
        if (components.operation_class == "recoil_pull") {
            coverage_->recoil_pull_frames += 1;
        }
        if (components.operation_class == "unreliable") {
            coverage_->unreliable_frames += 1;
        }
        if (components.recoil_stick.y < -0.0001f) {
            coverage_->recoil_active_frames += 1;
        }
    }

    const auto& vision = controller_.last_frame_vision_state();
    const auto& plan = controller_.last_target_plan();
    ControllerStepResult result;
    result.final_stick = {output.right_x, output.right_y};
    result.requested_assist_stick = {
        components.requested_assist_stick.x,
        components.requested_assist_stick.y,
    };
    result.shaped_assist_stick = {
        components.shaped_assist_stick.x,
        components.shaped_assist_stick.y,
    };
    result.predicted_terminal_error_px = {
        plan.predicted_terminal_error_px.x,
        plan.predicted_terminal_error_px.y,
    };
    result.radial_closing_velocity_px_per_sec =
        plan.radial_closing_velocity_px_per_sec;
    result.bodylock_mode = mode == "body_lock";
    result.target_observed = vision.current_observed_target_present;
    result.tracker_reliable = vision.has_target;
    result.controller_target_id = plan.target_id;
    result.pre_recoil_stick = {
        components.before_recoil_stick.x,
        components.before_recoil_stick.y,
    };
    result.has_pre_recoil_stick = true;
    return result;
}

ControllerStep make_native_controller(
    GamepadRuntimeConfig config,
    BenchmarkCohort cohort,
    std::shared_ptr<AssistedModeCoverage> coverage,
    bool recoil_enabled) {
    config.recoil.enabled = recoil_enabled;
    auto state = std::make_shared<NativeReplayAdapter>(
        std::move(config), cohort, std::move(coverage));
    return [state](const ControllerObservation& input) {
        return state->step(input);
    };
}

}  // namespace controller_native::benchmark_adapter
