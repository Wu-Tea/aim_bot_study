#pragma once

#include "vision_native/aim_enhancement.h"
#include "vision_native/dxgi_capture.h"
#include "vision_native/target_selector.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/types.h"

#include <atomic>
#include <cstdint>
#include <vector>

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
    void set_external_cue(bool found, float cue_x = 0.0f, float cue_y = 0.0f, float cue_score = 0.0f);
    void reset();
    VisionResult poll_once();

    int width() const;
    int height() const;

private:
    DxgiRoiCapture capture_;
    VisionTargetSelector selector_;
    AimEnhancementPipeline enhancer_;
    TensorRTEngine engine_;
    std::atomic<bool> aiming_{false};
    bool external_cue_found_ = false;
    float external_cue_x_ = 0.0f;
    float external_cue_y_ = 0.0f;
    float external_cue_score_ = 0.0f;
    void* graphics_resource_ = nullptr;
    std::vector<uint8_t> host_color_frame_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace vision_native
