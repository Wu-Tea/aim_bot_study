#include "intent_filter.h"

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

}  // namespace

int main() {
    try {
        test_deadzone_sized_drift_is_neutral();
        test_sustained_input_is_not_learned_away();
        test_reversal_and_release_are_explicit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[IntentFilterTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
