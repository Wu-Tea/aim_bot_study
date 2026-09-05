#include "recoil_reducer.h"

#include <algorithm>

namespace controller_native {

RecoilReducer::RecoilReducer(const GamepadRecoilConfig& config)
    : enabled_(config.enabled) {
    const float minimum = std::max(0.0f, config.feedback_min_amount);
    const float maximum = std::max(minimum, config.feedback_max_amount);
    amount_ = std::clamp(config.feedback_amount, minimum, maximum);
}

pipeline_contract::RecoilContribution RecoilReducer::reduce(
    bool effective_fire,
    bool /*aiming*/,
    double /*now_seconds*/,
    pipeline_contract::EventSequence cause_event) {
    pipeline_contract::RecoilContribution contribution{};
    contribution.cause_event = cause_event;
    if (!enabled_ || !effective_fire) return contribution;
    contribution.stick_delta = {0.0f, -amount_};
    contribution.active = true;
    return contribution;
}

}  // namespace controller_native
