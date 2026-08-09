#pragma once

#include "common_native/screen_geometry.h"

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
    common_native::Box2f body_box_px{};
    Vec2f motion_anchor_px{};
    float motion_anchor_score = 0.0f;
    float confidence = 0.0f;
    float cue_confidence = 0.0f;
    float normalized_size = 0.0f;
    float reliability = 0.0f;
    bool body_cue = false;
    bool head_cue = false;
    bool has_body_box = false;
    bool has_motion_anchor = false;
};

struct VisionObservationBatch {
    std::uint64_t frame_id = 0;
    std::uint64_t preferred_source_id = 0;
    double source_time_seconds = 0.0;
    double publish_time_seconds = 0.0;
    // W5-only source-present endpoint. source_time_seconds remains the
    // legacy copy-complete/controller clock and is not substituted here.
    std::uint64_t actuator_effect_present_qpc = 0;
    std::uint64_t actuator_effect_present_qpc_frequency = 0;
    std::uint64_t actuator_effect_present_steady_ns = 0;
    std::uint64_t actuator_effect_present_calibration_id = 0;
    std::uint64_t actuator_effect_present_calibration_uncertainty_ns = 0;
    double actuator_effect_present_time_seconds = 0.0;
    bool actuator_effect_present_raw_available = false;
    bool actuator_effect_present_steady_available = false;
    bool actuator_effect_present_time_valid = false;
    float frame_width_px = 0.0f;
    float frame_height_px = 0.0f;
    std::uint32_t count = 0;
    bool capture_fresh = false;
    bool selector_identity_protocol = false;
    // Stable identity is optional because older/direct callers do not have a
    // selector-owned generation. Zero means unavailable.
    std::uint64_t selector_target_generation = 0;
    bool selector_target_changed = false;
    // A selector-owned cue may update the geometry of an already owned target
    // without fabricating a detector observation id. The coordinator must
    // validate the existing identity/generation before consuming it.
    bool selector_cue_continuation = false;
    bool roi_fallback = false;
    bool fire_requested = false;
    bool observed_fire_eligible = false;
    // Adapter-side diagnostics for candidates filtered before the fixed
    // candidate array reaches the coordinator.  These are counts only; the
    // control path never depends on them.
    std::uint32_t rejected_friendly_count = 0;
    std::uint32_t rejected_low_reliability_count = 0;
    bool has_control_response_hint = false;
    float control_response_x_px_per_second = 0.0f;
    std::array<VisionCandidate, kMaxVisionCandidates> candidates{};
};

}  // namespace pipeline_contract
