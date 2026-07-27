#include "vision_native/resize_contract.h"

#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace vision_native {

VisionResizeContract validate_resize_contract(
    int capture_width,
    int capture_height,
    int tensor_width,
    int tensor_height,
    int expected_tensor_width,
    int expected_tensor_height,
    bool require_isotropic_resize) {
    if (capture_width <= 0 || capture_height <= 0) {
        throw std::invalid_argument("vision capture dimensions must be positive");
    }
    if (tensor_width <= 0 || tensor_height <= 0) {
        throw std::invalid_argument("TensorRT input dimensions must be positive");
    }
    if ((expected_tensor_width == 0) != (expected_tensor_height == 0)) {
        throw std::invalid_argument(
            "expected Tensor dimensions must both be zero (auto) or both be positive");
    }
    if (expected_tensor_width < 0 || expected_tensor_height < 0) {
        throw std::invalid_argument("expected Tensor dimensions must not be negative");
    }
    if (expected_tensor_width > 0 &&
        (tensor_width != expected_tensor_width || tensor_height != expected_tensor_height)) {
        std::ostringstream message;
        message << "configured Tensor size " << expected_tensor_width << 'x'
                << expected_tensor_height << " does not match engine input "
                << tensor_width << 'x' << tensor_height;
        throw std::runtime_error(message.str());
    }

    const bool isotropic =
        static_cast<std::int64_t>(capture_width) * static_cast<std::int64_t>(tensor_height) ==
        static_cast<std::int64_t>(capture_height) * static_cast<std::int64_t>(tensor_width);
    if (require_isotropic_resize && !isotropic) {
        std::ostringstream message;
        message << "anisotropic vision resize rejected: capture "
                << capture_width << 'x' << capture_height << " -> Tensor "
                << tensor_width << 'x' << tensor_height
                << "; use matching aspect ratios or explicitly set "
                   "require_isotropic_resize=false";
        throw std::runtime_error(message.str());
    }

    VisionResizeContract contract;
    contract.capture_width = capture_width;
    contract.capture_height = capture_height;
    contract.tensor_width = tensor_width;
    contract.tensor_height = tensor_height;
    contract.scale_x =
        static_cast<float>(capture_width) / static_cast<float>(tensor_width);
    contract.scale_y =
        static_cast<float>(capture_height) / static_cast<float>(tensor_height);
    contract.isotropic = isotropic;
    return contract;
}

}  // namespace vision_native
