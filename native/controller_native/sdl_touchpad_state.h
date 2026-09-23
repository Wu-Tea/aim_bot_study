#pragma once

#include "xinput_reader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace controller_native {

using SdlTouchpadFingerReader = int (*)(void*, int, int, std::uint8_t*, float*, float*, float*);

// Clear each sample before querying SDL, including release and failed reads.
inline std::array<TouchpadFingerState, 2> read_sdl_touchpad_fingers(
    void* controller, int finger_count, SdlTouchpadFingerReader read) noexcept {
    std::array<TouchpadFingerState, 2> result{};
    if (!controller || !read) return result;
    for (int finger = 0; finger < std::clamp(finger_count, 0, 2); ++finger) {
        std::uint8_t active = 0;
        float x = 0, y = 0, pressure = 0;
        if (read(controller, 0, finger, &active, &x, &y, &pressure) == 0 &&
            active && std::isfinite(x) && std::isfinite(y) &&
            x >= 0 && x <= 1 && y >= 0 && y <= 1) {
            result[static_cast<std::size_t>(finger)] = {true, x, y};
        }
    }
    return result;
}

}  // namespace controller_native
