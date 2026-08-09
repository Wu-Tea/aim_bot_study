#include "gate2_5_live_shadow.h"

#include <algorithm>
#include <cmath>

namespace runtime_app {
namespace {

bool finite(float value) noexcept { return std::isfinite(value); }

float magnitude(float x, float y) noexcept {
    return std::sqrt(x * x + y * y);
}

std::size_t command_magnitude_bin(float value) noexcept {
    // Nearest nominal command ranges, rather than cumulative <= thresholds:
    // 1/2/3/5/10% are separated at their midpoints. The final cell is the
    // explicit >10% cohort. A small float-integral tolerance keeps exact
    // percentage fixtures stable at their named values.
    constexpr float kBoundaryTolerance = 0.0001f;
    return value <= 0.015f + kBoundaryTolerance ? 0
        : value <= 0.025f + kBoundaryTolerance ? 1
        : value <= 0.040f + kBoundaryTolerance ? 2
        : value <= 0.075f + kBoundaryTolerance ? 3
        : value <= 0.125f + kBoundaryTolerance ? 4 : 5;
}

std::size_t output_axis_bin(float x, float y) noexcept {
    const float value = magnitude(x, y);
    if (value <= 0.0001f) return 3;
    if (std::fabs(x) >= 2.0f * std::fabs(y)) return 0;
    if (std::fabs(y) >= 2.0f * std::fabs(x)) return 1;
    return 2;
}

std::size_t cohort_index(
    std::uint8_t mode,
    std::size_t axis,
    std::size_t command_bin) noexcept {
    const std::size_t bounded_mode = std::min<std::size_t>(
        mode, kGate25ModeCapacity - 1);
    return (bounded_mode * kGate25AxisCapacity + axis) *
        kGate25CommandBinCapacity + command_bin;
}

// RuntimeLoop can prove that the estimate belongs to the current endpoint,
// but it cannot know which endpoint Gate25 accepted as its previous pair:
// rejected geometry, duplicate, stale, or backward observations deliberately
// do not advance previous_.  Only compare the controller estimate after its
// complete previous/current provenance matches that accepted Gate pair.
bool ledger_join_matches_accepted_pair(
    const Gate25ObservationInput& input,
    const Gate25ObservationInput& prior) noexcept {
    return input.ledger_realized_available &&
        input.ledger_physical_actuator_epoch != 0 &&
        input.ledger_physical_actuator_epoch == input.backend_epoch &&
        input.ledger_previous_source_frame_id == prior.source_frame_id &&
        input.ledger_previous_source_observation_id ==
            prior.source_observation_id &&
        input.ledger_previous_present_steady_ns ==
            prior.source_present_steady_ns &&
        input.ledger_previous_present_calibration_id ==
            prior.source_present_calibration_id &&
        input.ledger_previous_present_qpc_frequency ==
            prior.source_present_qpc_frequency &&
        input.ledger_previous_target_id == prior.persistent_target_id &&
        input.ledger_previous_ads_epoch == prior.physical_ads_epoch &&
        input.ledger_current_source_frame_id == input.source_frame_id &&
        input.ledger_current_source_observation_id ==
            input.source_observation_id &&
        input.ledger_current_present_steady_ns ==
            input.source_present_steady_ns &&
        input.ledger_current_present_calibration_id ==
            input.source_present_calibration_id &&
        input.ledger_current_present_qpc_frequency ==
            input.source_present_qpc_frequency &&
        input.ledger_current_target_id == input.persistent_target_id &&
        input.ledger_current_ads_epoch == input.physical_ads_epoch &&
        finite(input.ledger_realized_x) && finite(input.ledger_realized_y);
}

}  // namespace

bool Gate25DeliveryView::push(const Gate25DeliverySample& sample) noexcept {
    if (sample.sample_seq == 0 || sample.applied_at_ns == 0) return false;
    if (size_ != 0) {
        const auto& prior = entry(size_ - 1);
        if (sample.sample_seq <= prior.sample_seq ||
            sample.applied_at_ns <= prior.applied_at_ns) {
            return false;
        }
    }
    if (size_ < kCapacity) {
        entries_[(head_ + size_) % kCapacity] = sample;
        ++size_;
    } else {
        entries_[head_] = sample;
        head_ = (head_ + 1) % kCapacity;
        ++overwritten_;
    }
    ++push_count_;
    return true;
}

void Gate25DeliveryView::reset() noexcept {
    head_ = 0;
    size_ = 0;
    overwritten_ = 0;
    push_count_ = 0;
}

std::size_t Gate25DeliveryView::floor_index(
    std::uint64_t time_ns) const noexcept {
    std::size_t low = 0;
    std::size_t high = size_;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (entry(middle).applied_at_ns <= time_ns) low = middle + 1;
        else high = middle;
    }
    return low == 0 ? 0 : low - 1;
}

control_learning::ControlIntegral Gate25DeliveryView::integrate(
    std::uint64_t begin_ns,
    std::uint64_t end_ns) const noexcept {
    control_learning::ControlIntegral result;
    if (size_ == 0 || end_ns <= begin_ns || size_ < 2 ||
        begin_ns < entry(0).applied_at_ns ||
        end_ns > entry(size_ - 1).applied_at_ns) {
        return result;
    }

    const std::size_t first = floor_index(begin_ns);
    std::size_t index = first;
    std::uint64_t first_seq = 0;
    std::uint64_t last_seq = 0;
    std::uint32_t written = 0;
    std::uint64_t active_epoch = entry(first).backend_epoch;
    bool epoch_boundary = false;
    while (index < size_) {
        const auto& held = entry(index);
        const std::uint64_t segment_begin =
            std::max(begin_ns, held.applied_at_ns);
        const std::uint64_t next_time = index + 1 < size_
            ? entry(index + 1).applied_at_ns : end_ns;
        const std::uint64_t segment_end = std::min(end_ns, next_time);
        if (segment_end > segment_begin) {
            if (first_seq == 0) first_seq = held.sample_seq;
            last_seq = held.sample_seq;
            ++written;
            if (held.backend_epoch != active_epoch) epoch_boundary = true;
            if (!held.output_delivered) result.failed_delivery = true;
            if (!held.output_enabled) result.output_disabled = true;
            result.firing = result.firing || held.firing;
            result.recoil_active = result.recoil_active || held.recoil_active;
            result.saturated = result.saturated || held.saturated;
            const double seconds = static_cast<double>(segment_end - segment_begin) /
                1'000'000'000.0;
            result.final_right_stick_seconds.x += held.final_right.x *
                static_cast<float>(seconds);
            result.final_right_stick_seconds.y += held.final_right.y *
                static_cast<float>(seconds);
            result.physical_left_stick_seconds.x += held.physical_left.x *
                static_cast<float>(seconds);
            result.physical_left_stick_seconds.y += held.physical_left.y *
                static_cast<float>(seconds);
            result.manual_stick_seconds.x += held.manual.x *
                static_cast<float>(seconds);
            result.manual_stick_seconds.y += held.manual.y *
                static_cast<float>(seconds);
            result.ai_stick_seconds.x += held.ai.x *
                static_cast<float>(seconds);
            result.ai_stick_seconds.y += held.ai.y *
                static_cast<float>(seconds);
            const float final_abs = std::sqrt(
                held.final_right.x * held.final_right.x +
                held.final_right.y * held.final_right.y);
            result.final_right_stick_abs_seconds +=
                final_abs * static_cast<float>(seconds);
        }

        if (index + 1 >= size_ || entry(index + 1).applied_at_ns >= end_ns) {
            break;
        }
        const auto& next = entry(index + 1);
        if (held.output_delivered && next.output_delivered) {
            const float dot = held.final_right.x * next.final_right.x +
                held.final_right.y * next.final_right.y;
            const float held_abs = std::sqrt(
                held.final_right.x * held.final_right.x +
                held.final_right.y * held.final_right.y);
            const float next_abs = std::sqrt(
                next.final_right.x * next.final_right.x +
                next.final_right.y * next.final_right.y);
            result.final_right_reversal = result.final_right_reversal ||
                (dot < -1.0e-6f && held_abs > 1.0e-4f && next_abs > 1.0e-4f);
        }
        ++index;
    }

    result.first_seq = first_seq;
    result.last_seq = last_seq;
    result.written = written;
    if (first_seq != 0 && last_seq >= first_seq) {
        const std::uint64_t expected = last_seq - first_seq + 1;
        result.expected = expected > UINT32_MAX
            ? UINT32_MAX : static_cast<std::uint32_t>(expected);
    }
    // A backend epoch change is a physical-history boundary. It is not
    // converted into a neutral sample and therefore cannot become valid
    // merely because later reports are present.
    result.complete = written != 0 && result.expected == result.written &&
        !result.failed_delivery && !epoch_boundary;
    return result;
}

const char* gate25_reason_name(Gate25Reason reason) noexcept {
    switch (reason) {
    case Gate25Reason::None: return "none";
    case Gate25Reason::MissingPresentClock: return "missing_present_clock";
    case Gate25Reason::NoTargetIdentity: return "no_target_identity";
    case Gate25Reason::DuplicateObservation: return "duplicate_observation";
    case Gate25Reason::SamePresentEndpoint: return "same_present_endpoint";
    case Gate25Reason::StaleObservation: return "stale_observation";
    case Gate25Reason::BackwardPresent: return "backward_present";
    case Gate25Reason::CaptureGap: return "capture_gap";
    case Gate25Reason::IdentityBoundary: return "identity_boundary";
    case Gate25Reason::SelectorBoundary: return "selector_boundary";
    case Gate25Reason::AdsBoundary: return "ads_boundary";
    case Gate25Reason::ModeBoundary: return "mode_boundary";
    case Gate25Reason::ViewportBoundary: return "viewport_boundary";
    case Gate25Reason::BackendBoundary: return "backend_boundary";
    case Gate25Reason::TargetAcquisitionBoundary: return "target_acquisition_boundary";
    case Gate25Reason::ResponseDelayBoundary: return "response_delay_boundary";
    case Gate25Reason::PresentCalibrationBoundary:
        return "present_calibration_boundary";
    case Gate25Reason::MissingDeliveryHistory: return "missing_delivery_history";
    case Gate25Reason::IncompleteDeliveryHistory: return "incomplete_delivery_history";
    case Gate25Reason::MissingResponseDelay: return "missing_response_delay";
    case Gate25Reason::MissingNeutralBaseline: return "missing_neutral_baseline";
    case Gate25Reason::BelowNoiseFloor: return "below_noise_floor";
    case Gate25Reason::CancellationOrReversal:
        return "cancellation_or_reversal";
    case Gate25Reason::ExogenousMotion: return "exogenous_motion";
    case Gate25Reason::TargetMotionContamination: return "target_motion_contamination";
    case Gate25Reason::ResponseModelUnavailable:
        return "response_model_unavailable";
    case Gate25Reason::ProfileIdentityUnavailable:
        return "profile_identity_unavailable";
    case Gate25Reason::InvalidGeometry: return "invalid_geometry";
    default: return "unknown";
    }
}

const char* gate25_status_name(Gate25EvidenceStatus status) noexcept {
    return status == Gate25EvidenceStatus::EffectEvidenceAvailable
        ? "effect_evidence_available" : "insufficient_evidence";
}

void Gate25LiveShadow::reset_aggregate(std::uint64_t window_begin_ns) noexcept {
    aggregate_ = {};
    aggregate_.window_begin_ns = window_begin_ns;
    aggregate_.status = Gate25EvidenceStatus::InsufficientEvidence;
    update_baseline_summary();
}

void Gate25LiveShadow::reset_pair_state() noexcept {
    previous_ = {};
    last_effect_ = {};
    baseline_x_ = {};
    baseline_y_ = {};
    baseline_count_ = 0;
    baseline_head_ = 0;
    aggregate_.baseline_median_x = 0.0f;
    aggregate_.baseline_median_y = 0.0f;
    aggregate_.baseline_mad_x = 0.0f;
    aggregate_.baseline_mad_y = 0.0f;
}

void Gate25LiveShadow::record_observation_window(
    std::uint64_t present_ns,
    std::uint64_t controller_tick_id,
    Gate25WindowClockDomain clock_domain) noexcept {
    if (present_ns == 0) return;
    if (aggregate_.observations != 0 &&
        aggregate_.window_clock_domain != clock_domain) {
        pending_summary_ = aggregate_;
        pending_summary_.window_end_ns = aggregate_.window_end_ns;
        pending_summary_.anomaly_dropped = anomaly_dropped_;
        has_pending_summary_ = true;
        reset_aggregate(0);
        anomaly_dropped_ = 0;
    }
    if (aggregate_.observations == 0) {
        aggregate_.window_begin_ns = present_ns;
        aggregate_.window_clock_domain = clock_domain;
    } else if (present_ns >= aggregate_.window_begin_ns &&
               present_ns - aggregate_.window_begin_ns >= kAggregateWindowNs) {
        pending_summary_ = aggregate_;
        pending_summary_.window_end_ns = present_ns;
        pending_summary_.anomaly_dropped = anomaly_dropped_;
        has_pending_summary_ = true;
        // This carries the neutral baseline storage and recomputes its
        // summary instead of silently replacing it with zero.
        reset_aggregate(0);
        anomaly_dropped_ = 0;
        aggregate_.window_begin_ns = present_ns;
        aggregate_.window_clock_domain = clock_domain;
    } else if (present_ns < aggregate_.window_begin_ns) {
        // A rejected/backward endpoint must not move or reinterpret the
        // diagnostic window. The caller records its reason separately.
        return;
    }
    aggregate_.window_end_ns = present_ns;
    aggregate_.last_controller_tick_id = controller_tick_id;
    ++aggregate_.observations;
}

void Gate25LiveShadow::reset() noexcept {
    aggregate_ = {};
    pending_summary_ = {};
    has_pending_summary_ = false;
    previous_ = {};
    last_effect_ = {};
    baseline_x_ = {};
    baseline_y_ = {};
    baseline_count_ = 0;
    baseline_head_ = 0;
    anomalies_ = {};
    anomaly_head_ = 0;
    anomaly_count_ = 0;
    anomaly_dropped_ = 0;
    missing_clock_anomaly_emitted_ = false;
    invocation_count_ = 0;
}

bool Gate25LiveShadow::should_queue_anomaly(Gate25Reason reason) const noexcept {
    // Ordinary cohort rejection is represented by bounded aggregate counters.
    // The ring is reserved for integrity failures, lifecycle transitions and
    // model/clock boundaries; otherwise a no-target frame at Vision cadence
    // would become a persisted record on every frame.
    switch (reason) {
    case Gate25Reason::NoTargetIdentity:
    case Gate25Reason::MissingNeutralBaseline:
    case Gate25Reason::BelowNoiseFloor:
    case Gate25Reason::ExogenousMotion:
    case Gate25Reason::TargetMotionContamination:
    case Gate25Reason::ResponseModelUnavailable:
    case Gate25Reason::ProfileIdentityUnavailable:
    case Gate25Reason::InvalidGeometry:
    case Gate25Reason::CancellationOrReversal:
        return false;
    default:
        return true;
    }
}

void Gate25LiveShadow::count_reason(Gate25Reason reason) noexcept {
    switch (reason) {
    case Gate25Reason::MissingPresentClock: ++aggregate_.missing_present_clock; break;
    case Gate25Reason::DuplicateObservation: ++aggregate_.duplicate; break;
    case Gate25Reason::SamePresentEndpoint:
        ++aggregate_.same_present_endpoint; break;
    case Gate25Reason::StaleObservation: ++aggregate_.stale; break;
    case Gate25Reason::BackwardPresent: ++aggregate_.backward_present; break;
    case Gate25Reason::CaptureGap: ++aggregate_.capture_gap; break;
    case Gate25Reason::IdentityBoundary: ++aggregate_.identity_boundary; break;
    case Gate25Reason::SelectorBoundary: ++aggregate_.selector_boundary; break;
    case Gate25Reason::AdsBoundary: ++aggregate_.ads_boundary; break;
    case Gate25Reason::ModeBoundary: ++aggregate_.mode_boundary; break;
    case Gate25Reason::ViewportBoundary: ++aggregate_.viewport_boundary; break;
    case Gate25Reason::BackendBoundary: ++aggregate_.backend_boundary; break;
    case Gate25Reason::TargetAcquisitionBoundary:
        ++aggregate_.target_acquisition_boundary; break;
    case Gate25Reason::ResponseDelayBoundary:
        ++aggregate_.response_delay_boundary; break;
    case Gate25Reason::PresentCalibrationBoundary:
        ++aggregate_.present_calibration_boundary; break;
    case Gate25Reason::MissingDeliveryHistory: ++aggregate_.missing_delivery_history; break;
    case Gate25Reason::IncompleteDeliveryHistory: ++aggregate_.incomplete_delivery_history; break;
    case Gate25Reason::MissingResponseDelay: ++aggregate_.missing_response_delay; break;
    case Gate25Reason::CancellationOrReversal:
        ++aggregate_.cancellation_or_reversal; break;
    case Gate25Reason::InvalidGeometry: ++aggregate_.invalid_geometry; break;
    case Gate25Reason::ExogenousMotion: ++aggregate_.exogenous_rejected; break;
    case Gate25Reason::TargetMotionContamination:
        ++aggregate_.target_motion_contamination; break;
    case Gate25Reason::ResponseModelUnavailable:
        ++aggregate_.response_model_unavailable; break;
    case Gate25Reason::ProfileIdentityUnavailable:
        ++aggregate_.profile_identity_unavailable; break;
    case Gate25Reason::BelowNoiseFloor: ++aggregate_.below_noise_floor; break;
    case Gate25Reason::MissingNeutralBaseline:
        ++aggregate_.missing_neutral_baseline; break;
    case Gate25Reason::NoTargetIdentity:
        ++aggregate_.no_target_identity; break;
    case Gate25Reason::None: break;
    default: break;
    }
}

void Gate25LiveShadow::queue_anomaly(
    const Gate25ObservationInput& input,
    Gate25Reason reason,
    float target_delta_x,
    float target_delta_y,
    float observed_x,
    float observed_y,
    float snr) noexcept {
    Gate25Anomaly value;
    value.source_frame_id = input.source_frame_id;
    value.source_observation_id = input.source_observation_id;
    value.persistent_target_id = input.persistent_target_id;
    value.present_steady_ns = input.source_present_steady_ns;
    value.decision_ns = input.decision_ns;
    value.controller_tick_id = input.controller_tick_id;
    value.backend_epoch = input.backend_epoch;
    value.delivery_first_seq = last_effect_.delivery_first_seq;
    value.delivery_last_seq = last_effect_.delivery_last_seq;
    value.reason = reason;
    value.target_delta_x = target_delta_x;
    value.target_delta_y = target_delta_y;
    value.observed_camera_work_x = observed_x;
    value.observed_camera_work_y = observed_y;
    value.snr = snr;
    value.reliability = input.reliability;
    value.target_confidence = input.target_confidence;
    if (!should_queue_anomaly(reason)) return;
    if (reason == Gate25Reason::MissingPresentClock) {
        if (missing_clock_anomaly_emitted_) return;
        missing_clock_anomaly_emitted_ = true;
    }
    if (anomaly_count_ < kAnomalyCapacity) {
        anomalies_[(anomaly_head_ + anomaly_count_) % kAnomalyCapacity] = value;
        ++anomaly_count_;
    } else {
        anomalies_[anomaly_head_] = value;
        anomaly_head_ = (anomaly_head_ + 1) % kAnomalyCapacity;
        ++anomaly_dropped_;
    }
    if (aggregate_.anomaly_count != UINT32_MAX) ++aggregate_.anomaly_count;
}

void Gate25LiveShadow::add_neutral_baseline(float x, float y) noexcept {
    if (baseline_count_ < kBaselineCapacity) {
        baseline_x_[baseline_count_] = x;
        baseline_y_[baseline_count_] = y;
        ++baseline_count_;
    } else {
        baseline_x_[baseline_head_] = x;
        baseline_y_[baseline_head_] = y;
        baseline_head_ = (baseline_head_ + 1) % kBaselineCapacity;
    }
    ++aggregate_.neutral_baseline_pairs;
    update_baseline_summary();
}

float Gate25LiveShadow::baseline_median(
    const std::array<float, kBaselineCapacity>& values) const noexcept {
    std::array<float, kBaselineCapacity> ordered{};
    for (std::size_t i = 0; i < baseline_count_; ++i) {
        ordered[i] = values[(baseline_head_ + i) % kBaselineCapacity];
    }
    for (std::size_t i = 1; i < baseline_count_; ++i) {
        const float value = ordered[i];
        std::size_t j = i;
        while (j > 0 && ordered[j - 1] > value) {
            ordered[j] = ordered[j - 1];
            --j;
        }
        ordered[j] = value;
    }
    if (baseline_count_ == 0) return 0.0f;
    const std::size_t middle = baseline_count_ / 2;
    if ((baseline_count_ & 1u) != 0) return ordered[middle];
    return 0.5f * (ordered[middle - 1] + ordered[middle]);
}

float Gate25LiveShadow::baseline_mad(
    const std::array<float, kBaselineCapacity>& values,
    float median) const noexcept {
    std::array<float, kBaselineCapacity> deviations{};
    for (std::size_t i = 0; i < baseline_count_; ++i) {
        deviations[i] = std::fabs(values[(baseline_head_ + i) % kBaselineCapacity] - median);
    }
    return baseline_median(deviations);
}

void Gate25LiveShadow::update_baseline_summary() noexcept {
    aggregate_.baseline_median_x = baseline_median(baseline_x_);
    aggregate_.baseline_median_y = baseline_median(baseline_y_);
    aggregate_.baseline_mad_x = baseline_mad(baseline_x_, aggregate_.baseline_median_x);
    aggregate_.baseline_mad_y = baseline_mad(baseline_y_, aggregate_.baseline_median_y);
}

namespace {

control_learning::ControlIntegral integrate_control_history(
    const void* context,
    std::uint64_t begin_ns,
    std::uint64_t end_ns) noexcept {
    return static_cast<const control_learning::ControlHistory<1024>*>(
        context)->integrate(begin_ns, end_ns);
}

control_learning::ControlIntegral integrate_gate25_delivery_view(
    const void* context,
    std::uint64_t begin_ns,
    std::uint64_t end_ns) noexcept {
    return static_cast<const Gate25DeliveryView*>(context)->integrate(
        begin_ns, end_ns);
}

}  // namespace

void Gate25LiveShadow::observe(
    const Gate25ObservationInput& input,
    const control_learning::ControlHistory<1024>* delivery_history) noexcept {
    DeliveryIntegrator source;
    if (delivery_history != nullptr) {
        source.context = delivery_history;
        source.integrate = &integrate_control_history;
    }
    observe_impl(input, delivery_history != nullptr ? &source : nullptr);
}

void Gate25LiveShadow::observe(
    const Gate25ObservationInput& input,
    const Gate25DeliveryView& delivery_view) noexcept {
    DeliveryIntegrator source;
    source.context = &delivery_view;
    source.integrate = &integrate_gate25_delivery_view;
    observe_impl(input, &source);
}

void Gate25LiveShadow::observe_impl(
    const Gate25ObservationInput& input,
    const DeliveryIntegrator* delivery_source) noexcept {
    ++invocation_count_;
    last_effect_ = {};

    if (!input.source_present_available || !input.source_present_steady_available ||
        input.source_present_qpc == 0 || input.source_present_qpc_frequency == 0 ||
        input.source_present_steady_ns == 0 || input.source_present_calibration_id == 0) {
        const std::uint64_t diagnostic_ns = input.controller_consume_ns != 0
            ? input.controller_consume_ns : input.decision_ns;
        if (diagnostic_ns != 0) {
            record_observation_window(
                diagnostic_ns,
                input.controller_tick_id,
                Gate25WindowClockDomain::CollectorMonotonicDiagnostic);
        }
        const auto reason = Gate25Reason::MissingPresentClock;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        return;
    }
    missing_clock_anomaly_emitted_ = false;

    if (input.source_frame_id == 0 || input.source_observation_id == 0 ||
        input.persistent_target_id == 0 || input.target_acquisition_id == 0) {
        record_observation_window(
            input.source_present_steady_ns, input.controller_tick_id);
        const auto reason = Gate25Reason::NoTargetIdentity;
        count_reason(reason);
        queue_anomaly(input, reason);
        reset_pair_state();
        last_effect_.reason = reason;
        return;
    }
    if (!input.game_profile_identity_available) {
        aggregate_.cohort_profile_incomplete = true;
    }

    if (previous_.valid) {
        const auto& prior = previous_.value;
        if (input.source_frame_id == prior.source_frame_id ||
            input.source_observation_id == prior.source_observation_id) {
            const auto reason = Gate25Reason::DuplicateObservation;
            last_effect_.reason = reason;
            count_reason(reason);
            queue_anomaly(input, reason);
            return;
        }
        if (input.source_present_steady_ns == prior.source_present_steady_ns) {
            const auto reason = Gate25Reason::SamePresentEndpoint;
            last_effect_.reason = reason;
            count_reason(reason);
            queue_anomaly(input, reason);
            return;
        }
        if (input.source_present_steady_ns < prior.source_present_steady_ns) {
            const auto reason = Gate25Reason::BackwardPresent;
            last_effect_.reason = reason;
            count_reason(reason);
            queue_anomaly(input, reason);
            return;
        }
        if (input.source_frame_id < prior.source_frame_id) {
            const auto reason = Gate25Reason::StaleObservation;
            last_effect_.reason = reason;
            count_reason(reason);
            queue_anomaly(input, reason);
            return;
        }
    }

    // Monotonic validation above must precede this subtraction. In
    // particular, a backward present endpoint must never underflow and look
    // like a five-second rollover.
    record_observation_window(
        input.source_present_steady_ns, input.controller_tick_id);
    if (input.response_delay_valid) {
        aggregate_.response_delay_source = input.response_delay_source;
    }

    if (input.viewport_source_frame_id != input.source_frame_id ||
        !input.fresh_observed || !input.strong_observation ||
        !input.stable_coordinates_valid ||
        !finite(input.target_anchor_screen_x) || !finite(input.target_anchor_screen_y) ||
        !finite(input.reliability) || input.reliability < 0.0f || input.reliability > 1.0f ||
        !finite(input.stable_body_width) || !finite(input.stable_body_height) ||
        input.stable_body_width <= 0.0f || input.stable_body_height <= 0.0f ||
        input.viewport_width <= 0 || input.viewport_height <= 0) {
        const auto reason = Gate25Reason::InvalidGeometry;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        // This source endpoint belongs to the diagnostic window, but never
        // becomes the compatible pair state.
        return;
    }

    if (!previous_.valid) {
        previous_.value = input;
        previous_.valid = true;
        return;
    }

    const auto& prior = previous_.value;

    Gate25Reason boundary = Gate25Reason::None;
    if (input.source_frame_id > prior.source_frame_id + 1ull ||
        input.accumulated_frames > 1u) {
        boundary = Gate25Reason::CaptureGap;
    } else if (input.source_present_qpc_frequency !=
                   prior.source_present_qpc_frequency ||
               input.source_present_calibration_id == 0 ||
               prior.source_present_calibration_id == 0 ||
               input.source_present_calibration_uncertainty_ns >
                   kMaxPresentCalibrationUncertaintyNs ||
               prior.source_present_calibration_uncertainty_ns >
                   kMaxPresentCalibrationUncertaintyNs ||
               (input.source_present_steady_ns > prior.source_present_steady_ns &&
                input.source_present_calibration_uncertainty_ns +
                    prior.source_present_calibration_uncertainty_ns >
                    (input.source_present_steady_ns -
                     prior.source_present_steady_ns) / 4ull)) {
        boundary = Gate25Reason::PresentCalibrationBoundary;
    } else if (!input.backend_known || !prior.backend_known ||
        input.backend_epoch == 0 || prior.backend_epoch == 0 ||
        input.backend_epoch != prior.backend_epoch ||
        !input.output_enabled || !prior.output_enabled) {
        boundary = Gate25Reason::BackendBoundary;
    } else if (input.viewport_sequence != prior.viewport_sequence ||
               input.viewport_source_frame_id != input.source_frame_id ||
               input.viewport_width != prior.viewport_width ||
               input.viewport_height != prior.viewport_height) {
        boundary = Gate25Reason::ViewportBoundary;
    } else if (input.selector_identity_protocol != prior.selector_identity_protocol ||
               (input.selector_identity_protocol &&
                input.selector_target_generation != prior.selector_target_generation)) {
        boundary = Gate25Reason::SelectorBoundary;
    } else if (input.persistent_target_id != prior.persistent_target_id) {
        boundary = Gate25Reason::IdentityBoundary;
    } else if (input.target_acquisition_id != prior.target_acquisition_id) {
        boundary = Gate25Reason::TargetAcquisitionBoundary;
    } else if (input.physical_ads_epoch != prior.physical_ads_epoch) {
        boundary = Gate25Reason::AdsBoundary;
    } else if (input.mode != prior.mode) {
        boundary = Gate25Reason::ModeBoundary;
    } else if (!input.response_delay_valid || input.response_delay_ns == 0) {
        boundary = Gate25Reason::MissingResponseDelay;
    } else if (input.response_delay_source != prior.response_delay_source ||
               !prior.response_delay_valid || prior.response_delay_ns == 0 ||
               input.response_delay_ns != prior.response_delay_ns) {
        boundary = Gate25Reason::ResponseDelayBoundary;
    }
    if (boundary != Gate25Reason::None) {
        count_reason(boundary);
        queue_anomaly(input, boundary);
        reset_pair_state();
        previous_.value = input;
        previous_.valid = true;
        last_effect_.reason = boundary;
        return;
    }

    if (!input.response_delay_valid || input.response_delay_ns == 0) {
        const auto reason = Gate25Reason::MissingResponseDelay;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        previous_.value = input;
        return;
    }
    if (prior.source_present_steady_ns < input.response_delay_ns ||
        input.source_present_steady_ns < input.response_delay_ns) {
        const auto reason = Gate25Reason::IncompleteDeliveryHistory;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        previous_.value = input;
        return;
    }

    ++aggregate_.compatible_pairs;
    // The observed present interval is caused by deliveries in the delayed
    // interval [present-delay], not by deliveries in the present interval.
    const std::uint64_t begin_ns = prior.source_present_steady_ns - input.response_delay_ns;
    const std::uint64_t end_ns = input.source_present_steady_ns - input.response_delay_ns;
    const std::uint64_t interval_ns = end_ns - begin_ns;
    if (delivery_source == nullptr || delivery_source->integrate == nullptr) {
        const auto reason = Gate25Reason::MissingDeliveryHistory;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        previous_.value = input;
        return;
    }
    const auto integral = delivery_source->integrate(
        delivery_source->context, begin_ns, end_ns);
    if (!integral.complete) {
        const auto reason = Gate25Reason::IncompleteDeliveryHistory;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        previous_.value = input;
        return;
    }
    // A successful report is not enough to make an interval clean when the
    // backend was disabled anywhere in its half-open history.  This is a
    // backend-boundary diagnostic, distinct from firing/recoil/saturation
    // cohort labels; a later enabled report must not make the interval look
    // physically continuous.
    if (integral.output_disabled) {
        const auto reason = Gate25Reason::BackendBoundary;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason);
        previous_.value = input;
        return;
    }

    const float seconds = static_cast<float>(interval_ns) / 1'000'000'000.0f;
    if (aggregate_.delivery_first_seq == 0 || integral.first_seq < aggregate_.delivery_first_seq)
        aggregate_.delivery_first_seq = integral.first_seq;
    aggregate_.delivery_last_seq = std::max(aggregate_.delivery_last_seq, integral.last_seq);
    const float average_final_x = seconds > 0.0f
        ? integral.final_right_stick_seconds.x / seconds : 0.0f;
    const float average_final_y = seconds > 0.0f
        ? integral.final_right_stick_seconds.y / seconds : 0.0f;
    const float average_left_x = seconds > 0.0f
        ? integral.physical_left_stick_seconds.x / seconds : 0.0f;
    const float average_left_y = seconds > 0.0f
        ? integral.physical_left_stick_seconds.y / seconds : 0.0f;
    const float output_magnitude = magnitude(average_final_x, average_final_y);
    const float average_final_exposure = seconds > 0.0f
        ? integral.final_right_stick_abs_seconds / seconds : 0.0f;
    const std::size_t command_bin = command_magnitude_bin(output_magnitude);
    const std::size_t axis_bin = output_axis_bin(average_final_x, average_final_y);
    auto& cohort = aggregate_.cohort_grid[
        cohort_index(input.mode, axis_bin, command_bin)];
    ++cohort.pair_count;
    const auto mark_cohort_invalid = [&cohort]() noexcept {
        ++cohort.invalid_count;
    };
    last_effect_.controller_tick_id = input.controller_tick_id;
    last_effect_.delivery_first_seq = integral.first_seq;
    last_effect_.delivery_last_seq = integral.last_seq;
    last_effect_.response_delay_source = input.response_delay_source;
    last_effect_.interval_ms = static_cast<float>(interval_ns) / 1'000'000.0f;
    last_effect_.delivered_final_exposure_seconds =
        integral.final_right_stick_abs_seconds;
    const bool exogenous = magnitude(average_left_x, average_left_y) >
        kExogenousAverageLeftStick || integral.firing || integral.recoil_active;
    const float delta_x = input.target_anchor_screen_x - prior.target_anchor_screen_x;
    const float delta_y = input.target_anchor_screen_y - prior.target_anchor_screen_y;
    last_effect_.target_delta_x = delta_x;
    last_effect_.target_delta_y = delta_y;
    // Exogenous rejection precedes baseline checks so it is not mislabeled as
    // missing baseline. Saturation is a separate cohort label and is not an
    // exogenous source.
    if (exogenous) {
        mark_cohort_invalid();
        last_effect_.reason = Gate25Reason::ExogenousMotion;
        count_reason(last_effect_.reason);
        queue_anomaly(input, last_effect_.reason, delta_x, delta_y);
        previous_.value = input;
        return;
    }
    const bool saturated = integral.saturated;
    if (saturated) ++aggregate_.saturation_rows;

    const bool tracker_motion_contaminated =
        prior.lifecycle != static_cast<std::uint8_t>(pipeline_contract::TargetLifecycle::Observed) ||
        input.lifecycle != static_cast<std::uint8_t>(pipeline_contract::TargetLifecycle::Observed) ||
        prior.motion == static_cast<std::uint8_t>(pipeline_contract::TargetMotion::Strafe) ||
        prior.motion == static_cast<std::uint8_t>(pipeline_contract::TargetMotion::Jump) ||
        prior.motion == static_cast<std::uint8_t>(pipeline_contract::TargetMotion::Fall) ||
        input.motion == static_cast<std::uint8_t>(pipeline_contract::TargetMotion::Strafe) ||
        input.motion == static_cast<std::uint8_t>(pipeline_contract::TargetMotion::Jump) ||
        input.motion == static_cast<std::uint8_t>(pipeline_contract::TargetMotion::Fall);
    const bool target_motion_contaminated =
        tracker_motion_contaminated || prior.reused_or_projected || input.reused_or_projected ||
        (prior.stable_body_width > 0.0f && input.stable_body_width > 0.0f &&
         (std::fabs(input.stable_body_width / prior.stable_body_width - 1.0f) > 0.25f)) ||
        (prior.stable_body_height > 0.0f && input.stable_body_height > 0.0f &&
         (std::fabs(input.stable_body_height / prior.stable_body_height - 1.0f) > 0.25f)) ||
        (prior.stable_body_width > 0.0f && prior.stable_body_height > 0.0f &&
         input.stable_body_width > 0.0f && input.stable_body_height > 0.0f &&
         std::fabs((input.stable_body_width / input.stable_body_height) /
                   (prior.stable_body_width / prior.stable_body_height) - 1.0f) > 0.20f) ||
        (prior.has_motion_anchor && input.has_motion_anchor &&
         (prior.motion_anchor_score < 0.25f || input.motion_anchor_score < 0.25f));
    if (target_motion_contaminated) {
        mark_cohort_invalid();
        const auto reason = Gate25Reason::TargetMotionContamination;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason, delta_x, delta_y);
        previous_.value = input;
        return;
    }
    if (average_final_exposure > kNeutralAverageStick) {
        cohort.attempted_delivered_final_sum_x +=
            integral.final_right_stick_seconds.x;
        cohort.attempted_delivered_final_sum_y +=
            integral.final_right_stick_seconds.y;
        cohort.attempted_delivered_final_abs_sum +=
            integral.final_right_stick_abs_seconds;
        ++cohort.attempted_delivered_count;
    }

    if (integral.final_right_reversal) {
        mark_cohort_invalid();
        const auto reason = Gate25Reason::CancellationOrReversal;
        last_effect_.reason = reason;
        count_reason(reason);
        previous_.value = input;
        return;
    }

    const bool neutral = average_final_exposure <=
        kNeutralAverageStick && !saturated;
    if (neutral) {
        mark_cohort_invalid();
        add_neutral_baseline(delta_x, delta_y);
        previous_.value = input;
        return;
    }
    if (baseline_count_ == 0) {
        mark_cohort_invalid();
        const auto reason = Gate25Reason::MissingNeutralBaseline;
        last_effect_.reason = reason;
        count_reason(reason);
        queue_anomaly(input, reason, delta_x, delta_y);
        previous_.value = input;
        return;
    }

    const float baseline_x = aggregate_.baseline_median_x;
    const float baseline_y = aggregate_.baseline_median_y;
    const float observed_x = -(delta_x - baseline_x);
    const float observed_y = -(delta_y - baseline_y);
    const float noise = std::max(
        0.10f,
        1.4826f * std::max(aggregate_.baseline_mad_x, aggregate_.baseline_mad_y));
    const float snr = magnitude(observed_x, observed_y) / noise;
    last_effect_.previous_frame_id = prior.source_frame_id;
    last_effect_.current_frame_id = input.source_frame_id;
    last_effect_.previous_present_steady_ns = prior.source_present_steady_ns;
    last_effect_.current_present_steady_ns = input.source_present_steady_ns;
    last_effect_.neutral_baseline_x = baseline_x;
    last_effect_.neutral_baseline_y = baseline_y;
    last_effect_.observed_camera_work_x = observed_x;
    last_effect_.observed_camera_work_y = observed_y;
    last_effect_.delivered_final_work_x = integral.final_right_stick_seconds.x;
    last_effect_.delivered_final_work_y = integral.final_right_stick_seconds.y;
    last_effect_.snr = snr;
    last_effect_.saturated = saturated;
    if (magnitude(observed_x, observed_y) < kMinimumEffectPx || snr < kMinimumEffectSnr) {
        mark_cohort_invalid();
        ++cohort.below_noise_count;
        last_effect_.reason = Gate25Reason::BelowNoiseFloor;
        count_reason(last_effect_.reason);
        queue_anomaly(input, last_effect_.reason, delta_x, delta_y, observed_x, observed_y, snr);
        previous_.value = input;
        return;
    }

    last_effect_.valid = true;
    last_effect_.reason = Gate25Reason::None;
    ++cohort.valid_count;
    ++cohort.snr_pass_count;
    cohort.delivered_final_sum_x += integral.final_right_stick_seconds.x;
    cohort.delivered_final_sum_y += integral.final_right_stick_seconds.y;
    const float delivered_x = integral.final_right_stick_seconds.x;
    const float delivered_y = integral.final_right_stick_seconds.y;
    const float delivered_abs = magnitude(delivered_x, delivered_y);
    cohort.delivered_final_abs_sum += delivered_abs;
    cohort.delivered_final_energy_sum +=
        delivered_x * delivered_x + delivered_y * delivered_y;
    cohort.observed_camera_sum_x += observed_x;
    cohort.observed_camera_sum_y += observed_y;
    const float observed_abs = magnitude(observed_x, observed_y);
    cohort.observed_camera_abs_sum += observed_abs;
    cohort.observed_camera_energy_sum += observed_x * observed_x + observed_y * observed_y;
    cohort.delivered_observed_dot_sum +=
        delivered_x * observed_x + delivered_y * observed_y;
    cohort.delivered_observed_cross_sum +=
        delivered_x * observed_y - delivered_y * observed_x;
    constexpr float kComponentExposureThreshold = 1.0e-5f;
    constexpr float kComponentNoiseThreshold = 0.10f;
    if (std::fabs(delivered_x) > kComponentExposureThreshold &&
        std::fabs(observed_x) > kComponentNoiseThreshold) {
        if ((delivered_x > 0.0f) == (observed_x > 0.0f))
            ++cohort.component_sign_agree_count;
        else
            ++cohort.component_sign_disagree_count;
    }
    if (std::fabs(delivered_y) > kComponentExposureThreshold &&
        std::fabs(observed_y) > kComponentNoiseThreshold) {
        if ((delivered_y > 0.0f) == (observed_y > 0.0f))
            ++cohort.component_sign_agree_count;
        else
            ++cohort.component_sign_disagree_count;
    }
    ++aggregate_.effect_valid;
    ++aggregate_.snr_pass;
    aggregate_.observed_sum_x += observed_x;
    aggregate_.observed_sum_y += observed_y;
    ++aggregate_.continuous_zoh_coverage_rows;
    ++aggregate_.observed_present_cadence_coverage_rows;
    const float interval_ms = last_effect_.interval_ms;
    if (std::fabs(interval_ms - (1000.0f / 180.0f)) <= 1.25f) ++aggregate_.fixed_180hz_coverage_rows;
    if (std::fabs(interval_ms - (1000.0f / 240.0f)) <= 1.00f) ++aggregate_.fixed_240hz_coverage_rows;

    if (output_magnitude <= 0.0001f) {
        ++aggregate_.output_axis_bins[3];
    } else if (std::fabs(average_final_x) >= 2.0f * std::fabs(average_final_y)) {
        ++aggregate_.output_axis_bins[0];
    } else if (std::fabs(average_final_y) >= 2.0f * std::fabs(average_final_x)) {
        ++aggregate_.output_axis_bins[1];
    } else {
        ++aggregate_.output_axis_bins[2];
    }
    // Passive output cohorts use the planned 1/2/3/5/10% bins. The values
    // are actual delivered-final magnitudes, not claims about raw human
    // stick precision.
    const std::size_t magnitude_bin = command_bin;
    ++aggregate_.output_magnitude_bins[magnitude_bin];

    const float manual_magnitude = magnitude(
        integral.manual_stick_seconds.x / seconds,
        integral.manual_stick_seconds.y / seconds);
    const float ai_magnitude = magnitude(
        integral.ai_stick_seconds.x / seconds,
        integral.ai_stick_seconds.y / seconds);
    if (manual_magnitude > 0.02f && ai_magnitude <= 0.02f) ++aggregate_.manual_only_rows;
    else if (ai_magnitude > 0.02f && manual_magnitude <= 0.02f) ++aggregate_.ai_only_rows;
    else if (manual_magnitude > 0.02f || ai_magnitude > 0.02f) ++aggregate_.mixed_rows;
    const bool has_ads_profile_provenance =
        input.game_profile_identity_available &&
        input.response_profile_provenance_valid &&
        input.ads_release_neutral_before_rise && input.ads_rising_edge;
    if (has_ads_profile_provenance && input.response_label_ms == 260)
        ++aggregate_.response_260ms_rows;
    if (has_ads_profile_provenance && input.response_label_ms == 400)
        ++aggregate_.response_400ms_rows;
    if (!input.response_model_available)
        count_reason(Gate25Reason::ResponseModelUnavailable);
    if ((input.response_label_ms == 260 || input.response_label_ms == 400) &&
        !has_ads_profile_provenance) {
        count_reason(Gate25Reason::ProfileIdentityUnavailable);
    }

    if (ledger_join_matches_accepted_pair(input, prior)) {
        last_effect_.ledger_comparison_valid = true;
        last_effect_.ledger_realized_x = input.ledger_realized_x;
        last_effect_.ledger_realized_y = input.ledger_realized_y;
        last_effect_.residual_x = observed_x - input.ledger_realized_x;
        last_effect_.residual_y = observed_y - input.ledger_realized_y;
        ++aggregate_.ledger_comparisons;
        const float residual = magnitude(last_effect_.residual_x, last_effect_.residual_y);
        cohort.ledger_residual_sum += residual;
        cohort.ledger_residual_max = std::max(cohort.ledger_residual_max, residual);
        ++cohort.ledger_residual_count;
        aggregate_.residual_abs_sum += residual;
        aggregate_.residual_abs_max = std::max(aggregate_.residual_abs_max, residual);
    } else {
        ++aggregate_.model_insufficient;
    }
    aggregate_.status = Gate25EvidenceStatus::EffectEvidenceAvailable;
    previous_.value = input;
}

bool Gate25LiveShadow::take_due_summary(Gate25AggregateSnapshot& out) noexcept {
    if (!has_pending_summary_) return false;
    out = pending_summary_;
    has_pending_summary_ = false;
    return true;
}

bool Gate25LiveShadow::flush_summary(Gate25AggregateSnapshot& out) noexcept {
    if (aggregate_.observations == 0) return false;
    out = aggregate_;
    out.anomaly_dropped = anomaly_dropped_;
    // The next observation chooses its own clock domain. Seeding this reset
    // with the previous endpoint would make a diagnostic/source transition
    // inherit a stale SourcePresentSteady label.
    reset_aggregate(0);
    anomaly_dropped_ = 0;
    return true;
}

bool Gate25LiveShadow::pop_anomaly(Gate25Anomaly& out) noexcept {
    if (anomaly_count_ == 0) return false;
    out = anomalies_[anomaly_head_];
    anomaly_head_ = (anomaly_head_ + 1) % kAnomalyCapacity;
    --anomaly_count_;
    return true;
}

}  // namespace runtime_app
