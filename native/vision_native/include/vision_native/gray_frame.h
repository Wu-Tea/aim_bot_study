#pragma once

#include <cstdint>
#include <vector>

namespace vision_native {

struct GrayFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    bool empty() const {
        return pixels.empty() || width <= 0 || height <= 0;
    }

    uint8_t at(int x, int y) const {
        return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    }
};

} // namespace vision_native
