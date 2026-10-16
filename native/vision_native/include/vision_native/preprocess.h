#pragma once

#include <cstdint>
#include <cuda_runtime_api.h>

using cudaStream_t = struct CUstream_st*;

namespace vision_native {

void launch_rgb_hwc_to_chw_float(
    const uint8_t* src_rgb,
    int src_width,
    int src_height,
    int row_pitch,
    int dst_width,
    int dst_height,
    float* dst_chw,
    cudaStream_t stream);

void launch_bgra_hwc_to_chw_float(
    const uint8_t* src_bgra,
    int src_width,
    int src_height,
    int row_pitch,
    int dst_width,
    int dst_height,
    float* dst_chw,
    cudaStream_t stream);

// Point-sampled uchar4 texture; caller owns its lifetime through GPU completion.
// Manual interpolation intentionally matches the existing linear-buffer kernel.
void launch_bgra_texture_to_chw_float(cudaTextureObject_t source,
    int roi_left, int roi_top, int src_width, int src_height,
    int dst_width, int dst_height, float* dst_chw, cudaStream_t stream);

// Copies only a bounded low-resolution grayscale staging image from a mapped
// BGRA CUDA array. The caller never performs a full-resolution host readback.
cudaTextureObject_t launch_bgra_array_to_gray_u8(
    cudaArray_t source,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height,
    std::uint8_t* dst_gray,
    cudaStream_t stream);

} // namespace vision_native
