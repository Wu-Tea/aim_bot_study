#pragma once

#include "pipeline_contract/target_plan.h"

#include <array>

namespace controller_native {

inline constexpr std::size_t kCausalMixHorizonCount = 4;

struct CausalMixInput {
    pipeline_contract::Vec2f error_px{};
    std::array<pipeline_contract::Vec2f, kCausalMixHorizonCount> route{};
    std::array<pipeline_contract::Vec2f, kCausalMixHorizonCount>
        pending_camera_px{};
    pipeline_contract::Vec2f manual_stick{};
    pipeline_contract::Vec2f ai_stick{};
    float response_scale_px_per_stick_second = 0.0f;
    float response_confidence = 0.0f;
    float reliability = 0.0f;
    bool pending_valid = false;
    bool fresh_single_target = false;
};

struct CausalMixResult {
    bool valid = false;
    float radial_manual_weight = 1.0f;
    float tangential_manual_weight = 1.0f;
    float ai_weight = 0.0f;
    float full_mix_post_cross_debt_px = 0.0f;
};

class CausalMixEvaluator {
public:
    static CausalMixResult evaluate(const CausalMixInput& input) noexcept;
};

}  // namespace controller_native
