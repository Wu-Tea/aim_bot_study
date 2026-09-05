#include "fusion_overlay_contract.h"
#include "overlay_window_policy.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 0.001f) {
        throw std::runtime_error(message);
    }
}

fusion_overlay::CaptureIsolationObservation verified_observation() {
    return {
        true,
        true,
        true,
        true,
        fusion_overlay::kExcludeFromCaptureAffinity,
    };
}

void capture_isolation_is_fail_closed() {
    auto observation = verified_observation();
    require(
        fusion_overlay::decide_capture_isolation(observation).may_show,
        "fully verified capture isolation must allow the overlay");

    observation = verified_observation();
    observation.dwm_query_succeeded = false;
    require(
        !fusion_overlay::decide_capture_isolation(observation).may_show,
        "DWM query failure must fail closed");

    observation = verified_observation();
    observation.dwm_composition_enabled = false;
    require(
        !fusion_overlay::decide_capture_isolation(observation).may_show,
        "disabled DWM composition must fail closed");

    observation = verified_observation();
    observation.set_affinity_succeeded = false;
    require(
        !fusion_overlay::decide_capture_isolation(observation).may_show,
        "affinity setting failure must fail closed");

    observation = verified_observation();
    observation.readback_succeeded = false;
    require(
        !fusion_overlay::decide_capture_isolation(observation).may_show,
        "affinity readback failure must fail closed");

    observation = verified_observation();
    observation.affinity = 0x00000001u;
    require(
        !fusion_overlay::decide_capture_isolation(observation).may_show,
        "an affinity mismatch must fail closed");
}

void display_change_hides_then_revalidates_capture_isolation() {
    using Event = fusion_overlay::CaptureIsolationLifecycleEvent;
    using State = fusion_overlay::CaptureIsolationLifecycleState;

    const auto invalidated = fusion_overlay::transition_capture_isolation(
        State::Verified,
        Event::IsolationInvalidated);
    require(
        invalidated.state == State::RevalidationPending,
        "a display/DWM change must enter capture-isolation revalidation");
    require(
        !invalidated.may_show,
        "the canvas must remain hidden while isolation is unproven");
    require(
        invalidated.should_revalidate,
        "a display/DWM change must request the production capture probe");

    const auto recovered = fusion_overlay::transition_capture_isolation(
        invalidated.state,
        Event::RevalidationPassed);
    require(
        recovered.state == State::Verified && recovered.may_show,
        "a successful revalidation must restore the verified canvas");

    const auto failed = fusion_overlay::transition_capture_isolation(
        invalidated.state,
        Event::RevalidationFailed);
    require(
        failed.state == State::FailedClosed && !failed.may_show,
        "a failed revalidation must remain fail closed");

    const auto periodic_failure = fusion_overlay::transition_capture_isolation(
        State::Verified,
        Event::VerificationFailed);
    require(
        periodic_failure.state == State::FailedClosed &&
            !periodic_failure.may_show &&
            !periodic_failure.should_revalidate,
        "a proven periodic isolation loss must fail closed without retrying");

    const auto terminal = fusion_overlay::transition_capture_isolation(
        failed.state,
        Event::RevalidationPassed);
    require(
        terminal.state == State::FailedClosed && !terminal.may_show,
        "a failed-closed session must not recover without a new process");
}

void canvas_window_style_is_cross_process_mouse_passthrough() {
    const DWORD extended_style = fusion_overlay::fusion_canvas_extended_style();
    const DWORD window_style = fusion_overlay::fusion_canvas_window_style();
    require(
        fusion_overlay::satisfies_mouse_passthrough_contract(
            extended_style,
            window_style),
        "the production Canvas style must provide cross-process mouse passthrough");
    require(
        (extended_style & WS_EX_LAYERED) != 0,
        "the production Canvas must be a layered window");
    require(
        (extended_style & WS_EX_TRANSPARENT) != 0,
        "the production Canvas must ignore mouse hit testing");
    require(
        (extended_style & WS_EX_NOREDIRECTIONBITMAP) == 0,
        "a no-redirection DirectComposition window cannot own mouse passthrough");
    require(
        (window_style & WS_DISABLED) != 0,
        "the production Canvas must be disabled before it is shown");
    require(
        !fusion_overlay::satisfies_mouse_passthrough_contract(
            extended_style,
            window_style & ~WS_DISABLED),
        "layered transparency alone must not satisfy the input contract");
}

void marker_tracks_selector_target_in_roi_desktop_coordinates() {
    fusion_overlay::MarkerLayoutInput input;
    input.has_target = true;
    input.direct_observation = true;
    input.frame_width = 640;
    input.frame_height = 512;
    input.output_left = 1920;
    input.output_top = -120;
    input.output_width = 2560;
    input.output_height = 1440;
    input.roi_left = 640;
    input.roi_top = 284;
    input.virtual_left = -1280;
    input.virtual_top = -120;
    input.virtual_width = 5120;
    input.virtual_height = 1440;
    input.target_x = 0.61f;
    input.target_y = 0.47f;
    input.marker_radius_px = 6.0f;

    const auto marker = fusion_overlay::layout_target_point_marker(input);
    require(marker.visible, "a fresh direct selector target must produce a marker");

    const float expected_target_x =
        static_cast<float>(input.output_left + input.roi_left - input.virtual_left) +
        (input.target_x * input.frame_width);
    const float expected_target_y =
        static_cast<float>(input.output_top + input.roi_top - input.virtual_top) +
        (input.target_y * input.frame_height);
    require_near(marker.center_x, expected_target_x, "marker x must track selector target_x");
    require_near(marker.center_y, expected_target_y, "marker y must track selector target_y");
}

void marker_rejects_non_direct_or_invalid_targets() {
    fusion_overlay::MarkerLayoutInput input;
    input.has_target = true;
    input.direct_observation = false;
    input.frame_width = 640;
    input.frame_height = 512;
    input.virtual_width = 1920;
    input.virtual_height = 1080;
    input.output_width = 1920;
    input.output_height = 1080;
    input.roi_left = 640;
    input.roi_top = 284;
    input.target_x = 0.5f;
    input.target_y = 0.5f;
    require(
        !fusion_overlay::layout_target_point_marker(input).visible,
        "cue-only continuation must not produce a visibility marker");

    input.direct_observation = true;
    input.target_x = 1.01f;
    require(
        !fusion_overlay::layout_target_point_marker(input).visible,
        "an out-of-range selector target must not produce a marker");

    input.target_x = 0.5f;
    input.roi_left = 1600;
    require(
        !fusion_overlay::layout_target_point_marker(input).visible,
        "an ROI extending beyond the published output must fail closed");
}

void production_marker_uses_a_small_movable_surface() {
    fusion_overlay::CanvasPresentationInput input;
    input.marker_visible = true;
    input.virtual_left = -1280;
    input.virtual_top = -120;
    input.virtual_width = 5120;
    input.virtual_height = 1440;
    input.marker_center_x = 3700.25f;
    input.marker_center_y = 612.75f;
    input.marker_radius = 6.0f;

    const auto first = fusion_overlay::decide_canvas_presentation(input);
    require(
        first.mode == fusion_overlay::CanvasSurfaceMode::TargetMarker,
        "a production target cue must use the marker-sprite path");
    require(
        first.surface_width == fusion_overlay::kTargetMarkerSurfaceExtentPx &&
            first.surface_height == fusion_overlay::kTargetMarkerSurfaceExtentPx,
        "a target cue must not allocate a virtual-desktop-sized surface");
    require(
        first.surface_width * first.surface_height <
            input.virtual_width * input.virtual_height / 1000,
        "the production marker surface must remain negligible beside the desktop");
    require(
        std::fabs(
            static_cast<float>(first.window_left) + first.content_center_x -
            (static_cast<float>(input.virtual_left) + input.marker_center_x)) <= 0.5f,
        "the marker sprite must preserve the selector-owned desktop x coordinate");
    require(
        std::fabs(
            static_cast<float>(first.window_top) + first.content_center_y -
            (static_cast<float>(input.virtual_top) + input.marker_center_y)) <= 0.5f,
        "the marker sprite must preserve the selector-owned desktop y coordinate");

    auto moved_input = input;
    moved_input.marker_center_x += 80.0f;
    const auto moved = fusion_overlay::decide_canvas_presentation(moved_input);
    require(
        moved.window_left != first.window_left,
        "target motion must move the sprite window");
    require(
        !fusion_overlay::canvas_surface_redraw_required(first, moved, false),
        "moving an unchanged marker must not Present another surface");
}

void hidden_idle_does_not_own_a_composition_surface() {
    fusion_overlay::CanvasPresentationInput input;
    input.virtual_width = 1920;
    input.virtual_height = 1080;

    const auto hidden = fusion_overlay::decide_canvas_presentation(input);
    require(
        hidden.mode == fusion_overlay::CanvasSurfaceMode::Hidden,
        "idle_mode=hide without a target must hide the top-level window");
    require(
        hidden.surface_width == 0 && hidden.surface_height == 0,
        "hidden idle must not request a transparent full-screen Present");
    require(
        !fusion_overlay::canvas_surface_redraw_required({}, hidden, false),
        "hidden idle updates must not redraw or Present");

    input.idle_crosshair = true;
    const auto crosshair = fusion_overlay::decide_canvas_presentation(input);
    require(
        crosshair.mode == fusion_overlay::CanvasSurfaceMode::IdleCrosshair &&
            crosshair.surface_width == fusion_overlay::kTargetMarkerSurfaceExtentPx &&
            crosshair.surface_height == fusion_overlay::kTargetMarkerSurfaceExtentPx,
        "the optional idle crosshair must also use a small surface");
    require(
        fusion_overlay::canvas_surface_redraw_required(hidden, crosshair, false),
        "showing the idle crosshair must draw its initial surface");
    require(
        !fusion_overlay::canvas_surface_redraw_required(
            crosshair,
            crosshair,
            false),
        "an unchanged idle crosshair must not Present continuously");

    input.idle_crosshair = false;
    input.show_debug_detections = true;
    const auto debug = fusion_overlay::decide_canvas_presentation(input);
    require(
        debug.mode == fusion_overlay::CanvasSurfaceMode::DebugFullCanvas &&
            debug.surface_width == input.virtual_width &&
            debug.surface_height == input.virtual_height,
        "only explicit debug detections may request the full desktop surface");
    require(
        fusion_overlay::canvas_surface_redraw_required(hidden, debug, true),
        "debug detection content must redraw its full canvas");

    input.visibility_enabled = false;
    require(
        fusion_overlay::decide_canvas_presentation(input).mode ==
            fusion_overlay::CanvasSurfaceMode::Hidden,
        "the operator visibility toggle must hide even explicit debug content");
}

void publisher_qpc_owns_marker_freshness() {
    constexpr std::uint64_t frequency = 10'000'000;
    constexpr std::uint64_t published = 50'000'000;
    require(
        fusion_overlay::fusion_sample_is_fresh(
            published + 1'200'000, published, frequency, 120),
        "the exact 120 ms boundary must remain fresh");
    require(
        !fusion_overlay::fusion_sample_is_fresh(
            published + 1'200'001, published, frequency, 120),
        "a sample older than 120 ms must be stale");
    require(
        !fusion_overlay::fusion_sample_is_fresh(
            published - 1, published, frequency, 120),
        "a future publisher timestamp must fail closed");
    require(
        !fusion_overlay::fusion_sample_is_fresh(
            published + 1, published, 0, 120),
        "a missing QPC frequency must fail closed");
}

void recorded_short_direct_burst_survives_render_throttle() {
    // Frozen from video SHA-256
    // 52DAD97B51B0FC366ADE76C18479A6DA529F2BF2D2BB441E8FF11A13723CDBF6,
    // frames 1333-1342. Frame 1337 is a one-frame detector gap where the
    // selector preserves the confirmed generation as cue_hold.
    constexpr std::uint64_t frequency = 120'000;
    constexpr std::uint64_t direct_qpc = 1'336'000;

    fusion_overlay::MarkerContinuityInput input;
    input.latest_sample_fresh = true;
    input.latest_has_target = true;
    input.latest_has_body_box = true;
    input.latest_enemy_identity_confirmed = true;
    input.latest_target_generation = 12;
    input.latched_direct_available = true;
    input.latched_target_generation = 12;
    input.latched_published_qpc = direct_qpc;
    input.now_qpc = direct_qpc + 1'000;
    input.qpc_frequency = frequency;
    input.hold_ms = 120;

    require(
        fusion_overlay::decide_marker_continuity(input) ==
            fusion_overlay::MarkerContinuitySource::LatchedConfirmedContinuation,
        "a confirmed same-generation cue hold must bridge the recorded 8.3 ms gap");

    input.latest_has_target = false;
    input.latest_has_body_box = false;
    input.latest_enemy_identity_confirmed = false;
    input.latched_direct_pending_render = true;
    require(
        fusion_overlay::decide_marker_continuity(input) ==
            fusion_overlay::MarkerContinuitySource::LatchedDirectPendingRender,
        "a direct burst consumed before the 30 FPS render tick must render once");

    input.latched_direct_pending_render = false;
    require(
        fusion_overlay::decide_marker_continuity(input) ==
            fusion_overlay::MarkerContinuitySource::None,
        "an unconfirmed empty sample must not extend a rendered marker");

    input.latest_has_target = true;
    input.latest_has_body_box = true;
    input.latest_enemy_identity_confirmed = true;
    input.latest_target_generation = 13;
    require(
        fusion_overlay::decide_marker_continuity(input) ==
            fusion_overlay::MarkerContinuitySource::None,
        "a new selector generation must invalidate the old marker latch");

    input.latest_target_generation = 12;
    input.now_qpc = direct_qpc + 14'401;
    require(
        fusion_overlay::decide_marker_continuity(input) ==
            fusion_overlay::MarkerContinuitySource::None,
        "a confirmed continuation older than 120 ms must expire");
}

}  // namespace

int main() {
    try {
        capture_isolation_is_fail_closed();
        display_change_hides_then_revalidates_capture_isolation();
        canvas_window_style_is_cross_process_mouse_passthrough();
        marker_tracks_selector_target_in_roi_desktop_coordinates();
        marker_rejects_non_direct_or_invalid_targets();
        production_marker_uses_a_small_movable_surface();
        hidden_idle_does_not_own_a_composition_surface();
        publisher_qpc_owns_marker_freshness();
        recorded_short_direct_burst_survives_render_throttle();
        std::cout << "[PASS] FusionOverlayContracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] FusionOverlayContracts: " << error.what() << '\n';
        return 1;
    }
}
