#pragma once

#include <cstdint>
#include <vector>

namespace vision_native {

enum class PixelFormat {
    RGB8,
    BGRA8,
};

enum class MemoryKind {
    CpuHwc,
    D3D11Texture,
};

struct FramePacket {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB8;
    MemoryKind memory_kind = MemoryKind::CpuHwc;
    int row_pitch = 0;
    void* data = nullptr;
};

struct Detection {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float conf = 0.0f;
    int class_id = 0;
};

struct DetectionBatch {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    uint64_t inferred_at_ns = 0;
    int frame_width = 0;
    int frame_height = 0;
    std::vector<Detection> detections;
    float preprocess_ms = 0.0f;
    float infer_ms = 0.0f;
    float decode_ms = 0.0f;
};

} // namespace vision_native
