#include "viewport_controller.h"

#include <cstdlib>
#include <iostream>

namespace {

void require_at(bool condition, int line) {
    if (!condition) {
        std::cerr << "viewport controller assertion failed at line " << line << '\n';
        std::abort();
    }
}

#define require(condition) require_at((condition), __LINE__)

pipeline_contract::TargetPlan observed_plan(std::uint64_t target_id, std::uint64_t frame_id) {
    pipeline_contract::TargetPlan plan;
    plan.target_id = target_id;
    plan.source_frame_id = frame_id;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    return plan;
}

pipeline_contract::TargetPlan coasting_plan(
    std::uint64_t target_id,
    std::uint64_t frame_id) {
    auto plan = observed_plan(target_id, frame_id);
    plan.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    return plan;
}

pipeline_contract::CommittedCaptureObservation observation(
    std::uint64_t target_id,
    std::uint64_t frame_id,
    std::uint64_t captured_at_ns,
    float error_x,
    float error_y,
    float width,
    float height) {
    pipeline_contract::CommittedCaptureObservation value;
    value.source_frame_id = frame_id;
    value.source_observation_id = frame_id + 100;
    value.persistent_target_id = target_id;
    value.viewport_source_frame_id = frame_id;
    value.captured_at_ns = captured_at_ns;
    value.result_at_ns = captured_at_ns + 1;
    value.stable_error_px = {error_x, error_y};
    value.stable_body_size_px = {width, height};
    value.reliability = 1.0f;
    value.normalized_size = 0.2f;
    value.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    value.fresh_observed = true;
    value.strong_observation = true;
    value.stable_coordinates_valid = true;
    value.eligible_candidate_count = 1;
    return value;
}

runtime_app::ViewportController controller() {
    runtime_app::ViewportControllerConfig config;
    config.enabled = true;
    config.expand_confirm_frames = 2;
    config.normal_min_dwell_ms = 0.0f;
    config.rescue_min_dwell_ms = 0.0f;
    config.shrink_stable_ms = 100.0f;
    return runtime_app::ViewportController(config);
}

void test_starts_precision_and_expands_after_two_distinct_frames() {
    auto value = controller();
    require(value.current().level == runtime_app::ViewportLevel::Precision);

    auto first = observation(1, 1, 1'000'000'000, 120.0f, 0.0f, 80.0f, 120.0f);
    auto request = value.update(observed_plan(1, 1), &first, 1'001'000'000);
    require(request.level == runtime_app::ViewportLevel::Precision);

    auto second = observation(1, 2, 1'010'000'000, 120.0f, 0.0f, 80.0f, 120.0f);
    request = value.update(observed_plan(1, 2), &second, 1'011'000'000);
    require(request.level == runtime_app::ViewportLevel::Normal);
    require(request.changed);
}

void test_severe_overflow_expands_immediately_to_rescue() {
    auto value = controller();
    auto near_edge = observation(3, 10, 2'000'000'000, 245.0f, 0.0f, 100.0f, 150.0f);
    const auto request =
        value.update(observed_plan(3, 10), &near_edge, 2'001'000'000);
    require(request.level == runtime_app::ViewportLevel::Rescue);
    require(request.changed);
}

void test_shrink_is_delayed_and_one_level_at_a_time() {
    auto value = controller();
    auto edge = observation(4, 20, 3'000'000'000, 245.0f, 0.0f, 100.0f, 150.0f);
    require(value.update(observed_plan(4, 20), &edge, 3'001'000'000).level ==
        runtime_app::ViewportLevel::Rescue);

    auto centered = observation(4, 21, 3'020'000'000, 0.0f, 0.0f, 40.0f, 60.0f);
    require(value.update(observed_plan(4, 21), &centered, 3'021'000'000).level ==
        runtime_app::ViewportLevel::Rescue);
    require(value.update(observed_plan(4, 21), nullptr, 3'080'000'000).level ==
        runtime_app::ViewportLevel::Rescue);
    require(value.update(observed_plan(4, 21), nullptr, 3'130'000'000).level ==
        runtime_app::ViewportLevel::Normal);
}

void test_no_target_returns_to_precision() {
    auto value = controller();
    auto edge = observation(5, 30, 4'000'000'000, 245.0f, 0.0f, 100.0f, 150.0f);
    require(value.update(observed_plan(5, 30), &edge, 4'001'000'000).level ==
        runtime_app::ViewportLevel::Rescue);
    const pipeline_contract::TargetPlan none;
    const auto request = value.update(none, nullptr, 4'010'000'000);
    require(request.level == runtime_app::ViewportLevel::Precision);
}

void test_edge_loss_opens_rescue_viewport() {
    auto value = controller();
    auto edge = observation(6, 35, 4'500'000'000, 120.0f, 0.0f, 70.0f, 110.0f);
    (void)value.update(observed_plan(6, 35), &edge, 4'501'000'000);
    const auto request =
        value.update(coasting_plan(6, 35), nullptr, 4'520'000'000);
    require(request.level == runtime_app::ViewportLevel::Rescue);
}

void test_target_growth_prediction_promotes_before_boundary() {
    auto value = controller();
    auto first = observation(7, 40, 5'000'000'000, 70.0f, 0.0f, 80.0f, 100.0f);
    (void)value.update(observed_plan(7, 40), &first, 5'001'000'000);
    auto growing = observation(7, 41, 5'020'000'000, 70.0f, 0.0f, 120.0f, 150.0f);
    require(
        value.update(observed_plan(7, 41), &growing, 5'021'000'000).level ==
        runtime_app::ViewportLevel::Precision);
    auto still_growing =
        observation(7, 42, 5'040'000'000, 70.0f, 0.0f, 130.0f, 165.0f);
    const auto request =
        value.update(observed_plan(7, 42), &still_growing, 5'041'000'000);
    require(request.level != runtime_app::ViewportLevel::Precision);
}

}  // namespace

int main() {
    test_starts_precision_and_expands_after_two_distinct_frames();
    test_severe_overflow_expands_immediately_to_rescue();
    test_shrink_is_delayed_and_one_level_at_a_time();
    test_no_target_returns_to_precision();
    test_edge_loss_opens_rescue_viewport();
    test_target_growth_prediction_promotes_before_boundary();
    return 0;
}
