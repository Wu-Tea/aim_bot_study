#pragma once

#include "vision_native/types.h"

#include <cstdint>
#include <memory>

namespace vision_native {

struct DxgiCaptureMetadata {
    bool updated = false;
    FramePacket frame;
    uint64_t capture_acquire_begin_ns = 0;
    uint64_t capture_acquire_complete_ns = 0;
    uint64_t capture_copy_complete_ns = 0;
    uint64_t source_present_qpc = 0;
    uint64_t source_present_qpc_frequency = 0;
    bool source_present_available = false;
    uint32_t accumulated_frames = 0;
    int roi_left = 0;
    int roi_top = 0;
    int output_width = 0;
    int output_height = 0;
    int adapter_index = 0;
    int output_index = 0;
    float acquire_ms = 0.0f;
    float copy_ms = 0.0f;
};

class DxgiRoiCapture {
public:
    DxgiRoiCapture(
        int width,
        int height,
        int adapter_index = 0,
        int output_index = -1,
        int timeout_ms = 0);
    ~DxgiRoiCapture();

    DxgiRoiCapture(const DxgiRoiCapture&) = delete;
    DxgiRoiCapture& operator=(const DxgiRoiCapture&) = delete;

    DxgiCaptureMetadata grab();

    int width() const;
    int height() const;
    int output_width() const;
    int output_height() const;
    int roi_left() const;
    int roi_top() const;
    void* d3d11_device() const;
    void* texture() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vision_native
