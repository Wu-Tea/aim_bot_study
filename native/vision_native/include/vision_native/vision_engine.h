#pragma once

#include "vision_native/dxgi_capture.h"
#include "vision_native/resize_contract.h"
#include "vision_native/target_selector.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/types.h"
#include "pipeline_contract/target_snapshot.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vision_native {

class ColorReadbackBuffer;

class VisionEngine {
public:
    VisionEngine(
        int width,
        int height,
        int adapter_index = 0,
        int output_index = -1,
        int timeout_ms = 0,
        std::string engine_path = {},
        std::string color_readback_mode = "pageable",
        int expected_tensor_width = 0,
        int expected_tensor_height = 0,
        bool require_isotropic_resize = true);
    ~VisionEngine();

    VisionEngine(const VisionEngine&) = delete;
    VisionEngine& operator=(const VisionEngine&) = delete;

    void set_aiming(bool aiming);
    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent);
    void set_viewport(
        int level,
        int width,
        int height,
        std::uint64_t sequence,
        std::uint64_t source_frame_id);
    void set_external_cue(bool found, float cue_x = 0.0f, float cue_y = 0.0f, float cue_score = 0.0f);
    void reset();
    VisionResult poll_once();

    int width() const;
    int height() const;
    int tensor_width() const;
    int tensor_height() const;
    float resize_scale_x() const;
    float resize_scale_y() const;
    bool resize_isotropic() const;

private:
    DxgiRoiCapture capture_;
    VisionTargetSelector selector_;
    std::unique_ptr<TensorRTEngine> engine_;
    std::atomic<bool> aiming_{false};
    std::atomic<int> requested_viewport_level_{1};
    std::atomic<int> requested_viewport_width_{0};
    std::atomic<int> requested_viewport_height_{0};
    std::atomic<std::uint64_t> requested_viewport_sequence_{0};
    std::atomic<std::uint64_t> requested_viewport_source_frame_id_{0};
    std::mutex viewport_mutex_;
    pipeline_contract::UserAimIntent user_aim_intent_;
    bool external_cue_found_ = false;
    float external_cue_x_ = 0.0f;
    float external_cue_y_ = 0.0f;
    float external_cue_score_ = 0.0f;
    void* graphics_resource_ = nullptr;
    std::unique_ptr<ColorReadbackBuffer> host_color_frame_;
    int width_ = 0;
    int height_ = 0;
    int active_viewport_width_ = 0;
    int active_viewport_height_ = 0;
    std::uint64_t active_viewport_sequence_ = 0;
    VisionResizeContract resize_contract_{};
};

} // namespace vision_native
