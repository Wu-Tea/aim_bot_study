#include "vision_native/preprocess.h"

#include <cuda_runtime.h>

#include <cstdint>

namespace vision_native {
namespace {

__device__ float clamp_float(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

__device__ float sample_hwc_channel_bilinear(
    const uint8_t* src,
    int src_width,
    int src_height,
    int row_pitch,
    int channels,
    int channel,
    float src_x,
    float src_y) {
    src_x = clamp_float(src_x, 0.0f, static_cast<float>(src_width - 1));
    src_y = clamp_float(src_y, 0.0f, static_cast<float>(src_height - 1));

    const int x0 = static_cast<int>(floorf(src_x));
    const int y0 = static_cast<int>(floorf(src_y));
    const int x1 = x0 + 1 < src_width ? x0 + 1 : x0;
    const int y1 = y0 + 1 < src_height ? y0 + 1 : y0;
    const float tx = src_x - static_cast<float>(x0);
    const float ty = src_y - static_cast<float>(y0);

    const uint8_t* p00 = src + (y0 * row_pitch) + (x0 * channels) + channel;
    const uint8_t* p10 = src + (y0 * row_pitch) + (x1 * channels) + channel;
    const uint8_t* p01 = src + (y1 * row_pitch) + (x0 * channels) + channel;
    const uint8_t* p11 = src + (y1 * row_pitch) + (x1 * channels) + channel;

    const float top = (static_cast<float>(*p00) * (1.0f - tx)) + (static_cast<float>(*p10) * tx);
    const float bottom = (static_cast<float>(*p01) * (1.0f - tx)) + (static_cast<float>(*p11) * tx);
    return ((top * (1.0f - ty)) + (bottom * ty)) / 255.0f;
}

__global__ void rgb_hwc_to_chw_float_direct_kernel(
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

__global__ void bgra_hwc_to_chw_float_direct_kernel(
    const uint8_t* src_bgra,
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
    const uint8_t* src = src_bgra + (y * row_pitch) + (x * 4);
    const int plane_size = width * height;
    dst_chw[pixel_index] = static_cast<float>(src[2]) / 255.0f;
    dst_chw[plane_size + pixel_index] = static_cast<float>(src[1]) / 255.0f;
    dst_chw[(plane_size * 2) + pixel_index] = static_cast<float>(src[0]) / 255.0f;
}

__global__ void rgb_hwc_to_chw_float_kernel(
    const uint8_t* src_rgb,
    int src_width,
    int src_height,
    int row_pitch,
    int dst_width,
    int dst_height,
    float* dst_chw) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dst_width || y >= dst_height) {
        return;
    }

    const float scale_x = static_cast<float>(src_width) / static_cast<float>(dst_width);
    const float scale_y = static_cast<float>(src_height) / static_cast<float>(dst_height);
    const float src_x = ((static_cast<float>(x) + 0.5f) * scale_x) - 0.5f;
    const float src_y = ((static_cast<float>(y) + 0.5f) * scale_y) - 0.5f;
    const int pixel_index = y * dst_width + x;
    const int plane_size = dst_width * dst_height;
    dst_chw[pixel_index] = sample_hwc_channel_bilinear(src_rgb, src_width, src_height, row_pitch, 3, 0, src_x, src_y);
    dst_chw[plane_size + pixel_index] = sample_hwc_channel_bilinear(src_rgb, src_width, src_height, row_pitch, 3, 1, src_x, src_y);
    dst_chw[(plane_size * 2) + pixel_index] = sample_hwc_channel_bilinear(src_rgb, src_width, src_height, row_pitch, 3, 2, src_x, src_y);
}

__global__ void bgra_hwc_to_chw_float_kernel(
    const uint8_t* src_bgra,
    int src_width,
    int src_height,
    int row_pitch,
    int dst_width,
    int dst_height,
    float* dst_chw) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dst_width || y >= dst_height) {
        return;
    }

    const float scale_x = static_cast<float>(src_width) / static_cast<float>(dst_width);
    const float scale_y = static_cast<float>(src_height) / static_cast<float>(dst_height);
    const float src_x = ((static_cast<float>(x) + 0.5f) * scale_x) - 0.5f;
    const float src_y = ((static_cast<float>(y) + 0.5f) * scale_y) - 0.5f;
    const int pixel_index = y * dst_width + x;
    const int plane_size = dst_width * dst_height;
    dst_chw[pixel_index] = sample_hwc_channel_bilinear(src_bgra, src_width, src_height, row_pitch, 4, 2, src_x, src_y);
    dst_chw[plane_size + pixel_index] = sample_hwc_channel_bilinear(src_bgra, src_width, src_height, row_pitch, 4, 1, src_x, src_y);
    dst_chw[(plane_size * 2) + pixel_index] = sample_hwc_channel_bilinear(src_bgra, src_width, src_height, row_pitch, 4, 0, src_x, src_y);
}

__global__ void bgra_array_to_gray_u8_kernel(
    cudaTextureObject_t source_texture,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height,
    std::uint8_t* dst_gray) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dst_width || y >= dst_height) return;
    const float scale_x = static_cast<float>(src_width) /
        static_cast<float>(dst_width);
    const float scale_y = static_cast<float>(src_height) /
        static_cast<float>(dst_height);
    const int src_x = min(src_width - 1,
        max(0, static_cast<int>((static_cast<float>(x) + 0.5f) * scale_x)));
    const int src_y = min(src_height - 1,
        max(0, static_cast<int>((static_cast<float>(y) + 0.5f) * scale_y)));
    const uchar4 bgra = tex2D<uchar4>(
        source_texture, static_cast<float>(src_x) + 0.5f,
        static_cast<float>(src_y) + 0.5f);
    const float gray = (0.114f * static_cast<float>(bgra.x)) +
        (0.587f * static_cast<float>(bgra.y)) +
        (0.299f * static_cast<float>(bgra.z));
    dst_gray[(y * dst_width) + x] = static_cast<std::uint8_t>(
        min(255.0f, max(0.0f, gray)));
}

} // namespace

void launch_rgb_hwc_to_chw_float(
    const uint8_t* src_rgb,
    int src_width,
    int src_height,
    int row_pitch,
    int dst_width,
    int dst_height,
    float* dst_chw,
    cudaStream_t stream) {
    const dim3 block(16, 16);
    if (src_width == dst_width && src_height == dst_height) {
        const dim3 grid((dst_width + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);
        rgb_hwc_to_chw_float_direct_kernel<<<grid, block, 0, stream>>>(
            src_rgb,
            src_width,
            src_height,
            row_pitch,
            dst_chw);
        return;
    }
    const dim3 grid((dst_width + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);
    rgb_hwc_to_chw_float_kernel<<<grid, block, 0, stream>>>(
        src_rgb,
        src_width,
        src_height,
        row_pitch,
        dst_width,
        dst_height,
        dst_chw);
}

void launch_bgra_hwc_to_chw_float(
    const uint8_t* src_bgra,
    int src_width,
    int src_height,
    int row_pitch,
    int dst_width,
    int dst_height,
    float* dst_chw,
    cudaStream_t stream) {
    const dim3 block(16, 16);
    if (src_width == dst_width && src_height == dst_height) {
        const dim3 grid((dst_width + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);
        bgra_hwc_to_chw_float_direct_kernel<<<grid, block, 0, stream>>>(
            src_bgra,
            src_width,
            src_height,
            row_pitch,
            dst_chw);
        return;
    }
    const dim3 grid((dst_width + block.x - 1) / block.x, (dst_height + block.y - 1) / block.y);
    bgra_hwc_to_chw_float_kernel<<<grid, block, 0, stream>>>(
        src_bgra,
        src_width,
        src_height,
        row_pitch,
        dst_width,
        dst_height,
        dst_chw);
}

cudaTextureObject_t launch_bgra_array_to_gray_u8(
    cudaArray_t source,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height,
    std::uint8_t* dst_gray,
    cudaStream_t stream) {
    if (source == nullptr || dst_gray == nullptr || src_width <= 0 ||
        src_height <= 0 || dst_width <= 0 || dst_height <= 0) {
        return 0;
    }
    cudaResourceDesc resource{};
    resource.resType = cudaResourceTypeArray;
    resource.res.array.array = source;
    cudaTextureDesc texture{};
    texture.addressMode[0] = cudaAddressModeClamp;
    texture.addressMode[1] = cudaAddressModeClamp;
    texture.filterMode = cudaFilterModePoint;
    texture.readMode = cudaReadModeElementType;
    texture.normalizedCoords = 0;
    cudaTextureObject_t texture_object = 0;
    if (cudaCreateTextureObject(&texture_object, &resource, &texture, nullptr) !=
        cudaSuccess) {
        return 0;
    }
    const dim3 block(16, 16);
    const dim3 grid(
        (dst_width + block.x - 1) / block.x,
        (dst_height + block.y - 1) / block.y);
    bgra_array_to_gray_u8_kernel<<<grid, block, 0, stream>>>(
        texture_object, src_width, src_height, dst_width, dst_height, dst_gray);
    return texture_object;
}

} // namespace vision_native
