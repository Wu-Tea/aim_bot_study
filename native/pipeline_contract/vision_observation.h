#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pipeline_contract {

inline constexpr std::size_t kMaxVisionCandidates = 32;

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct VisionCandidate {
    std::uint64_t source_id = 0;
    Vec2f aim_px{};
    Vec2f box_size_px{};
    float confidence = 0.0f;
    float cue_confidence = 0.0f;
    float normalized_size = 0.0f;
    float reliability = 0.0f;
    bool body_cue = false;
    bool head_cue = false;
};

struct VisionObservationBatch {
    std::uint64_t frame_id = 0;
    std::uint64_t preferred_source_id = 0;
    double source_time_seconds = 0.0;
    double publish_time_seconds = 0.0;
    float frame_width_px = 0.0f;
    float frame_height_px = 0.0f;
    std::uint32_t count = 0;
    bool capture_fresh = false;
    bool roi_fallback = false;
    bool fire_requested = false;
    bool observed_fire_eligible = false;
    bool has_control_response_hint = false;
    float control_response_x_px_per_second = 0.0f;
    std::array<VisionCandidate, kMaxVisionCandidates> candidates{};
};

}  // namespace pipeline_contract
