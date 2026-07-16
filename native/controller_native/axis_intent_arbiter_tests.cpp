#include "axis_intent_arbiter.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) throw std::runtime_error(message);
}

controller_native::AxisIntentInput observed(float error, float requested, float manual = 0.0f) {
    controller_native::AxisIntentInput input;
    input.error = error;
    input.requested_assist = requested;
    input.manual = manual;
    input.manual_confidence = std::fabs(manual) > 0.02f ? 1.0f : 0.0f;
    input.reliability = 1.0f;
    input.target_id = 7;
    input.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    input.mode = pipeline_contract::ControlMode::AdsAcquire;
    return input;
}

void test_drift_does_not_change_assist() {
    controller_native::AxisIntentArbiter arbiter;
    auto input = observed(80.0f, 0.7f, 0.0118f);
    input.manual_confidence = 0.0f;
    const auto decision = arbiter.update(controller_native::Axis::X, input, 0.001f);
    require_near(decision.assist_output, 0.7f, 0.0001f,
                 "drift must not change assist");
    require_near(decision.wrong_way_budget, 1.0f, 0.0001f,
                 "drift must not arm final limiting");
}

void test_helpful_manual_reduces_stacking_but_preserves_far_ads_floor() {
    controller_native::AxisIntentArbiter arbiter;
    const auto decision = arbiter.update(
        controller_native::Axis::X,
        observed(100.0f, 0.8f, 0.4f),
        0.001f);
    require_true(decision.assist_output < 0.8f,
                 "helpful manual must reduce duplicate assist");
    require_true(decision.assist_output >= 0.4f,
                 "far ADS must retain a strong assist floor");
}

void test_stable_observed_crossing_limits_wrong_way_budget() {
    controller_native::AxisIntentArbiter arbiter;
    auto before = observed(4.0f, 0.2f, 0.30f);
    before.error_rate = -420.0f;
    arbiter.update(controller_native::Axis::X, before, 0.010f);
    auto crossed = observed(-3.0f, -0.2f, 0.30f);
    crossed.error_rate = -260.0f;
    controller_native::AxisDecision decision;
    for (int i = 0; i < 3; ++i) {
        decision = arbiter.update(controller_native::Axis::X, crossed, 0.010f);
    }
    require_true(decision.divergence_risk > 0.5f,
                 "stable crossing must build divergence risk");
    require_true(decision.wrong_way_budget < 0.15f,
                 "stable crossing must bound wrong-way net output");
}

void test_coasting_never_limits_manual_output() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(4.0f, 0.2f, 0.3f), 0.010f);
    auto crossed = observed(-3.0f, -0.2f, 0.3f);
    crossed.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto decision = arbiter.update(controller_native::Axis::X, crossed, 0.010f);
    require_near(decision.wrong_way_budget, 1.0f, 0.0001f,
                 "coasting must not limit manual output");
}

void test_geometry_jump_disables_final_output_limit() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(4.0f, 0.2f, 0.3f), 0.010f);
    auto crossed = observed(-3.0f, -0.2f, 0.3f);
    crossed.target_innovation_px = 80.0f;
    crossed.normalized_size_change = 0.30f;
    const auto decision = arbiter.update(controller_native::Axis::X, crossed, 0.010f);
    require_near(decision.wrong_way_budget, 1.0f, 0.0001f,
                 "geometry jump must not limit manual output");
}

void test_manual_escape_bypasses_one_axis_only() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(4.0f, 0.2f, 0.3f), 0.010f);
    auto crossed = observed(-3.0f, -0.2f, 0.60f);
    crossed.manual_escape_threshold = 0.45f;
    const auto x = arbiter.update(controller_native::Axis::X, crossed, 0.010f);
    const auto y = arbiter.update(
        controller_native::Axis::Y,
        observed(30.0f, 0.4f, 0.0f),
        0.010f);
    require_near(x.wrong_way_budget, 1.0f, 0.0001f,
                 "strong X escape must bypass X limiting");
    require_near(y.assist_output, 0.4f, 0.0001f,
                 "X escape must not alter Y state");
}

void test_target_change_clears_crossing_evidence() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(4.0f, 0.2f, 0.3f), 0.010f);
    auto new_target = observed(-3.0f, -0.2f, 0.3f);
    new_target.target_id = 8;
    const auto decision = arbiter.update(controller_native::Axis::X, new_target, 0.010f);
    require_near(decision.wrong_way_budget, 1.0f, 0.0001f,
                 "target change must clear crossing evidence");
}

}  // namespace

int main() {
    try {
        test_drift_does_not_change_assist();
        test_helpful_manual_reduces_stacking_but_preserves_far_ads_floor();
        test_stable_observed_crossing_limits_wrong_way_budget();
        test_coasting_never_limits_manual_output();
        test_geometry_jump_disables_final_output_limit();
        test_manual_escape_bypasses_one_axis_only();
        test_target_change_clears_crossing_evidence();
        std::cout << "[AxisIntentArbiterTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AxisIntentArbiterTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
