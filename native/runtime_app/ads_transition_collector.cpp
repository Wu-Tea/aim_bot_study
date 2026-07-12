#include "ads_transition_collector.h"

#include <cmath>

namespace runtime_app {

AdsTransitionCollector::AdsTransitionCollector(AdsTransitionCollectorOptions options)
    : options_(options), visual_(options.visual) {}

void AdsTransitionCollector::observe_hipfire(const AdsVisualFrame& frame) noexcept {
    if (active_) return;
    has_hipfire_ = frame.live && frame.target_track_id != 0 && high_quality(frame);
    if (has_hipfire_) hipfire_ = frame;
}

void AdsTransitionCollector::on_ads_pressed(std::uint64_t timestamp_ns) noexcept {
    if (active_) invalidate(AdsInvalidReason::AdsNotSettled);
    active_event_id_ = next_event_id_++;
    pressed_at_ns_ = timestamp_ns;
    cumulative_manual_ = cumulative_ai_ = cumulative_recoil_ = 0.0f;
    new_frames_ = 0;
    gaps_ = 0;
    if (!has_hipfire_) {
        active_ = true;
        invalidate(AdsInvalidReason::NoHipfireTarget);
        return;
    }
    active_ = true;
    first_seq_ = hipfire_.sample_seq;
    last_seq_ = hipfire_.sample_seq;
    visual_.start(hipfire_);
}

void AdsTransitionCollector::observe_vision(const AdsVisualFrame& frame) noexcept {
    if (!active_) return;
    if (!frame.live) {
        invalidate(AdsInvalidReason::TargetLost);
        return;
    }
    if (frame.target_track_id != hipfire_.target_track_id) {
        invalidate(AdsInvalidReason::TargetSwitched);
        return;
    }
    if (!high_quality(frame)) {
        invalidate(AdsInvalidReason::IdentityAmbiguous);
        return;
    }
    if (last_seq_ != 0 && frame.sample_seq > last_seq_ + 1)
        gaps_ += static_cast<std::uint32_t>(frame.sample_seq - last_seq_ - 1);
    last_seq_ = frame.sample_seq;
    ++new_frames_;
    const AdsVisualEvidence evidence = visual_.observe(frame);
    if (evidence.settled) complete(frame, evidence);
}

void AdsTransitionCollector::observe_command(
    float manual_integral, float ai_integral, float recoil_integral) noexcept {
    if (!active_) return;
    cumulative_manual_ += std::fabs(manual_integral);
    cumulative_ai_ += std::fabs(ai_integral);
    cumulative_recoil_ += std::fabs(recoil_integral);
}

void AdsTransitionCollector::on_tick(std::uint64_t timestamp_ns) noexcept {
    if (active_ && timestamp_ns > pressed_at_ns_ &&
        timestamp_ns - pressed_at_ns_ >= options_.timeout_ns)
        invalidate(AdsInvalidReason::AdsNotSettled);
}

void AdsTransitionCollector::on_required_sample_dropped() noexcept {
    if (active_) invalidate(AdsInvalidReason::QueueOverflow);
}

void AdsTransitionCollector::shutdown(std::uint64_t) noexcept {
    if (active_) invalidate(AdsInvalidReason::RuntimeShutdown);
}

std::optional<AdsTransitionEvent> AdsTransitionCollector::take_completed() noexcept {
    auto result = completed_;
    completed_.reset();
    return result;
}

void AdsTransitionCollector::invalidate(AdsInvalidReason reason) noexcept {
    AdsTransitionEvent event;
    event.ads_event_id = active_event_id_;
    event.target_track_id = hipfire_.target_track_id;
    event.hipfire_frame_id = hipfire_.frame_id;
    event.invalid_reason = reason;
    event.completeness.first_seq = first_seq_;
    event.completeness.last_seq = last_seq_;
    event.completeness.written = new_frames_ + (has_hipfire_ ? 1u : 0u);
    event.completeness.expected = event.completeness.written + gaps_;
    event.completeness.dropped = gaps_;
    event.completeness.complete = false;
    completed_ = event;
    active_ = false;
    visual_.reset();
}

void AdsTransitionCollector::complete(
    const AdsVisualFrame& frame, const AdsVisualEvidence& evidence) noexcept {
    AdsTransitionEvent event;
    event.ads_event_id = active_event_id_;
    event.target_track_id = hipfire_.target_track_id;
    event.hipfire_frame_id = hipfire_.frame_id;
    event.settled_frame_id = frame.frame_id;
    event.hipfire_dx = hipfire_.target_x - hipfire_.screen_center_x;
    event.hipfire_dy = hipfire_.target_y - hipfire_.screen_center_y;
    event.ads_dx = frame.target_x - frame.screen_center_x;
    event.ads_dy = frame.target_y - frame.screen_center_y;
    event.delta_dx = event.ads_dx - event.hipfire_dx;
    event.delta_dy = event.ads_dy - event.hipfire_dy;
    event.scale_x = evidence.scale_x;
    event.scale_y = evidence.scale_y;
    event.offset_x = evidence.offset_x;
    event.offset_y = evidence.offset_y;
    event.settle_confidence = evidence.settle_confidence;
    event.cumulative_manual = cumulative_manual_;
    event.cumulative_ai = cumulative_ai_;
    event.cumulative_recoil = cumulative_recoil_;
    event.completeness.first_seq = first_seq_;
    event.completeness.last_seq = last_seq_;
    event.completeness.written = new_frames_ + 1;
    event.completeness.expected = last_seq_ >= first_seq_
        ? static_cast<std::uint32_t>(last_seq_ - first_seq_ + 1) : 0;
    event.completeness.dropped = gaps_;
    event.completeness.complete = gaps_ == 0 &&
        new_frames_ >= options_.minimum_new_frames;
    if (!event.completeness.complete) {
        event.invalid_reason = gaps_ > 0
            ? AdsInvalidReason::SampleGap : AdsInvalidReason::InsufficientFrames;
    } else {
        event.valid = true;
        event.readiness = TelemetryReadiness::ModelEligible;
        const float command_total = cumulative_manual_ + cumulative_ai_ + cumulative_recoil_;
        event.calibration_class = evidence.clean_calibration &&
            command_total <= options_.clean_command_integral
            ? AdsCalibrationClass::CalibrationClean
            : AdsCalibrationClass::ConditionalModel;
    }
    completed_ = event;
    active_ = false;
    visual_.reset();
}

bool AdsTransitionCollector::high_quality(const AdsVisualFrame& frame) const noexcept {
    return frame.identity_quality == TargetIdentityQuality::ProductionAssociated ||
        frame.identity_quality == TargetIdentityQuality::StrongGeometricMatch;
}

} // namespace runtime_app
