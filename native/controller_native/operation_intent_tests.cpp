#include "operation_intent.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::ostringstream out;
        out << message << " expected=" << expected << " actual=" << actual;
        throw std::runtime_error(out.str());
    }
}

controller_native::OperationIntentInput base_input() {
    controller_native::OperationIntentInput input;
    input.aiming = true;
    input.target_owned = true;
    input.right_confidence = 1.0f;
    return input;
}

void test_no_gesture_when_no_material_input() {
    auto input = base_input();
    input.target_owned = false;
    input.filtered_right_x = 0.01f;
    input.filtered_right_y = -0.01f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::NoGesture,
        "sub-deadzone input should be NoGesture");
    require_near(output.direction_trust, 0.50f, 0.001f, "NoGesture trust is neutral");
}

void test_acquire_flick_on_deliberate_onset_push() {
    auto input = base_input();
    input.right_purpose =
        pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    input.right_phase = pipeline_contract::StickPhase::Onset;
    input.filtered_right_x = 0.60f;
    input.filtered_right_y = 0.10f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class ==
            controller_native::OperationClass::AcquireFlick,
        "high-magnitude onset push should classify as AcquireFlick");
    require_true(output.direction_trust > 0.70f, "flick direction is trusted");
}

void test_recoil_pull_while_firing_and_pulling_down() {
    auto input = base_input();
    input.firing = true;
    input.filtered_right_y = -0.30f;
    controller_native::OperationIntentClassifier classifier;
    const auto first = classifier.classify(input);
    require_true(
        first.operation_class == controller_native::OperationClass::RecoilPull,
        "firing + downward filtered pull should classify as RecoilPull");
    require_near(first.recoil_pull_strength, 0.30f, 0.001f, "pull strength tracks downward y");
    require_true(first.recoil_pull_onset, "first firing tick reports onset");

    const auto second = classifier.classify(input);
    require_true(
        second.operation_class == controller_native::OperationClass::RecoilPull,
        "steady pull stays RecoilPull");
    require_true(!second.recoil_pull_onset, "sustained firing does not re-report onset");
}

void test_follow_track_when_assist_holds_and_user_is_light() {
    auto input = base_input();
    input.filtered_right_x = 0.02f;
    input.filtered_right_y = -0.02f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::FollowTrack,
        "target owned with light manual should classify as FollowTrack");
}

void test_lead_track_when_crosshair_rides_ahead_of_fast_target() {
    auto input = base_input();
    input.filtered_right_x = 0.02f;
    input.target_velocity_px_per_sec = 320.0f;
    input.error_along_motion_px = 22.0f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::LeadTrack,
        "fast target with crosshair ahead should classify as LeadTrack");
}

void test_slow_target_never_classifies_as_lead() {
    auto input = base_input();
    input.filtered_right_x = 0.02f;
    input.target_velocity_px_per_sec = 40.0f;
    input.error_along_motion_px = 22.0f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::FollowTrack,
        "slow target must degrade to FollowTrack, never LeadTrack");
}

void test_correct_track_for_bounded_desired_point_push() {
    auto input = base_input();
    input.manual_correction = true;
    input.filtered_right_x = 0.22f;
    input.filtered_right_y = 0.08f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::CorrectTrack,
        "bounded D-correction push should classify as CorrectTrack");
}

void test_handover_intent_is_explicit() {
    auto input = base_input();
    input.right_purpose =
        pipeline_contract::UserAimIntentPurpose::HandoverTarget;
    input.filtered_right_x = 0.70f;
    input.filtered_right_y = -0.40f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class ==
            controller_native::OperationClass::HandoverIntent,
        "explicit handover purpose wins over any stick gesture");
}

void test_unreliable_when_material_input_matches_no_template() {
    auto input = base_input();  // target owned: active assistance context
    input.right_purpose =
        pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    input.right_phase = pipeline_contract::StickPhase::Neutral;
    input.filtered_right_x = 0.55f;
    input.filtered_right_y = 0.20f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::Unreliable,
        "material push matching no template during assistance must be Unreliable");
    require_near(output.direction_trust, 0.10f, 0.001f, "Unreliable drives trust to floor");
}

void test_free_look_without_assistance_is_not_unreliable() {
    // Free look (no target, no fire) is not assistance and not degraded intent:
    // material input here is NoGesture, so telemetry never counts it as a
    // failure. Unreliable is reserved for input that fights active assistance.
    auto input = base_input();
    input.target_owned = false;
    input.firing = false;
    input.right_purpose =
        pipeline_contract::UserAimIntentPurpose::AcquireTarget;
    input.right_phase = pipeline_contract::StickPhase::Neutral;
    input.filtered_right_x = 0.55f;
    input.filtered_right_y = 0.20f;
    controller_native::OperationIntentClassifier classifier;
    const auto output = classifier.classify(input);
    require_true(
        output.operation_class == controller_native::OperationClass::NoGesture,
        "material input with no assistance context must stay NoGesture");
    require_near(output.direction_trust, 0.50f, 0.001f, "free look keeps neutral trust");
}

void test_degraded_firing_flail_is_unreliable_with_collapsing_trust() {
    // The design's degraded scenario: user low-blood-sugar, pushing nearly all
    // wrong directions while firing. No single push should be re-labelled as a
    // trusted operation, and repeated reversals must collapse direction trust.
    auto input = base_input();
    input.firing = true;
    input.right_purpose =
        pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    input.right_phase = pipeline_contract::StickPhase::Sustained;

    controller_native::OperationIntentClassifier classifier;
    float worst_trust = 1.0f;
    bool saw_unreliable = false;
    for (const float direction : {-0.6f, 0.6f, -0.5f, 0.7f, -0.4f, 0.5f}) {
        input.filtered_right_x = direction;
        input.filtered_right_y = 0.0f;
        const auto output = classifier.classify(input);
        if (output.operation_class ==
            controller_native::OperationClass::Unreliable) {
            saw_unreliable = true;
        }
        worst_trust = std::min(worst_trust, output.direction_trust);
    }
    require_true(saw_unreliable, "flailing horizontal pushes must surface Unreliable");
    require_true(worst_trust < 0.15f, "repeated reversals collapse direction trust");
}

void test_reset_clears_firing_edge() {
    auto input = base_input();
    input.firing = true;
    input.filtered_right_y = -0.30f;
    controller_native::OperationIntentClassifier classifier;
    const auto first = classifier.classify(input);
    require_true(first.recoil_pull_onset, "first tick reports onset before reset");
    classifier.reset();
    const auto after = classifier.classify(input);
    require_true(after.recoil_pull_onset, "reset restores the firing-edge state");
}

void test_class_names_are_stable() {
    require_true(
        std::string(controller_native::operation_class_name(
            controller_native::OperationClass::Unreliable)) == "unreliable",
        "Unreliable serializes to a stable token");
    require_true(
        std::string(controller_native::operation_class_name(
            controller_native::OperationClass::RecoilPull)) == "recoil_pull",
        "RecoilPull serializes to a stable token");
    require_true(
        std::string(controller_native::operation_class_name(
            controller_native::OperationClass::NoGesture)) == "no_gesture",
        "NoGesture serializes to a stable token");
}

}  // namespace

void register_operation_intent_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "no_gesture_when_no_material_input", test_no_gesture_when_no_material_input);
    registry.add_case("BaseBodyLock", "acquire_flick_on_deliberate_onset_push", test_acquire_flick_on_deliberate_onset_push);
    registry.add_case("BaseBodyLock", "recoil_pull_while_firing_and_pulling_down", test_recoil_pull_while_firing_and_pulling_down);
    registry.add_case("BaseBodyLock", "follow_track_when_assist_holds_and_user_is_light", test_follow_track_when_assist_holds_and_user_is_light);
    registry.add_case("BaseBodyLock", "lead_track_when_crosshair_rides_ahead_of_fast_target", test_lead_track_when_crosshair_rides_ahead_of_fast_target);
    registry.add_case("BaseBodyLock", "slow_target_never_classifies_as_lead", test_slow_target_never_classifies_as_lead);
    registry.add_case("BaseBodyLock", "correct_track_for_bounded_desired_point_push", test_correct_track_for_bounded_desired_point_push);
    registry.add_case("BaseBodyLock", "handover_intent_is_explicit", test_handover_intent_is_explicit);
    registry.add_case("BaseBodyLock", "unreliable_when_material_input_matches_no_template", test_unreliable_when_material_input_matches_no_template);
    registry.add_case("BaseBodyLock", "free_look_without_assistance_is_not_unreliable", test_free_look_without_assistance_is_not_unreliable);
    registry.add_case("BaseBodyLock", "degraded_firing_flail_collapses_trust", test_degraded_firing_flail_is_unreliable_with_collapsing_trust);
    registry.add_case("BaseBodyLock", "reset_clears_firing_edge", test_reset_clears_firing_edge);
    registry.add_case("BaseBodyLock", "operation_class_names_are_stable", test_class_names_are_stable);
}
