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

controller_native::AxisIntentInput observed(float error, float manual) {
    controller_native::AxisIntentInput input;
    input.error = error;
    input.manual = manual;
    input.manual_confidence = 0.8f;
    input.reliability = 1.0f;
    input.target_id = 7;
    input.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    input.mode = pipeline_contract::ControlMode::AdsAcquire;
    return input;
}

void test_normal_and_helpful_input_leave_baseline_confidence_unchanged() {
    controller_native::AxisIntentArbiter arbiter;
    const auto neutral = arbiter.update(
        controller_native::Axis::X, observed(40.0f, 0.0f), 0.001f);
    const auto helpful = arbiter.update(
        controller_native::Axis::X, observed(35.0f, 0.3f), 0.001f);
    require_near(neutral.manual_yield_confidence, 0.8f, 0.0001f,
                 "neutral input must preserve baseline confidence");
    require_near(helpful.manual_yield_confidence, 0.8f, 0.0001f,
                 "helpful input must preserve baseline confidence");
    require_true(!helpful.intervention,
                 "helpful input must not enter intervention path");
}

void test_confirmed_worsening_wrong_way_input_disables_manual_yield() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(10.0f, -0.30f), 0.001f);
    auto worsening = observed(13.0f, -0.30f);
    worsening.error_rate = 220.0f;
    const auto decision = arbiter.update(controller_native::Axis::X, worsening, 0.001f);
    require_true(decision.intervention,
                 "stable worsening wrong-way input must intervene");
    require_near(decision.manual_yield_confidence, 0.0f, 0.0001f,
                 "confirmed wrong-way input must stop suppressing AI");
    require_true(decision.reason == controller_native::AxisDecisionReason::ConfirmedWrongWay,
                 "intervention reason must be explicit");
}

void test_wrong_way_without_worsening_does_not_intervene() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(20.0f, -0.25f), 0.001f);
    auto closing = observed(18.0f, -0.25f);
    closing.error_rate = -180.0f;
    const auto decision = arbiter.update(controller_native::Axis::X, closing, 0.001f);
    require_true(!decision.intervention,
                 "wrong-way sign alone must not override baseline yielding");
    require_near(decision.manual_yield_confidence, 0.8f, 0.0001f,
                 "unconfirmed wrong-way input must preserve baseline confidence");
}

void test_confirmed_intervention_bridges_only_one_vision_interval() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(10.0f, -0.30f), 0.001f);
    auto worsening = observed(13.0f, -0.30f);
    worsening.error_rate = 220.0f;
    require_true(arbiter.update(controller_native::Axis::X, worsening, 0.001f).intervention,
                 "observed evidence must start the short hold");
    auto coasting = worsening;
    coasting.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    require_true(arbiter.update(controller_native::Axis::X, coasting, 0.005f).intervention,
                 "confirmed evidence must bridge the normal inter-frame gap");
    require_true(!arbiter.update(controller_native::Axis::X, coasting, 0.010f).intervention,
                 "inter-frame hold must expire during a real observation gap");
}

void test_coasting_geometry_jump_and_escape_never_intervene() {
    for (int kind = 0; kind < 3; ++kind) {
        controller_native::AxisIntentArbiter arbiter;
        arbiter.update(controller_native::Axis::X, observed(10.0f, -0.30f), 0.001f);
        auto input = observed(13.0f, -0.30f);
        input.error_rate = 220.0f;
        if (kind == 0) input.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
        if (kind == 1) {
            input.target_innovation_px = 80.0f;
            input.normalized_size_change = 0.30f;
        }
        if (kind == 2) {
            input.manual = -0.60f;
            input.manual_escape_threshold = 0.45f;
        }
        const auto decision = arbiter.update(controller_native::Axis::X, input, 0.001f);
        require_true(!decision.intervention,
                     "ambiguous evidence or escape must not intervene");
        require_near(decision.manual_yield_confidence, 0.8f, 0.0001f,
                     "ambiguous evidence or escape must preserve baseline confidence");
    }
}

void test_target_change_resets_evidence_and_axes_are_independent() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(10.0f, -0.30f), 0.001f);
    auto new_target = observed(13.0f, -0.30f);
    new_target.error_rate = 220.0f;
    new_target.target_id = 8;
    const auto x = arbiter.update(controller_native::Axis::X, new_target, 0.001f);
    const auto y = arbiter.update(
        controller_native::Axis::Y, observed(30.0f, 0.20f), 0.001f);
    require_true(!x.intervention, "target change must reset X evidence");
    require_true(!y.intervention, "X history must not affect Y");
}

}  // namespace

int main() {
    try {
        test_normal_and_helpful_input_leave_baseline_confidence_unchanged();
        test_confirmed_worsening_wrong_way_input_disables_manual_yield();
        test_wrong_way_without_worsening_does_not_intervene();
        test_confirmed_intervention_bridges_only_one_vision_interval();
        test_coasting_geometry_jump_and_escape_never_intervene();
        test_target_change_resets_evidence_and_axes_are_independent();
        std::cout << "[AxisIntentArbiterTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AxisIntentArbiterTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
