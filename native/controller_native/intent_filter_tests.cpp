#include "intent_filter.h"
#include "assist_control_state_machine.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) throw std::runtime_error(message);
}

void test_deadzone_sized_drift_is_neutral() {
    controller_native::IntentFilter filter;
    pipeline_contract::IntentState state{};
    for (int i = 0; i < 200; ++i) {
        state = filter.update({0.0f, 0.0f}, {-0.0118f, 0.004f}, false, false, i * 0.001);
    }
    require_true(state.right_phase == pipeline_contract::StickPhase::Neutral,
                 "deadzone drift must remain neutral");
    require_near(state.filtered_right.x, 0.0f, 0.002f,
                 "deadzone drift must not become correction intent");
    require_near(state.raw_right.x, -0.0118f, 0.00001f,
                 "intent filtering must preserve raw input");
}

void test_sustained_input_is_not_learned_away() {
    controller_native::IntentFilter filter;
    pipeline_contract::IntentState state{};
    for (int i = 0; i < 20; ++i) {
        state = filter.update({0.0f, 0.0f}, {0.7f, 0.0f}, true, false, i * 0.01);
    }
    require_true(state.right_phase == pipeline_contract::StickPhase::Sustained,
                 "held right input must become sustained intent");
    require_true(state.filtered_right.x > 0.6f,
                 "neutral learner must not absorb sustained input");
    require_true(state.right_confidence > 0.8f,
                 "strong held input must be high confidence");
}

void test_reversal_and_release_are_explicit() {
    controller_native::IntentFilter filter;
    filter.update({0.6f, 0.0f}, {0.0f, 0.0f}, true, false, 0.00);
    filter.update({0.6f, 0.0f}, {0.0f, 0.0f}, true, false, 0.01);
    auto state = filter.update({-0.6f, 0.0f}, {0.0f, 0.0f}, true, false, 0.02);
    require_true(state.left_phase == pipeline_contract::StickPhase::Reversal,
                 "sign change must report reversal");
    state = filter.update({0.0f, 0.0f}, {0.0f, 0.0f}, true, false, 0.03);
    require_true(state.left_phase == pipeline_contract::StickPhase::Release,
                 "return to neutral must report release");
    state = filter.update({0.0f, 0.0f}, {0.0f, 0.0f}, true, false, 0.04);
    require_true(state.left_phase == pipeline_contract::StickPhase::Neutral,
                 "release is a single transition state");
}

void test_gesture_purpose_survives_target_acquisition() {
    controller_native::IntentFilter filter;
    auto state = filter.update(
        {0.0f, 0.0f}, {0.30f, -0.20f}, true, false, 0.00,
        false, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Onset &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "gesture begun without I must be acquisition");

    state = filter.update(
        {0.0f, 0.0f}, {0.30f, -0.20f}, true, false, 0.01,
        true, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Sustained &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "held acquisition gesture was reinterpreted after I appeared");

    (void)filter.update(
        {0.0f, 0.0f}, {}, true, false, 0.02, true, false);
    state = filter.update(
        {0.0f, 0.0f}, {0.30f, -0.20f}, true, false, 0.03,
        true, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Onset &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
        "new gesture on owned I must correct current target");
}

void test_two_dimensional_reversal_starts_new_purpose() {
    controller_native::IntentFilter filter;
    (void)filter.update(
        {}, {0.40f, 0.20f}, true, false, 0.00, false, false);
    const auto state = filter.update(
        {}, {-0.10f, -0.50f}, true, false, 0.01, true, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Reversal &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
        "2-D reversal must start a correction gesture on owned I");
}

void test_correction_purpose_does_not_cross_target_loss() {
    controller_native::IntentFilter filter;
    auto state = filter.update(
        {}, {0.25f, -0.20f}, true, false, 0.00, true, false);
    require_true(
        state.right_purpose ==
            pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget,
        "owned gesture did not begin as current-target correction");

    state = filter.update(
        {}, {0.25f, -0.20f}, true, false, 0.01, false, false);
    require_true(
        state.right_phase == pipeline_contract::StickPhase::Sustained &&
            state.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "held correction leaked across target loss");

    state = filter.update(
        {}, {0.25f, -0.20f}, true, false, 0.02, true, false);
    require_true(
        state.right_purpose ==
            pipeline_contract::UserAimIntentPurpose::AcquireTarget,
        "next target inherited the prior target's correction purpose");
}

void test_carried_axis_release_continuity(const native_test::TestContext& context) {
    float maximum_step = 0.0f;
    float maximum_neutral_loss = 0.0f;
    int neutral_samples = 0;
    int held_samples = 0;
    int worst_tick = 0, worst_axis = 0;
    float worst_physical = 0, worst_centered = 0, worst_activity = 0, worst_previous = 0, worst_final = 0;
    // Sweep physical release through calibrated/adaptive deadzones, both axes
    // and signs, with and without firing. This checks delivered behavior, not
    // the activity formula, and protects a real held opposing gesture.
    for (int axis : {0, 1}) for (float sign : {-1.0f, 1.0f})
    for (float deadzone : {.02f, .06f}) for (float bias : {-.012f, .012f})
    for (bool firing : {false, true}) {
        controller_native::IntentFilterConfig config;
        config.base_deadzone = deadzone;
        controller_native::IntentFilter filter(config);
        const auto vec = [axis](float value) {
            return axis == 0 ? pipeline_contract::Vec2f{value, 0}
                             : pipeline_contract::Vec2f{0, value};
        };
        for (int i = 0; i < 200; ++i)
            (void)filter.update({}, vec(bias), false, false, i * .001);
        controller_native::AssistControlStateMachine machine;
        float previous = 0;
        for (int i = 0; i <= 360; ++i) {
            const float physical = bias + sign * (.18f - i * .0005f);
            const auto intent = filter.update({}, vec(physical), true, firing,
                                               1 + i * .001, i > 0);
            const auto& state = axis == 0 ? intent.right_x : intent.right_y;
            controller_native::AssistControlStateMachineInput input;
            input.aiming = input.target_authoritative = input.fresh_observation = true;
            input.target_id = 172;
            input.selector_target_generation = 8;
            input.now_seconds = 1 + i * .001;
            input.mode = pipeline_contract::ControlMode::BodyLockFollow;
            input.visual_authority = 1;
            input.firing = firing;
            input.target_error_px = vec(-sign * 32 * (axis == 1 ? -1 : 1));
            input.manual_stick = vec(physical);
            input.centered_manual_available = true;
            input.centered_manual_stick = vec(physical - state.neutral_bias);
            input.filtered_manual_stick = intent.filtered_right;
            input.manual_axis_activity = {intent.right_x.activity, intent.right_y.activity};
            input.carried_acquisition_gesture = intent.right_purpose ==
                pipeline_contract::UserAimIntentPurpose::AcquireTarget;
            input.ai_stick = vec(-sign * .5f);
            const auto output = machine.update(input);
            const float final = axis == 0 ? output.stick.x : output.stick.y;
            require_true(input.carried_acquisition_gesture,
                         "release sweep changed gesture purpose unexpectedly");
            if (i == 0) {
                require_near(final, physical, 1e-5f, "held gesture lost physical authority");
                ++held_samples;
            } else {
                if (std::fabs(final - previous) > maximum_step) {
                    worst_tick = i;
                    worst_axis = axis;
                    worst_physical = physical;
                    worst_centered = physical - state.neutral_bias;
                    worst_activity = state.activity;
                    worst_previous = previous;
                    worst_final = final;
                }
                maximum_step = std::max(maximum_step, std::fabs(final - previous));
            }
            if (state.filtered == 0) {
                maximum_neutral_loss = std::max(maximum_neutral_loss,
                    std::fabs(final + sign * .5f));
                ++neutral_samples;
            }
            previous = final;
        }
    }
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("carry-release-continuity.json"));
    report << std::setprecision(9) << "{\"max_output_step\":" << maximum_step
           << ",\"max_neutral_loss\":" << maximum_neutral_loss
           << ",\"neutral_samples\":" << neutral_samples
           << ",\"held_samples\":" << held_samples << "}\n";
    std::cout << "[CarryReleaseSweep] max_step=" << maximum_step
              << " tick=" << worst_tick << " axis=" << worst_axis
              << " physical=" << worst_physical << " centered=" << worst_centered
              << " activity=" << worst_activity << " previous=" << worst_previous
              << " final=" << worst_final << '\n';
    report.close();
    require_true(held_samples == 32 && neutral_samples > 500,
                 "release sweep must cover material and neutral inputs");
    require_true(maximum_step < .04f, "release created a deadzone output cliff");
    require_true(maximum_neutral_loss < .001f, "noise-sized axis retained a carried-input veto");
}

}  // namespace

void register_intent_filter_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock", "carried_axis_release_continuity", test_carried_axis_release_continuity);
    registry.add_case("BaseBodyLock", "deadzone_sized_drift_is_neutral", test_deadzone_sized_drift_is_neutral);
    registry.add_case("BaseBodyLock", "sustained_input_is_not_learned_away", test_sustained_input_is_not_learned_away);
    registry.add_case("BaseBodyLock", "reversal_and_release_are_explicit", test_reversal_and_release_are_explicit);
    registry.add_case("BaseBodyLock", "gesture_purpose_survives_target_acquisition", test_gesture_purpose_survives_target_acquisition);
    registry.add_case("BaseBodyLock", "two_dimensional_reversal_starts_new_purpose", test_two_dimensional_reversal_starts_new_purpose);
    registry.add_case("BaseBodyLock", "correction_purpose_does_not_cross_target_loss", test_correction_purpose_does_not_cross_target_loss);
}
