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
    float color_bonus = 0.0f;
    bool is_friendly = false;
    bool color_classified = false;
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

struct VisionResult {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    uint64_t inferred_at_ns = 0;
    uint64_t result_at_ns = 0;

    bool has_target = false;
    bool auto_fire = false;

    float dx = 0.0f;
    float dy = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float screen_center_x = 0.0f;
    float screen_center_y = 0.0f;

    bool has_body_box = false;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;

    const char* target_source = "";
    float anchor_confidence = 0.0f;
    float ego_confidence = 0.0f;
    const char* body_state_mode = "drop";
    const char* anchor_source = "none";
    float torso_x1 = 0.0f;
    float torso_y1 = 0.0f;
    float torso_x2 = 0.0f;
    float torso_y2 = 0.0f;
    bool yellow_cue_present = false;
    float yellow_cue_score = 0.0f;
    float yellow_cue_x = 0.0f;
    float yellow_cue_y = 0.0f;
    float yellow_mask_area = 0.0f;
    float yellow_roi_x1 = 0.0f;
    float yellow_roi_y1 = 0.0f;
    float yellow_roi_x2 = 0.0f;
    float yellow_roi_y2 = 0.0f;
    bool refiner_applied = false;
    float refined_target_x = 0.0f;
    float refined_target_y = 0.0f;
    const char* ego_model = "identity";
    const char* engine_mode = "idle";
    bool scan_ran = false;
    float scan_age_ms = 0.0f;
    const char* scan_reason = "";
    float keyframe_age_ms = 0.0f;
    bool prewarm_used = false;
    float debug_search_x1 = 0.0f;
    float debug_search_y1 = 0.0f;
    float debug_search_x2 = 0.0f;
    float debug_search_y2 = 0.0f;
    float debug_predicted_x = 0.0f;
    float debug_predicted_y = 0.0f;
    float debug_patch_x = 0.0f;
    float debug_patch_y = 0.0f;
    bool debug_patch_valid = false;
    float debug_template_w = 0.0f;
    float debug_template_h = 0.0f;
    std::vector<float> debug_track_points;
    std::vector<float> debug_scan_boxes;

    float acquire_ms = 0.0f;
    float copy_ms = 0.0f;
    float capture_ms = 0.0f;
    float wait_ms = 0.0f;
    float preprocess_ms = 0.0f;
    float infer_ms = 0.0f;
    float decode_ms = 0.0f;
    float post_ms = 0.0f;
    float age_ms = 0.0f;
    float boxes_seen = 0.0f;
};

} // namespace vision_native
