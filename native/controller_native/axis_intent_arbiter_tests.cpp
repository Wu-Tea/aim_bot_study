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
    input.manual_preservation_floor = 0.65f;
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

void test_confirmed_wrong_way_retention_attacks_floor_and_releases_smoothly() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(10.0f, -0.30f), 0.001f);
    auto worsening = observed(13.0f, -0.30f);
    worsening.error_rate = 220.0f;
    auto decision = arbiter.update(controller_native::Axis::X, worsening, 0.001f);
    require_near(decision.manual_retention, 0.85f, 0.001f,
                 "first confirmed frame must preserve 85 percent manual input");

    float previous = decision.manual_retention;
    for (int tick = 0; tick < 20; ++tick) {
        worsening.error += 1.1f;
        decision = arbiter.update(controller_native::Axis::X, worsening, 0.001f);
        require_true(decision.manual_retention <= previous + 0.0001f,
                     "sustained wrong-way retention must not rise");
        require_true(decision.manual_retention >= 0.65f - 0.0001f,
                     "retention must never fall below configured floor");
        previous = decision.manual_retention;
    }
    require_true(decision.manual_retention < 0.80f,
                 "sustained evidence must materially approach the floor");

    auto helpful = observed(10.0f, 0.30f);
    const auto releasing = arbiter.update(
        controller_native::Axis::X, helpful, 0.010f);
    require_true(releasing.manual_retention > decision.manual_retention &&
                 releasing.manual_retention < 1.0f,
                 "release must return manual authority without a step");

    auto escape = worsening;
    escape.manual = -0.60f;
    escape.manual_escape_threshold = 0.45f;
    require_near(
        arbiter.update(controller_native::Axis::X, escape, 0.001f).manual_retention,
        1.0f, 0.0001f,
        "explicit escape must immediately restore full manual authority");
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

void test_small_predictive_manual_input_is_never_attenuated() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(10.0f, -0.24f), 0.001f);
    auto worsening = observed(13.0f, -0.24f);
    worsening.error_rate = 220.0f;
    require_true(
        !arbiter.update(
            controller_native::Axis::X, worsening, 0.001f).intervention,
        "sub-P25 predictive manual input must remain fully owned by the user");
}

void test_confirmed_wrong_way_state_survives_natural_magnitude_decay() {
    controller_native::AxisIntentArbiter arbiter;
    arbiter.update(controller_native::Axis::X, observed(10.0f, -0.30f), 0.001f);
    auto confirmed = observed(13.0f, -0.30f);
    confirmed.error_rate = 220.0f;
    require_true(
        arbiter.update(controller_native::Axis::X, confirmed, 0.001f).intervention,
        "strong wrong-way input must start confirmation");
    auto decaying = observed(15.0f, -0.20f);
    decaying.error_rate = 220.0f;
    require_true(
        arbiter.update(controller_native::Axis::X, decaying, 0.001f).intervention,
        "confirmed state must survive natural wrong-input magnitude decay");
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
    require_near(y.manual_retention, 1.0f, 0.0001f,
                 "X intervention must not attenuate Y");
}

}  // namespace

int main() {
    try {
        test_normal_and_helpful_input_leave_baseline_confidence_unchanged();
        test_confirmed_worsening_wrong_way_input_disables_manual_yield();
        test_confirmed_wrong_way_retention_attacks_floor_and_releases_smoothly();
        test_wrong_way_without_worsening_does_not_intervene();
        test_small_predictive_manual_input_is_never_attenuated();
        test_confirmed_wrong_way_state_survives_natural_magnitude_decay();
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
