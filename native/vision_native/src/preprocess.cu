#include "vision_native/preprocess.h"

#include <cuda_runtime.h>

namespace vision_native {
namespace {

__global__ void rgb_hwc_to_chw_float_kernel(
    const uint8_t* src_rgb,
    int width,
    int height,
    int row_pitch,
    float* dst_chw) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) {
        return;
    }

    const int pixel_index = y * width + x;
    const uint8_t* src = src_rgb + (y * row_pitch) + (x * 3);
    const int plane_size = width * height;
    dst_chw[pixel_index] = static_cast<float>(src[0]) / 255.0f;
    dst_chw[plane_size + pixel_index] = static_cast<float>(src[1]) / 255.0f;
    dst_chw[(plane_size * 2) + pixel_index] = static_cast<float>(src[2]) / 255.0f;
}

} // namespace

void launch_rgb_hwc_to_chw_float(
    const uint8_t* src_rgb,
    int width,
    int height,
    int row_pitch,
    float* dst_chw,
    cudaStream_t stream) {
    const dim3 block(16, 16);
    const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    rgb_hwc_to_chw_float_kernel<<<grid, block, 0, stream>>>(src_rgb, width, height, row_pitch, dst_chw);
}

} // namespace vision_native
