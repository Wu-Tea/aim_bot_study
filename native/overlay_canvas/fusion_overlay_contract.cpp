#include "fusion_overlay_contract.h"

#include <algorithm>
#include <cmath>

namespace fusion_overlay {

CaptureIsolationLifecycleDecision transition_capture_isolation(
    CaptureIsolationLifecycleState state,
    CaptureIsolationLifecycleEvent event) noexcept {
    if (state == CaptureIsolationLifecycleState::FailedClosed) {
        return {CaptureIsolationLifecycleState::FailedClosed, false, false};
    }

    switch (event) {
    case CaptureIsolationLifecycleEvent::IsolationInvalidated:
        return {
            CaptureIsolationLifecycleState::RevalidationPending,
            false,
            true,
        };
    case CaptureIsolationLifecycleEvent::RevalidationPassed:
        if (state == CaptureIsolationLifecycleState::RevalidationPending) {
            return {CaptureIsolationLifecycleState::Verified, true, false};
        }
        break;
    case CaptureIsolationLifecycleEvent::RevalidationFailed:
    case CaptureIsolationLifecycleEvent::VerificationFailed:
        return {CaptureIsolationLifecycleState::FailedClosed, false, false};
    }
    return {CaptureIsolationLifecycleState::FailedClosed, false, false};
}

CaptureIsolationDecision decide_capture_isolation(
    const CaptureIsolationObservation& observation) noexcept {
    if (!observation.dwm_query_succeeded) {
        return {false, CaptureIsolationFailure::DwmQueryFailed};
    }
    if (!observation.dwm_composition_enabled) {
        return {false, CaptureIsolationFailure::DwmCompositionDisabled};
    }
    if (!observation.set_affinity_succeeded) {
        return {false, CaptureIsolationFailure::SetAffinityFailed};
    }
    if (!observation.readback_succeeded) {
        return {false, CaptureIsolationFailure::ReadbackFailed};
    }
    if (observation.affinity != kExcludeFromCaptureAffinity) {
        return {false, CaptureIsolationFailure::AffinityMismatch};
    }
    return {true, CaptureIsolationFailure::None};
}

const char* capture_isolation_failure_name(
    CaptureIsolationFailure failure) noexcept {
    switch (failure) {
    case CaptureIsolationFailure::None: return "none";
    case CaptureIsolationFailure::DwmQueryFailed: return "dwm_query_failed";
    case CaptureIsolationFailure::DwmCompositionDisabled: return "dwm_composition_disabled";
    case CaptureIsolationFailure::SetAffinityFailed: return "set_affinity_failed";
    case CaptureIsolationFailure::ReadbackFailed: return "readback_failed";
    case CaptureIsolationFailure::AffinityMismatch: return "affinity_mismatch";
    }
    return "unknown";
}

bool fusion_sample_is_fresh(
    std::uint64_t now_qpc,
    std::uint64_t published_qpc,
    std::uint64_t qpc_frequency,
    std::uint64_t timeout_ms) noexcept {
    if (now_qpc == 0 || published_qpc == 0 || qpc_frequency == 0 ||
        timeout_ms == 0 || now_qpc < published_qpc) {
        return false;
    }

    const std::uint64_t whole_ticks = qpc_frequency / 1000u;
    const std::uint64_t remainder = qpc_frequency % 1000u;
    if (whole_ticks > UINT64_MAX / timeout_ms) {
        return true;
    }
    const std::uint64_t whole_timeout_ticks = whole_ticks * timeout_ms;
    if (remainder != 0 && timeout_ms > UINT64_MAX / remainder) {
        return true;
    }
    const std::uint64_t remainder_timeout_ticks =
        (remainder * timeout_ms) / 1000u;
    if (whole_timeout_ticks > UINT64_MAX - remainder_timeout_ticks) {
        return true;
    }
    return now_qpc - published_qpc <=
         whole_timeout_ticks + remainder_timeout_ticks;
}

MarkerContinuitySource decide_marker_continuity(
    const MarkerContinuityInput& input) noexcept {
    if (input.latest_sample_fresh && input.latest_has_target &&
        input.latest_has_body_box && input.latest_direct_observation) {
        return MarkerContinuitySource::LatestDirect;
    }

    if (!input.latched_direct_available ||
        input.latched_target_generation == 0 ||
        input.latest_target_generation != input.latched_target_generation ||
        !fusion_sample_is_fresh(
            input.now_qpc,
            input.latched_published_qpc,
            input.qpc_frequency,
            input.hold_ms)) {
        return MarkerContinuitySource::None;
    }

    if (input.latched_direct_pending_render) {
        return MarkerContinuitySource::LatchedDirectPendingRender;
    }

    if (input.latest_sample_fresh && input.latest_has_target &&
        input.latest_has_body_box && input.latest_enemy_identity_confirmed) {
        return MarkerContinuitySource::LatchedConfirmedContinuation;
    }

    return MarkerContinuitySource::None;
}

MarkerLayout layout_target_point_marker(const MarkerLayoutInput& input) noexcept {
    const bool finite_target =
        std::isfinite(input.target_x) && std::isfinite(input.target_y);
    if (!input.has_target || !input.direct_observation ||
        input.frame_width <= 0 || input.frame_height <= 0 ||
        input.output_width <= 0 || input.output_height <= 0 ||
        input.roi_left < 0 || input.roi_top < 0 ||
        input.frame_width > input.output_width - input.roi_left ||
        input.frame_height > input.output_height - input.roi_top ||
        input.virtual_width <= 0 || input.virtual_height <= 0 ||
        !finite_target || !std::isfinite(input.marker_radius_px) ||
        input.marker_radius_px <= 0.0f ||
        input.target_x < 0.0f || input.target_x > 1.0f ||
        input.target_y < 0.0f || input.target_y > 1.0f) {
        return {};
    }

    const float roi_origin_x = static_cast<float>(
        input.output_left + input.roi_left - input.virtual_left);
    const float roi_origin_y = static_cast<float>(
        input.output_top + input.roi_top - input.virtual_top);
    const float center_x = roi_origin_x +
        (input.target_x * static_cast<float>(input.frame_width));
    const float center_y = roi_origin_y +
        (input.target_y * static_cast<float>(input.frame_height));

    const float radius = input.marker_radius_px;
    if (center_x - radius < 0.0f ||
        center_x + radius > static_cast<float>(input.virtual_width) ||
        center_y - radius < 0.0f ||
        center_y + radius > static_cast<float>(input.virtual_height)) {
        return {};
    }
    return {true, center_x, center_y, radius};
}

CanvasPresentation decide_canvas_presentation(
    const CanvasPresentationInput& input) noexcept {
    if (!input.visibility_enabled || input.virtual_width <= 0 ||
        input.virtual_height <= 0) {
        return {};
    }

    if (input.show_debug_detections) {
        CanvasPresentation output;
        output.mode = CanvasSurfaceMode::DebugFullCanvas;
        output.window_left = input.virtual_left;
        output.window_top = input.virtual_top;
        output.surface_width = input.virtual_width;
        output.surface_height = input.virtual_height;
        output.content_center_x = input.marker_center_x;
        output.content_center_y = input.marker_center_y;
        output.marker_radius = input.marker_radius;
        return output;
    }

    const bool valid_marker =
        input.marker_visible &&
        std::isfinite(input.marker_center_x) &&
        std::isfinite(input.marker_center_y) &&
        std::isfinite(input.marker_radius) &&
        input.marker_radius > 0.0f;
    if (valid_marker) {
        const int required_extent = static_cast<int>(std::ceil(
            (input.marker_radius + 3.0f) * 2.0f));
        const int extent = std::max(
            kTargetMarkerSurfaceExtentPx,
            required_extent);
        const int canvas_left = static_cast<int>(std::lround(
            input.marker_center_x)) - extent / 2;
        const int canvas_top = static_cast<int>(std::lround(
            input.marker_center_y)) - extent / 2;

        CanvasPresentation output;
        output.mode = CanvasSurfaceMode::TargetMarker;
        output.window_left = input.virtual_left + canvas_left;
        output.window_top = input.virtual_top + canvas_top;
        output.surface_width = extent;
        output.surface_height = extent;
        output.content_center_x = static_cast<float>(extent) * 0.5f;
        output.content_center_y = static_cast<float>(extent) * 0.5f;
        output.marker_radius = input.marker_radius;
        return output;
    }

    if (input.idle_crosshair) {
        CanvasPresentation output;
        output.mode = CanvasSurfaceMode::IdleCrosshair;
        output.surface_width = kTargetMarkerSurfaceExtentPx;
        output.surface_height = kTargetMarkerSurfaceExtentPx;
        output.window_left = input.virtual_left +
            (input.virtual_width - output.surface_width) / 2;
        output.window_top = input.virtual_top +
            (input.virtual_height - output.surface_height) / 2;
        output.content_center_x =
            static_cast<float>(output.surface_width) * 0.5f;
        output.content_center_y =
            static_cast<float>(output.surface_height) * 0.5f;
        return output;
    }

    return {};
}

bool canvas_surface_redraw_required(
    const CanvasPresentation& previous,
    const CanvasPresentation& next,
    bool debug_content_dirty) noexcept {
    if (next.mode == CanvasSurfaceMode::Hidden) {
        return false;
    }
    if (previous.mode != next.mode ||
        previous.surface_width != next.surface_width ||
        previous.surface_height != next.surface_height) {
        return true;
    }
    if (next.mode == CanvasSurfaceMode::DebugFullCanvas) {
        return debug_content_dirty;
    }
    if (next.mode == CanvasSurfaceMode::TargetMarker) {
        return std::fabs(previous.marker_radius - next.marker_radius) > 0.001f;
    }
    return false;
}

}  // namespace fusion_overlay
