#pragma once

#include "vision_native/gray_frame.h"
#include "vision_native/types.h"

#include <algorithm>
#include <cstdint>

namespace vision_native::detail {

struct ImageFrameView {
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int row_pitch = 0;
    PixelFormat format = PixelFormat::RGB8;
};

inline GrayFrame to_grayscale(const ImageFrameView& frame, int downsample = 1) {
    GrayFrame gray;
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 || frame.row_pitch <= 0) {
        return gray;
    }

    const int step = std::max(1, downsample);
    gray.width = std::max(1, frame.width / step);
    gray.height = std::max(1, frame.height / step);
    gray.pixels.resize(static_cast<size_t>(gray.width) * static_cast<size_t>(gray.height));

    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            const int src_x = std::min(frame.width - 1, x * step);
            const int src_y = std::min(frame.height - 1, y * step);
            const uint8_t* row = frame.data + (static_cast<size_t>(src_y) * static_cast<size_t>(frame.row_pitch));
            int r = 0;
            int g = 0;
            int b = 0;
            if (frame.format == PixelFormat::BGRA8) {
                const uint8_t* pixel = row + (static_cast<size_t>(src_x) * 4);
                b = pixel[0];
                g = pixel[1];
                r = pixel[2];
            } else {
                const uint8_t* pixel = row + (static_cast<size_t>(src_x) * 3);
                r = pixel[0];
                g = pixel[1];
                b = pixel[2];
            }

            gray.pixels[static_cast<size_t>(y) * static_cast<size_t>(gray.width) + static_cast<size_t>(x)] =
                static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        }
    }

    return gray;
}

inline GrayFrame downsample_gray(const GrayFrame& source, int downsample = 1) {
    GrayFrame gray;
    if (source.empty()) {
        return gray;
    }

    const int step = std::max(1, downsample);
    gray.width = std::max(1, source.width / step);
    gray.height = std::max(1, source.height / step);
    gray.pixels.resize(static_cast<size_t>(gray.width) * static_cast<size_t>(gray.height));

    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            const int src_x = std::min(source.width - 1, x * step);
            const int src_y = std::min(source.height - 1, y * step);
            gray.pixels[static_cast<size_t>(y) * static_cast<size_t>(gray.width) + static_cast<size_t>(x)] =
                source.at(src_x, src_y);
        }
    }

    return gray;
}

} // namespace vision_native::detail
