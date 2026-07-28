#include "pipeline_contract/committed_capture_observation.h"
#include "runtime_app/vision_controller_adapter.h"

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance = 1.0e-5f) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error("unexpected floating-point value");
    }
}

pipeline_contract::CommittedCaptureObservation valid_observation() {
    pipeline_contract::CommittedCaptureObservation value;
    value.source_frame_id = 91;
    value.source_observation_id = (91ull << 32ull) | 1ull;
    value.persistent_target_id = 7;
    value.viewport_sequence = 0;
    value.viewport_source_frame_id = 91;
    value.captured_at_ns = 1'000;
    value.result_at_ns = 1'100;
    value.stable_error_px = {12.0f, -4.0f};
    value.stable_body_size_px = {40.0f, 100.0f};
    value.reliability = 0.8f;
    value.normalized_size = 0.25f;
    value.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    value.motion = pipeline_contract::TargetMotion::Steady;
    value.mode = pipeline_contract::ControlMode::BodyLockFollow;
    value.fresh_observed = true;
    value.strong_observation = true;
    value.stable_coordinates_valid = true;
    value.eligible_candidate_count = 1;
    return value;
}

void test_contract_is_fixed_size_and_validates_defaults_explicitly() {
    require(std::is_trivially_copyable_v<
                pipeline_contract::CommittedCaptureObservation>,
            "committed capture contract must be fixed-size publishable data");
    require(pipeline_contract::valid(valid_observation()),
            "complete committed capture must validate");
    require(!pipeline_contract::valid(
                pipeline_contract::CommittedCaptureObservation{}),
            "default observation must not masquerade as committed evidence");
}

void test_committed_capture_rejects_result_before_capture() {
    auto value = valid_observation();
    value.result_at_ns = value.captured_at_ns - 1;
    require(!pipeline_contract::valid(value),
            "result time before capture must be rejected");
}

void test_committed_capture_keeps_frame_owned_viewport() {
    auto value = valid_observation();
    value.viewport_source_frame_id = 92;
    require(!pipeline_contract::valid(value),
            "future or foreign viewport identity must be rejected");
}

void test_fixed_roi_zero_offset_is_valid() {
    auto value = valid_observation();
    value.viewport_offset_px = {};
    value.viewport_sequence = 0;
    value.viewport_source_frame_id = value.source_frame_id;
    require(pipeline_contract::valid(value),
            "fixed ROI zero offset must remain valid");
}

void test_contract_rejects_invalid_units_and_fresh_identity() {
    auto value = valid_observation();
    value.reliability = 1.01f;
    require(!pipeline_contract::valid(value),
            "reliability outside unit interval must be rejected");
    value = valid_observation();
    value.stable_error_px.x = std::nanf("");
    require(!pipeline_contract::valid(value),
            "non-finite stable error must be rejected");
    value = valid_observation();
    value.persistent_target_id = 0;
    require(!pipeline_contract::valid(value),
            "fresh observation must own a persistent target");
}

std::uint64_t observation_id(std::uint64_t frame, std::size_t index) {
    return ((frame & 0xffffffffull) << 32ull) |
        static_cast<std::uint64_t>(index + 1u);
}

vision_native::Detection detection(
    float x1, float y1, float x2, float y2, float confidence) {
    vision_native::Detection value;
    value.x1 = x1;
    value.y1 = y1;
    value.x2 = x2;
    value.y2 = y2;
    value.conf = confidence;
    return value;
}

void test_adapter_joins_committed_identity_not_raw_challenger() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 41;
    result.captured_at_ns = 2'000'000'000ull;
    result.result_at_ns = 2'012'000'000ull;
    result.screen_center_x = 240.0f;
    result.screen_center_y = 208.0f;
    result.viewport_sequence = 5;
    result.viewport_left = 60;
    result.viewport_top = 52;
    result.detections.push_back(detection(260, 160, 300, 260, 0.90f));
    result.detections.push_back(detection(232, 170, 260, 250, 0.99f));
    result.has_selected_detection = true;
    result.selected_detection_index = 1;

    pipeline_contract::TargetPlan plan;
    plan.source_frame_id = result.frame_id;
    plan.source_observation_id = observation_id(result.frame_id, 0);
    plan.target_id = 77;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.motion = pipeline_contract::TargetMotion::Steady;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.reliability = 0.75f;
    plan.normalized_size = 0.24f;

    const auto committed = runtime_app::adapt_committed_capture_observation(
        result, plan, 0.365f, 9);
    require(pipeline_contract::valid(committed),
            "committed candidate A must produce valid capture evidence");
    require(committed.persistent_target_id == 77,
            "adapter must retain coordinator-owned identity");
    require(committed.source_observation_id == observation_id(41, 0),
            "adapter must join the committed source observation");
    require(committed.source_observation_id != observation_id(41, 1),
            "raw challenger B must not enter learning evidence");
    require(committed.eligible_candidate_count == 2,
            "adapter must retain multi-target audit context");
    require_near(committed.stable_error_px.x, 40.0f);
    require_near(committed.stable_error_px.y, -11.5f);
    require(committed.ads_epoch == 9,
            "adapter must bind observation to the active ADS epoch");
    require(committed.viewport_sequence == 5,
            "adapter must retain the viewport generation used by the frame");
    require_near(committed.viewport_offset_px.x, 60.0f);
    require_near(committed.viewport_offset_px.y, 52.0f);
}

void test_adapter_never_substitutes_result_time_for_capture_time() {
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 8;
    result.captured_at_ns = 0;
    result.result_at_ns = 50'000;
    result.screen_center_x = 240.0f;
    result.screen_center_y = 208.0f;
    result.detections.push_back(detection(230, 150, 270, 250, 0.9f));
    pipeline_contract::TargetPlan plan;
    plan.source_frame_id = 8;
    plan.source_observation_id = observation_id(8, 0);
    plan.target_id = 2;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    const auto committed = runtime_app::adapt_committed_capture_observation(
        result, plan, 0.365f, 1);
    require(!pipeline_contract::valid(committed),
            "missing capture time must remain invalid for learning");
}

}  // namespace

int main() {
    test_contract_is_fixed_size_and_validates_defaults_explicitly();
    test_committed_capture_rejects_result_before_capture();
    test_committed_capture_keeps_frame_owned_viewport();
    test_fixed_roi_zero_offset_is_valid();
    test_contract_rejects_invalid_units_and_fresh_identity();
    test_adapter_joins_committed_identity_not_raw_challenger();
    test_adapter_never_substitutes_result_time_for_capture_time();
    return 0;
}
