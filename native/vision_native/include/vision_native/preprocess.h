#pragma once

#include <cstdint>
#include <cuda_runtime_api.h>

using cudaStream_t = struct CUstream_st*;

namespace vision_native {

void launch_rgb_hwc_to_chw_float(
    const uint8_t* src_rgb,
    int width,
    int height,
    int row_pitch,
    float* dst_chw,
    cudaStream_t stream);

void launch_bgra_hwc_to_chw_float(
    const uint8_t* src_bgra,
    int width,
    int height,
    int row_pitch,
    float* dst_chw,
    cudaStream_t stream);

} // namespace vision_native
