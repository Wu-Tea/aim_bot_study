#pragma once

#include <cstdint>

namespace controller_native {

enum class AdsCompletionReason {
    None,
    Centered,
    Timeout,
    Released,
    TargetLost,
};

struct AdsCompletionGateInput {
    bool aiming = false;
    bool has_target_authority = false;
    bool has_strong_target = false;
    bool fresh_observation = false;
    std::uint64_t vision_sequence = 0;
    float dx = 0.0f;
    float dy = 0.0f;
    double now_seconds = 0.0;
};

struct AdsCompletionGateState {
    bool active = false;
    int centered_fresh_frames = 0;
    AdsCompletionReason reason = AdsCompletionReason::None;
};

class AdsCompletionGate {
public:
    AdsCompletionGate(float radius_px, int fresh_frames, float max_acquisition_ms);
    AdsCompletionGateState update(const AdsCompletionGateInput& input);
    void reset(AdsCompletionReason reason = AdsCompletionReason::None);
    const AdsCompletionGateState& state() const;

private:
    float radius_px_;
    int fresh_frames_;
    double max_acquisition_seconds_;
    AdsCompletionGateState state_;
    double started_at_seconds_ = 0.0;
    std::uint64_t last_vision_sequence_ = 0;
    bool completed_for_ads_hold_ = false;
};

const char* ads_completion_reason_name(AdsCompletionReason reason);

}  // namespace controller_native
