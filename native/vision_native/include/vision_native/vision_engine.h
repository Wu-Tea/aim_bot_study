#pragma once

#include "vision_native/dxgi_capture.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/types.h"

#include <atomic>

namespace vision_native {

class VisionEngine {
public:
    VisionEngine(
        int width,
        int height,
        int adapter_index = 0,
        int output_index = -1,
        int timeout_ms = 0);
    ~VisionEngine();

    VisionEngine(const VisionEngine&) = delete;
    VisionEngine& operator=(const VisionEngine&) = delete;

    void set_aiming(bool aiming);
    void reset();
    VisionResult poll_once();

    int width() const;
    int height() const;

private:
    DxgiRoiCapture capture_;
    TensorRTEngine engine_;
    std::atomic<bool> aiming_{false};
    void* graphics_resource_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace vision_native
