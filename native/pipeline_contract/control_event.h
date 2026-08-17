#pragma once

// Small identity/value types shared by reducer snapshots and output commands.
// Control events are represented by explicit reducer snapshots; there is no
// parallel runtime event bus or shadow event buffer.

#include <cstdint>

namespace pipeline_contract {

template <typename Tag>
struct ControlIdentity {
    bool valid = false;
    std::uint64_t value = 0;

    constexpr ControlIdentity() noexcept = default;
    constexpr explicit ControlIdentity(std::uint64_t raw) noexcept
        : valid(raw != 0), value(raw) {}

    static constexpr ControlIdentity none() noexcept { return {}; }
    static constexpr ControlIdentity from(std::uint64_t raw) noexcept {
        return ControlIdentity(raw);
    }

    friend constexpr bool operator==(
        ControlIdentity left, ControlIdentity right) noexcept {
        return left.valid == right.valid && left.value == right.value;
    }

    friend constexpr bool operator!=(
        ControlIdentity left, ControlIdentity right) noexcept {
        return !(left == right);
    }
};

struct EventSequenceTag;
struct ControllerTickIdTag;
struct VisionFrameIdTag;
struct TargetGenerationTag;

using EventSequence = ControlIdentity<EventSequenceTag>;
using ControllerTickId = ControlIdentity<ControllerTickIdTag>;
using VisionFrameId = ControlIdentity<VisionFrameIdTag>;
using TargetGeneration = ControlIdentity<TargetGenerationTag>;

enum class AdsReacquireDecisionCode : std::uint8_t {
    DeferredWaitingForFreshVision = 0,
    RearmedOutsideBodylockEnvelope = 1,
    RearmedWaitingForTarget = 2,
    KeptBodylockWithinEnvelope = 3,
    CoveredByActiveAdsSnap = 4,
    CancelledByTargetGenerationChange = 5,
    CancelledByAdsRelease = 6,
    ExpiredWithoutEligibleObservation = 7,
    InitialScopeAcquired = 8,
};

}  // namespace pipeline_contract
