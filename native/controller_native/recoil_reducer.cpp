#include "recoil_reducer.h"

namespace controller_native {

pipeline_contract::RecoilContribution RecoilReducer::reduce(
    bool effective_fire,
    bool aiming,
    double now_seconds,
    pipeline_contract::EventSequence cause_event) {
    NativeRecoilInput input{};
    input.fire_active = effective_fire;
    input.aiming = aiming;
    input.now_seconds = now_seconds;
    const auto output = policy_.compute(input);
    pipeline_contract::RecoilContribution contribution{};
    contribution.cause_event = cause_event;
    if (!output.recoil_active) return contribution;
    contribution.stick_delta = {
        output.recoil_stick.x,
        output.recoil_stick.y};
    contribution.active = true;
    return contribution;
}

}  // namespace controller_native
