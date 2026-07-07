#pragma once

#include <cstdint>

namespace common_native {

enum class AssistAuthority : std::uint8_t {
    None = 0,
    AimObserved = 1,
    AimCoast = 2,
};

enum class FireAuthority : std::uint8_t {
    None = 0,
    ObservedOnly = 1,
};

enum class TargetAuthorityState : std::uint8_t {
    Reject = 0,
    StrongAssist = 1,
    WeakAssist = 2,
    TrackOnly = 3,
    Yield = 4,
};

}  // namespace common_native
