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

}  // namespace common_native
