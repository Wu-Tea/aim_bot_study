#include "control_response_window.h"

#include <algorithm>

namespace runtime_app {
namespace {

float seconds_between(std::uint64_t from, std::uint64_t to) noexcept {
    return to > from ? static_cast<float>(to - from) / 1'000'000'000.0f : 0.0f;
}

} // namespace

void ControlResponseWindowAssembler::observe_controller(
    const ResponseControllerSample& sample) noexcept {
    if (!has_anchor_ || sample.output_sent_ns <= anchor_.captured_at_ns) return;
    if (command_count_ < commands_.size()) commands_[command_count_++] = sample;
    else ++overflow_;
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

    std::uint64_t first_seq = 0;
    std::uint64_t last_seq = 0;
    for (std::size_t i = 0; i < command_count_; ++i) {
        const auto& value = commands_[i];
        if (value.output_sent_ns >= frame.captured_at_ns) break;
        const std::uint64_t end_ns = i + 1 < command_count_
            ? std::min(commands_[i + 1].output_sent_ns, frame.captured_at_ns)
            : frame.captured_at_ns;
        const float dt = seconds_between(value.output_sent_ns, end_ns);
        result.physical_x_integral += value.physical_x * dt;
        result.physical_y_integral += value.physical_y * dt;
        result.manual_x_integral += value.manual_x * dt;
        result.manual_y_integral += value.manual_y * dt;
        result.ai_x_integral += value.ai_x * dt;
        result.ai_y_integral += value.ai_y * dt;
        result.pre_recoil_x_integral += value.pre_recoil_x * dt;
        result.pre_recoil_y_integral += value.pre_recoil_y * dt;
        result.recoil_x_integral += value.recoil_x * dt;
        result.recoil_y_integral += value.recoil_y * dt;
        result.final_x_integral += value.final_x * dt;
        result.final_y_integral += value.final_y * dt;
        if (first_seq == 0) first_seq = value.sample_seq;
        last_seq = value.sample_seq;
        ++result.completeness.written;
    }
    result.completeness.first_seq = first_seq;
    result.completeness.last_seq = last_seq;
    result.completeness.expected = first_seq != 0 && last_seq >= first_seq
        ? static_cast<std::uint32_t>(last_seq - first_seq + 1) : 0;
    result.completeness.dropped = overflow_ +
        (result.completeness.expected > result.completeness.written
            ? result.completeness.expected - result.completeness.written : 0);
    result.completeness.complete = result.completeness.written > 0 &&
        result.completeness.dropped == 0;

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
    command_count_ = 0;
    overflow_ = 0;
    return result;
}

void ControlResponseWindowAssembler::reset() noexcept {
    anchor_ = ResponseVisionFrame{};
    has_anchor_ = false;
    command_count_ = 0;
    overflow_ = 0;
}

bool ControlResponseWindowAssembler::high_quality(TargetIdentityQuality quality) const noexcept {
    return quality == TargetIdentityQuality::ProductionAssociated ||
        quality == TargetIdentityQuality::StrongGeometricMatch;
}

} // namespace runtime_app
