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

// R is the current source-supported region in which a desired impact point is
// considered anatomically valid and hittable. It is deliberately distinct
// from the detector body box: the detector box is evidence, while R is the
// smaller control contract published by the Vision target owner.
enum class AimRegionSource : unsigned char {
    None,
    VisionGeometry,
    BodyBoxFallback,
    CueTranslated,
};

struct VisionCandidate {
    std::uint64_t source_id = 0;
    // Source-selected anatomical point. The coordinator may preserve a
    // user-corrected D inside aim_region_px instead of blindly replacing it.
    Vec2f aim_px{};
    common_native::Box2f aim_region_px{};
    AimRegionSource aim_region_source = AimRegionSource::None;
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
    bool has_aim_point = false;
    bool has_aim_region = false;
    bool has_body_box = false;
    bool has_motion_anchor = false;
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
    bool selector_identity_protocol = false;
    // Stable identity is optional because older/direct callers do not have a
    // selector-owned generation. Zero means unavailable.
    std::uint64_t selector_target_generation = 0;
    bool selector_target_changed = false;
    bool selector_enemy_cue_current = false;
    bool selector_enemy_identity_confirmed = false;
    bool selector_enemy_cue_checked = false;
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
    std::array<VisionCandidate, kMaxVisionCandidates> candidates{};
};

}  // namespace pipeline_contract
