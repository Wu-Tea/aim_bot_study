#pragma once

#include <array>
#include <cstdint>

namespace control_learning {

struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

struct ResponseMatrix2d {
    std::array<std::array<double, 2>, 2> values{{{0.0, 0.0}, {0.0, 0.0}}};
};

enum class VisionSampleQuality : std::uint8_t {
    Invalid,
    Normal,
    ReusedOrProjected,
    IdentityTransition,
};

enum class IdentificationUpdateOutcome : std::uint8_t {
    NotAttempted,
    Accepted,
    InsufficientExcitation,
    HardRejected,
    NumericallyRejected,
};

enum IdentificationReasonBits : std::uint32_t {
    IdentificationReasonNone = 0,
    IdentificationReasonInvalidObservation = 1u << 0,
    IdentificationReasonCaptureGap = 1u << 1,
    IdentificationReasonIdentityChange = 1u << 2,
    IdentificationReasonAdsEpochChange = 1u << 3,
    IdentificationReasonDeliveryGap = 1u << 4,
    IdentificationReasonFiring = 1u << 5,
    IdentificationReasonRecoil = 1u << 6,
    IdentificationReasonSaturation = 1u << 7,
    IdentificationReasonLowExcitation = 1u << 8,
    IdentificationReasonCollinear = 1u << 9,
    IdentificationReasonNonFinite = 1u << 10,
};

struct SampleAssessment {
    VisionSampleQuality vision_quality = VisionSampleQuality::Invalid;
    IdentificationUpdateOutcome update_outcome =
        IdentificationUpdateOutcome::NotAttempted;
    std::uint32_t reason_bits = IdentificationReasonNone;
    bool accepted_by_any_delay = false;
    std::uint8_t accepted_delay_count = 0;
};

}  // namespace control_learning
