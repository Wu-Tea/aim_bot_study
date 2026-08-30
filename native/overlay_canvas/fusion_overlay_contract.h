#pragma once

#include <cstdint>

namespace fusion_overlay {

inline constexpr std::uint32_t kExcludeFromCaptureAffinity = 0x00000011u;

enum class CaptureIsolationFailure {
    None,
    DwmQueryFailed,
    DwmCompositionDisabled,
    SetAffinityFailed,
    ReadbackFailed,
    AffinityMismatch,
};

struct CaptureIsolationObservation {
    bool dwm_query_succeeded = false;
    bool dwm_composition_enabled = false;
    bool set_affinity_succeeded = false;
    bool readback_succeeded = false;
    std::uint32_t affinity = 0;
};

struct CaptureIsolationDecision {
    bool may_show = false;
    CaptureIsolationFailure failure = CaptureIsolationFailure::DwmQueryFailed;
};

CaptureIsolationDecision decide_capture_isolation(
    const CaptureIsolationObservation& observation) noexcept;

const char* capture_isolation_failure_name(
    CaptureIsolationFailure failure) noexcept;

bool fusion_sample_is_fresh(
    std::uint64_t now_qpc,
    std::uint64_t published_qpc,
    std::uint64_t qpc_frequency,
    std::uint64_t timeout_ms) noexcept;

enum class MarkerContinuitySource {
    None,
    LatestDirect,
    LatchedDirectPendingRender,
    LatchedConfirmedContinuation,
};

struct MarkerContinuityInput {
    bool latest_sample_fresh = false;
    bool latest_has_target = false;
    bool latest_has_body_box = false;
    bool latest_direct_observation = false;
    bool latest_enemy_identity_confirmed = false;
    std::uint64_t latest_target_generation = 0;
    bool latched_direct_available = false;
    bool latched_direct_pending_render = false;
    std::uint64_t latched_target_generation = 0;
    std::uint64_t latched_published_qpc = 0;
    std::uint64_t now_qpc = 0;
    std::uint64_t qpc_frequency = 0;
    std::uint64_t hold_ms = 0;
};

MarkerContinuitySource decide_marker_continuity(
    const MarkerContinuityInput& input) noexcept;

struct MarkerLayoutInput {
    bool has_target = false;
    bool direct_observation = false;
    int frame_width = 0;
    int frame_height = 0;
    int output_left = 0;
    int output_top = 0;
    int output_width = 0;
    int output_height = 0;
    int roi_left = 0;
    int roi_top = 0;
    int virtual_left = 0;
    int virtual_top = 0;
    int virtual_width = 0;
    int virtual_height = 0;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float marker_radius_px = 6.0f;
};

struct MarkerLayout {
    bool visible = false;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float radius = 0.0f;
};

MarkerLayout layout_target_point_marker(const MarkerLayoutInput& input) noexcept;

}  // namespace fusion_overlay
