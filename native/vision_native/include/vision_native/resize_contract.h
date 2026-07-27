#pragma once

namespace vision_native {

struct VisionResizeContract {
    int capture_width = 0;
    int capture_height = 0;
    int tensor_width = 0;
    int tensor_height = 0;
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    bool isotropic = false;
};

VisionResizeContract validate_resize_contract(
    int capture_width,
    int capture_height,
    int tensor_width,
    int tensor_height,
    int expected_tensor_width = 0,
    int expected_tensor_height = 0,
    bool require_isotropic_resize = true);

}  // namespace vision_native
