#include "control_response_window.h"

namespace runtime_app {
void ControlResponseWindowAssembler::observe_controller(
    const ResponseControllerSample& sample) noexcept {
    control_learning::DeliveredControlSample delivered;
    delivered.sample_seq = sample.sample_seq;
    delivered.applied_at_ns = sample.output_sent_ns;
    delivered.physical_right = {sample.physical_x, sample.physical_y};
    delivered.physical_left = {sample.physical_left_x, sample.physical_left_y};
    delivered.manual_component = {sample.manual_x, sample.manual_y};
    delivered.ai_component = {sample.ai_x, sample.ai_y};
    delivered.pre_recoil = {sample.pre_recoil_x, sample.pre_recoil_y};
    delivered.recoil_component = {sample.recoil_x, sample.recoil_y};
    delivered.final_right = {sample.final_x, sample.final_y};
    delivered.final_left = {sample.final_left_x, sample.final_left_y};
    delivered.ads_epoch = sample.ads_epoch;
    delivered.output_delivered = sample.output_delivered;
    delivered.output_disabled = sample.output_disabled;
    delivered.firing = sample.firing;
    delivered.recoil_active = sample.recoil_active;
    delivered.saturated = sample.saturated;
    history_.push(delivered);
}

std::optional<ControlResponseWindow> ControlResponseWindowAssembler::observe_vision(
    const ResponseVisionFrame& frame) noexcept {
    if (!has_anchor_) {
        anchor_ = frame;
        has_anchor_ = true;
        return std::nullopt;
    }
    if (frame.frame_id == anchor_.frame_id) return std::nullopt;

    ControlResponseWindow result;
    result.frame_id_before = anchor_.frame_id;
    result.frame_id_after = frame.frame_id;
    result.target_track_id = anchor_.target_track_id;
    result.delta_error_x = frame.dx - anchor_.dx;
    result.delta_error_y = frame.dy - anchor_.dy;
    result.residual_x = result.delta_error_x - frame.predicted_motion_x;
    result.residual_y = result.delta_error_y - frame.predicted_motion_y;

    const auto interval = history_.integrate(
        anchor_.captured_at_ns, frame.captured_at_ns);
    result.physical_x_integral = interval.physical_right_stick_seconds.x;
    result.physical_y_integral = interval.physical_right_stick_seconds.y;
    result.manual_x_integral = interval.manual_stick_seconds.x;
    result.manual_y_integral = interval.manual_stick_seconds.y;
    result.ai_x_integral = interval.ai_stick_seconds.x;
    result.ai_y_integral = interval.ai_stick_seconds.y;
    result.pre_recoil_x_integral = interval.pre_recoil_stick_seconds.x;
    result.pre_recoil_y_integral = interval.pre_recoil_stick_seconds.y;
    result.recoil_x_integral = interval.recoil_stick_seconds.x;
    result.recoil_y_integral = interval.recoil_stick_seconds.y;
    result.final_x_integral = interval.final_right_stick_seconds.x;
    result.final_y_integral = interval.final_right_stick_seconds.y;
    result.completeness.first_seq = interval.first_seq;
    result.completeness.last_seq = interval.last_seq;
    result.completeness.expected = interval.expected;
    result.completeness.written = interval.written;
    result.completeness.dropped = interval.expected > interval.written
        ? interval.expected - interval.written : 0;
    result.completeness.complete = interval.complete;

    const bool same_target = anchor_.target_track_id != 0 &&
        anchor_.target_track_id == frame.target_track_id;
    const bool same_geometry = anchor_.frame_width == frame.frame_width &&
        anchor_.frame_height == frame.frame_height;
    const bool identities_ok = high_quality(anchor_.identity_quality) &&
        high_quality(frame.identity_quality) && anchor_.live && frame.live;
    const bool timing_ok = frame.captured_at_ns > anchor_.captured_at_ns;
    if (!same_target) result.reason = ResponseWindowReason::TargetChanged;
    else if (!identities_ok) result.reason = ResponseWindowReason::IdentityWeak;
    else if (!same_geometry) result.reason = ResponseWindowReason::GeometryChanged;
    else if (!result.completeness.complete) result.reason = ResponseWindowReason::SampleGap;
    else if (!timing_ok) result.reason = ResponseWindowReason::TimingInvalid;
    else result.readiness = TelemetryReadiness::ModelEligible;

    anchor_ = frame;
    return result;
}

void ControlResponseWindowAssembler::reset() noexcept {
    anchor_ = ResponseVisionFrame{};
    has_anchor_ = false;
    history_.clear();
}

bool ControlResponseWindowAssembler::high_quality(TargetIdentityQuality quality) const noexcept {
    return quality == TargetIdentityQuality::ProductionAssociated ||
        quality == TargetIdentityQuality::StrongGeometricMatch;
}

} // namespace runtime_app
