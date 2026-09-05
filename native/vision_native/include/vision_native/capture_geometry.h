#pragma once

#include <cstdint>
#include <stdexcept>

namespace vision_native {

struct CaptureGeometry {
    int output_left = 0;
    int output_top = 0;
    int output_width = 0;
    int output_height = 0;
    int roi_left = 0;
    int roi_top = 0;
};

inline CaptureGeometry centered_capture_geometry(
    int width, int height, int left, int top, int output_width, int output_height) {
    if (width <= 0 || height <= 0 || output_width < width || output_height < height) {
        throw std::runtime_error("requested ROI is larger than the selected output");
    }
    return {left, top, output_width, output_height,
            (output_width - width) / 2, (output_height - height) / 2};
}

// AcquireNextFrame also succeeds for hardware-pointer-only updates. These
// do not provide a new image, frame identity, inference or control observation.
inline bool dxgi_has_new_desktop_image(
    std::int64_t last_present_qpc, std::uint32_t accumulated_frames) noexcept {
    return last_present_qpc > 0 && accumulated_frames > 0;
}

}  // namespace vision_native
