#include "recoil_reducer.h"

#include <algorithm>

namespace controller_native {

RecoilReducer::RecoilReducer(const GamepadRecoilConfig& config)
    : enabled_(config.enabled) {
    // Legacy amount is an alias, not a second hidden output envelope.
    amount_ = config.output_amount >= 0.f ? config.output_amount : config.feedback_amount;
    hipfire_multiplier_ = std::clamp(config.hipfire_multiplier, 0.0f, 1.0f);
}

pipeline_contract::RecoilContribution RecoilReducer::reduce(
    bool effective_fire,
    bool aiming,
    double /*now_seconds*/,
    pipeline_contract::EventSequence cause_event) {
    pipeline_contract::RecoilContribution contribution{};
    contribution.cause_event = cause_event;
    if (!enabled_ || !effective_fire) return contribution;
    // Preserve the configured ADS amount exactly; hipfire applies only its
    // explicit multiplier. Manual-fire AI activation is not physical ADS.
    contribution.stick_delta = {0.0f, -amount_ * (aiming ? 1.0f : hipfire_multiplier_)};
    contribution.active = true;
    return contribution;
}

}  // namespace controller_native
