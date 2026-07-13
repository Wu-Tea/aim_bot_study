#pragma once

#include "pipeline_contract/target_snapshot.h"

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

enum class PreprocessMode {
    Unknown,
    RgbHostCopy,
    OldBgraCopy,
};

inline const char* preprocess_mode_name(PreprocessMode mode) {
    switch (mode) {
    case PreprocessMode::RgbHostCopy:
        return "rgb_host_copy";
    case PreprocessMode::OldBgraCopy:
        return "old_bgra_copy";
    case PreprocessMode::Unknown:
    default:
        return "none";
    }
}

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
    bool has_cue_point = false;
    float cue_x = 0.0f;
    float cue_y = 0.0f;
    float cue_score = 0.0f;
};

struct DetectionBatch {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    uint64_t inferred_at_ns = 0;
    int frame_width = 0;
    int frame_height = 0;
    bool has_external_cue = false;
    float external_cue_x = 0.0f;
    float external_cue_y = 0.0f;
    float external_cue_score = 0.0f;
    std::vector<Detection> detections;
    float preprocess_ms = 0.0f;
    float infer_ms = 0.0f;
    float output_copy_sync_ms = 0.0f;
    float gpu_total_ms = 0.0f;
    float output_copy_ms = 0.0f;
    float output_wait_ms = 0.0f;
    float decode_ms = 0.0f;
    PreprocessMode preprocess_mode = PreprocessMode::Unknown;
};

struct VisionResult {
    uint64_t frame_id = 0;
    uint64_t captured_at_ns = 0;
    uint64_t inferred_at_ns = 0;
    uint64_t result_at_ns = 0;
    bool frame_updated = false;
    const char* service_freshness = "none";
    const char* service_source_state = "unknown";
    uint64_t service_sequence = 0;
    bool service_controller_aiming = false;
    bool service_engine_aiming = false;
    float aim_wakeup_to_dispatch_ms = 0.0f;
    float aim_wakeup_to_capture_ms = 0.0f;
    float aim_wakeup_to_result_ms = 0.0f;
    float requested_vision_fps = 0.0f;

    bool has_target = false;
    bool auto_fire = false;
    bool has_selected_detection = false;
    std::uint32_t selected_detection_index = 0;

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
    const char* target_tier = "none";
    bool aim_authority = false;
    bool fire_authority = false;
    const char* association_stage = "";
    float target_confidence = 0.0f;
    uint64_t intent_id = 0;
    bool intent_applied = false;
    const char* intent_decision = "none";
    float intent_score = 0.0f;
    pipeline_contract::UserAimIntent user_aim_intent;
    bool has_external_cue = false;
    float external_cue_x = 0.0f;
    float external_cue_y = 0.0f;
    float external_cue_score = 0.0f;

    float wait_ms = 0.0f;
    float capture_acquire_ms = 0.0f;
    float capture_copy_ms = 0.0f;
    float cuda_map_ms = 0.0f;
    float preprocess_ms = 0.0f;
    float color_copy_ms = 0.0f;
    bool color_copy_required = false;
    std::uint64_t color_copy_bytes = 0;
    float color_copy_region_ratio = 0.0f;
    const char* color_readback_mode = "none";
    float color_classify_ms = 0.0f;
    std::uint32_t color_candidate_count = 0;
    float infer_ms = 0.0f;
    float output_copy_sync_ms = 0.0f;
    float gpu_total_ms = 0.0f;
    float output_copy_ms = 0.0f;
    float output_wait_ms = 0.0f;
    float decode_ms = 0.0f;
    float selector_ms = 0.0f;
    float enhance_ms = 0.0f;
    float cuda_unmap_ms = 0.0f;
    float post_ms = 0.0f;
    float age_ms = 0.0f;
    float boxes_seen = 0.0f;
    PreprocessMode preprocess_mode = PreprocessMode::Unknown;
    std::vector<Detection> detections;
};

} // namespace vision_native
