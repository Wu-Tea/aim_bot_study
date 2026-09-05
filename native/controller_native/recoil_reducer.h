#pragma once

#include "runtime_config.h"
#include "../pipeline_contract/control_command.h"

namespace controller_native {

// Sole recoil state owner. Its input deliberately excludes target, D/R,
// manual stick, AI proposal and pre-recoil T.
class RecoilReducer {
public:
    explicit RecoilReducer(const GamepadRecoilConfig& config = {});

    void reset() {}

    pipeline_contract::RecoilContribution reduce(
        bool effective_fire,
        bool aiming,
        double now_seconds,
        pipeline_contract::EventSequence cause_event = {});

private:
    // Production recoil has no file-backed profile/recognizer dependency.
    // Legacy playback remains an offline library, never a live control mode.
    bool enabled_ = false;
    float amount_ = 0.0f;
};

}  // namespace controller_native
