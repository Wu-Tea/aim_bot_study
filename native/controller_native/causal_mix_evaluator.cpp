#include "causal_mix_evaluator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace controller_native {
namespace {

float dot(pipeline_contract::Vec2f left,
          pipeline_contract::Vec2f right) noexcept {
    return left.x * right.x + left.y * right.y;
}

float length(pipeline_contract::Vec2f value) noexcept {
    return std::hypot(value.x, value.y);
}

bool finite_vec(pipeline_contract::Vec2f value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

pipeline_contract::Vec2f subtract(
    pipeline_contract::Vec2f left,
    pipeline_contract::Vec2f right) noexcept {
    return {left.x - right.x, left.y - right.y};
}

pipeline_contract::Vec2f candidate_output(
    const CausalMixInput& input,
    pipeline_contract::Vec2f radial,
    float radial_manual_weight) noexcept {
    const float manual_radial = dot(input.manual_stick, radial);
    const pipeline_contract::Vec2f radial_manual{
        radial.x * manual_radial, radial.y * manual_radial};
    const auto tangent = subtract(input.manual_stick, radial_manual);
    return {
        std::clamp(
            radial_manual.x * radial_manual_weight + tangent.x +
                input.ai_stick.x,
            -1.0f, 1.0f),
        std::clamp(
            radial_manual.y * radial_manual_weight + tangent.y +
                input.ai_stick.y,
            -1.0f, 1.0f),
    };
}

struct Score {
    float cost = std::numeric_limits<float>::infinity();
    float debt = 0.0f;
};

Score score(
    const CausalMixInput& input,
    pipeline_contract::Vec2f radial,
    float radial_manual_weight) noexcept {
    constexpr std::array<float, kCausalMixHorizonCount> kHorizons{
        0.040f, 0.080f, 0.120f, 0.160f};
    constexpr std::array<float, kCausalMixHorizonCount> kWeights{
        0.15f, 0.20f, 0.25f, 0.40f};
    const auto output = candidate_output(
        input, radial, radial_manual_weight);
    float cost = 0.0f;
    float debt = 0.0f;
    for (std::size_t index = 0; index < kHorizons.size(); ++index) {
        const pipeline_contract::Vec2f camera{
            output.x * input.response_scale_px_per_stick_second *
                kHorizons[index],
            output.y * input.response_scale_px_per_stick_second *
                kHorizons[index],
        };
        const auto pending = input.pending_valid
            ? input.pending_camera_px[index]
            : pipeline_contract::Vec2f{};
        const auto predicted = subtract(
            subtract(input.route[index], pending), camera);
        const float error = length(predicted);
        const float post_cross = std::max(0.0f, -dot(predicted, radial));
        cost += error * kWeights[index] + post_cross * 2.5f;
        debt += post_cross;
    }
    const float manual_radial = std::fabs(dot(input.manual_stick, radial));
    cost += manual_radial * (1.0f - radial_manual_weight) *
        input.response_scale_px_per_stick_second * 0.012f;
    return {cost, debt};
}

}  // namespace

CausalMixResult CausalMixEvaluator::evaluate(
    const CausalMixInput& input) noexcept {
    CausalMixResult result;
    const bool scalar_valid =
        std::isfinite(input.response_scale_px_per_stick_second) &&
        std::isfinite(input.response_confidence) &&
        std::isfinite(input.reliability);
    bool vectors_valid = finite_vec(input.error_px) &&
        finite_vec(input.manual_stick) && finite_vec(input.ai_stick);
    for (std::size_t index = 0; index < input.route.size(); ++index) {
        vectors_valid = vectors_valid && finite_vec(input.route[index]) &&
            (!input.pending_valid ||
             finite_vec(input.pending_camera_px[index]));
    }
    const float error_length = length(input.error_px);
    if (!scalar_valid || !vectors_valid || error_length <= 2.0f ||
        input.response_scale_px_per_stick_second <= 0.0f ||
        input.response_confidence < 0.50f || input.reliability < 0.85f ||
        !input.fresh_single_target) {
        return result;
    }

    const pipeline_contract::Vec2f radial{
        input.error_px.x / error_length, input.error_px.y / error_length};
    constexpr std::array<float, 4> kManualWeights{
        1.0f, 0.65f, 0.35f, 0.0f};
    const Score full_mix = score(input, radial, 1.0f);
    const auto full_output = candidate_output(input, radial, 1.0f);
    const pipeline_contract::Vec2f first_camera{
        full_output.x * input.response_scale_px_per_stick_second * 0.040f,
        full_output.y * input.response_scale_px_per_stick_second * 0.040f,
    };
    const auto first_pending = input.pending_valid
        ? input.pending_camera_px.front()
        : pipeline_contract::Vec2f{};
    const auto first_predicted = subtract(
        subtract(input.route.front(), first_pending), first_camera);
    const bool imminent_cross = dot(first_predicted, radial) < 0.0f;
    const bool currently_wrong_way =
        dot(input.manual_stick, radial) < -0.05f &&
        dot(input.ai_stick, radial) > 0.05f;
    if (!imminent_cross && !currently_wrong_way) {
        result.valid = true;
        result.ai_weight = 1.0f;
        result.full_mix_post_cross_debt_px = full_mix.debt;
        return result;
    }
    Score best = currently_wrong_way
        ? Score{}
        : full_mix;
    float best_weight = currently_wrong_way ? 0.35f : 1.0f;
    for (const float weight : kManualWeights) {
        if (currently_wrong_way && weight > 0.35f) continue;
        const Score candidate = score(input, radial, weight);
        if (candidate.cost + 0.01f < best.cost) {
            best = candidate;
            best_weight = weight;
        }
    }
    result.valid = true;
    result.radial_manual_weight = best_weight;
    result.tangential_manual_weight = 1.0f;
    result.ai_weight = 1.0f;
    result.full_mix_post_cross_debt_px = full_mix.debt;
    return result;
}

}  // namespace controller_native
