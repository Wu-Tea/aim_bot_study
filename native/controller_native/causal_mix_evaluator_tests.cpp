#include "causal_mix_evaluator.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using controller_native::CausalMixEvaluator;
using controller_native::CausalMixInput;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

CausalMixInput vertical_crossing_input() {
    CausalMixInput input;
    input.error_px = {0.0f, 12.0f};
    input.manual_stick = {0.20f, 0.80f};
    input.ai_stick = {0.0f, 0.55f};
    input.response_scale_px_per_stick_second = 500.0f;
    input.response_confidence = 1.0f;
    input.reliability = 1.0f;
    input.fresh_single_target = true;
    for (std::size_t i = 0; i < input.route.size(); ++i) {
        input.route[i] = {0.0f, 12.0f};
    }
    return input;
}

void test_absolute_sustained_output_is_not_treated_as_zero_motion() {
    const auto result = CausalMixEvaluator::evaluate(vertical_crossing_input());
    require(result.valid, "reliable crossing input must be evaluated");
    require(result.full_mix_post_cross_debt_px > 20.0f,
            "absolute sustained output must create visible future debt");
    require(result.radial_manual_weight < 1.0f,
            "obsolete radial manual must be attenuated");
}

void test_pending_motion_reduces_remaining_required_push() {
    auto without_pending = vertical_crossing_input();
    auto with_pending = without_pending;
    with_pending.pending_valid = true;
    for (auto& sample : with_pending.pending_camera_px) {
        sample = {0.0f, 8.0f};
    }
    const auto plain = CausalMixEvaluator::evaluate(without_pending);
    const auto pending = CausalMixEvaluator::evaluate(with_pending);
    require(pending.full_mix_post_cross_debt_px >
                plain.full_mix_post_cross_debt_px,
            "already scheduled motion must increase crossing debt");
}

void test_tangential_manual_is_always_preserved() {
    const auto result = CausalMixEvaluator::evaluate(vertical_crossing_input());
    require(std::fabs(result.tangential_manual_weight - 1.0f) < 1e-6f,
            "causal radial correction must preserve tangent exactly");
}

void test_required_net_motion_produces_continuous_manual_weight() {
    auto input = vertical_crossing_input();
    input.ai_stick = {0.0f, 0.10f};
    const auto result = CausalMixEvaluator::evaluate(input);
    require(result.valid, "reliable crossing input must be evaluated");
    require(result.radial_manual_weight > 0.20f &&
                result.radial_manual_weight < 0.30f,
            "80ms required net motion must be solved as a continuous "
            "manual weight rather than a discrete preset");
}

void test_low_confidence_returns_safe_invalid_result() {
    auto input = vertical_crossing_input();
    input.response_confidence = 0.2f;
    const auto result = CausalMixEvaluator::evaluate(input);
    require(!result.valid, "weak response evidence must not change ownership");
    require(result.radial_manual_weight == 1.0f,
            "invalid evaluation must retain manual ownership");
}

}  // namespace

int main() {
    try {
        test_absolute_sustained_output_is_not_treated_as_zero_motion();
        test_pending_motion_reduces_remaining_required_push();
        test_tangential_manual_is_always_preserved();
        test_required_net_motion_produces_continuous_manual_weight();
        test_low_confidence_returns_safe_invalid_result();
        std::cout << "cod_native_causal_mix_evaluator_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_causal_mix_evaluator_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
