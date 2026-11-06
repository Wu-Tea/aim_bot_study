#pragma once

namespace pipeline_contract {
// Marker presence is deliberately absent from this policy. Friendly rejection
// happens before admission; model confidence alone distinguishes weak evidence.
struct PersonTargetPolicy {
    static constexpr float pickup_confidence = .42f;
    static constexpr float tracking_confidence = .40f;
    static constexpr float weak_confidence = .20f;
    static constexpr float single_frame_confidence = .50f;

    static bool admits(float confidence, bool tracking) noexcept {
        return confidence >= (tracking ? tracking_confidence : pickup_confidence);
    }
};
}
