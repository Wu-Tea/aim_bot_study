#include "pending_control_motion.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace controller_native {

pipeline_contract::Vec2f delivered_camera_work_px(
    pipeline_contract::Vec2f control_displacement_px) noexcept {
    return {
        control_displacement_px.x,
        -control_displacement_px.y,
    };
}

pipeline_contract::Vec2f remaining_work_after_delivery(
    pipeline_contract::Vec2f error_px,
    pipeline_contract::Vec2f delivered_work_px) noexcept {
    return {
        error_px.x - delivered_work_px.x,
        error_px.y - delivered_work_px.y,
    };
}

const DeliveredPreRecoilSample& PendingControlMotion::at(
    std::size_t index) const noexcept {
    return samples_[(head_ + index) % kCapacity];
}

bool PendingControlMotion::observe(
    const DeliveredPreRecoilSample& sample) noexcept {
    const bool valid = std::isfinite(sample.delivered_at_seconds) &&
        sample.delivered_at_seconds > 0.0 &&
        pipeline_contract::finite(sample.pre_recoil_stick) &&
        sample.target_id != 0 && sample.delivered && sample.output_enabled;
    if (!valid) {
        reset();
        return false;
    }
    if (size_ != 0 &&
        sample.delivered_at_seconds <= at(size_ - 1).delivered_at_seconds) {
        reset();
        return false;
    }
    if (size_ < kCapacity) {
        samples_[(head_ + size_) % kCapacity] = sample;
        ++size_;
    } else {
        samples_[head_] = sample;
        head_ = (head_ + 1) % kCapacity;
    }
    return true;
}

PendingControlMotionEstimate PendingControlMotion::estimate(
    double now_seconds,
    float observation_age_ms,
    float response_scale_px_per_stick_second,
    std::uint64_t target_id) const noexcept {
    PendingControlMotionEstimate result;
    if (size_ == 0 || target_id == 0 || !std::isfinite(now_seconds) ||
        !std::isfinite(observation_age_ms) ||
        !std::isfinite(response_scale_px_per_stick_second) ||
        observation_age_ms <= 0.0f ||
        response_scale_px_per_stick_second <= 0.0f) {
        return result;
    }
    const double begin_seconds =
        now_seconds - static_cast<double>(observation_age_ms) / 1000.0;
    return estimate_between(
        begin_seconds,
        now_seconds,
        response_scale_px_per_stick_second,
        target_id);
}

PendingControlMotionEstimate PendingControlMotion::estimate_between(
    double begin_seconds,
    double end_seconds,
    float response_scale_px_per_stick_second,
    std::uint64_t target_id) const noexcept {
    PendingControlMotionEstimate result;
    constexpr double kMaximumAccountingWindowSeconds = 0.100;
    if (size_ == 0 || target_id == 0 ||
        !std::isfinite(begin_seconds) || !std::isfinite(end_seconds) ||
        !std::isfinite(response_scale_px_per_stick_second) ||
        end_seconds <= begin_seconds ||
        end_seconds - begin_seconds > kMaximumAccountingWindowSeconds ||
        response_scale_px_per_stick_second <= 0.0f) {
        return result;
    }
    constexpr double kMaximumInitialZeroHoldSeconds = 0.002;
    if (begin_seconds <
            at(0).delivered_at_seconds - kMaximumInitialZeroHoldSeconds ||
        end_seconds < at(size_ - 1).delivered_at_seconds) {
        return result;
    }

    pipeline_contract::Vec2f integral{};
    std::size_t held_index = 0;
    while (held_index + 1 < size_ &&
           at(held_index + 1).delivered_at_seconds <= begin_seconds) {
        ++held_index;
    }
    // Output delivery occurs just after the controller decision. At a new
    // capture there may be a sub-tick gap before the first delivered sample;
    // that interval represents zero held output, not missing history.
    double cursor = std::max(begin_seconds, at(held_index).delivered_at_seconds);
    while (held_index < size_ && cursor < end_seconds) {
        const auto& held = at(held_index);
        if (held.target_id != target_id) return result;
        const double next_time = held_index + 1 < size_
            ? at(held_index + 1).delivered_at_seconds
            : end_seconds;
        const double segment_end = std::min(end_seconds, next_time);
        if (segment_end > cursor) {
            const float dt = static_cast<float>(segment_end - cursor);
            integral.x += held.pre_recoil_stick.x * dt;
            integral.y += held.pre_recoil_stick.y * dt;
        }
        cursor = segment_end;
        ++held_index;
    }
    if (cursor + 1.0e-9 < end_seconds) return result;

    const pipeline_contract::Vec2f displacement{
        integral.x * response_scale_px_per_stick_second,
        integral.y * response_scale_px_per_stick_second,
    };
    result.camera_displacement_px.fill(displacement);
    result.valid = pipeline_contract::finite(displacement);
    return result;
}

void PendingControlMotion::reset() noexcept {
    head_ = 0;
    size_ = 0;
}

const DeliveredFinalCommandSample& CausalMotionLedger::at(
    std::size_t index) const noexcept {
    return samples_[(head_ + index) % kCapacity];
}

void CausalMotionLedger::clear_samples() noexcept {
    head_ = 0;
    size_ = 0;
    last_delivery_seconds_ = 0.0;
    history_coverage_start_seconds_ = 0.0;
    cumulative_area_.fill({});
    confidence_block_min_.fill(std::numeric_limits<float>::infinity());
    confidence_block_invalid_count_.fill(0);
}

bool CausalMotionLedger::physical_index_is_active(
    std::size_t physical_index) const noexcept {
    if (size_ == 0) return false;
    if (size_ == kCapacity) return true;
    const std::size_t end = (head_ + size_) % kCapacity;
    if (head_ + size_ <= kCapacity) {
        return physical_index >= head_ && physical_index < head_ + size_;
    }
    return physical_index >= head_ || physical_index < end;
}

void CausalMotionLedger::rebuild_confidence_block(
    std::size_t physical_index) noexcept {
    const std::size_t block = physical_index / kConfidenceBlockSize;
    float minimum = std::numeric_limits<float>::infinity();
    std::uint16_t invalid_count = 0;
    const std::size_t first = block * kConfidenceBlockSize;
    const std::size_t last = std::min(
        first + kConfidenceBlockSize, kCapacity);
    for (std::size_t index = first; index < last; ++index) {
        if (!physical_index_is_active(index)) continue;
        const auto& sample = samples_[index];
        if (!sample.response_model_valid ||
            !std::isfinite(sample.response_confidence) ||
            sample.response_confidence < 0.0f ||
            sample.response_confidence > 1.0f) {
            ++invalid_count;
            continue;
        }
        minimum = std::min(minimum, sample.response_confidence);
    }
    confidence_block_min_[block] = minimum;
    confidence_block_invalid_count_[block] = invalid_count;
}

std::size_t CausalMotionLedger::upper_bound_index(
    double seconds) const noexcept {
    std::size_t first = 0;
    std::size_t last = size_;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (at(middle).delivered_at_seconds <= seconds) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return first;
}

std::size_t CausalMotionLedger::lower_bound_index(
    double seconds) const noexcept {
    std::size_t first = 0;
    std::size_t last = size_;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (at(middle).delivered_at_seconds < seconds) {
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return first;
}

bool CausalMotionLedger::primitive_at(
    double seconds,
    pipeline_contract::Vec2f* area) const noexcept {
    if (area == nullptr || size_ == 0 || !std::isfinite(seconds)) {
        return false;
    }
    const auto& oldest = at(0);
    if (seconds < oldest.delivered_at_seconds) return false;
    const std::size_t upper = upper_bound_index(seconds);
    if (upper == 0) return false;
    const std::size_t logical_index = upper - 1;
    const std::size_t physical_index =
        (head_ + logical_index) % kCapacity;
    const auto& held = samples_[physical_index];
    const double dt = seconds - held.delivered_at_seconds;
    *area = cumulative_area_[physical_index];
    area->x += held.camera_velocity_px_per_second.x *
        static_cast<float>(dt);
    area->y += held.camera_velocity_px_per_second.y *
        static_cast<float>(dt);
    return pipeline_contract::finite(*area);
}

void CausalMotionLedger::include_confidence_range(
    std::size_t first_physical,
    std::size_t last_physical,
    float* minimum_confidence,
    bool* confidence_valid,
    bool* invalid_response_model) const noexcept {
    if (minimum_confidence == nullptr || confidence_valid == nullptr ||
        invalid_response_model == nullptr || first_physical > last_physical) {
        return;
    }
    const auto include_sample = [&](std::size_t index) {
        const auto& sample = samples_[index];
        if (!sample.response_model_valid ||
            !std::isfinite(sample.response_confidence) ||
            sample.response_confidence < 0.0f ||
            sample.response_confidence > 1.0f) {
            *invalid_response_model = true;
            return;
        }
        *minimum_confidence = std::min(
            *minimum_confidence, sample.response_confidence);
        *confidence_valid = true;
    };
    std::size_t first = first_physical;
    const std::size_t last = last_physical;
    while (first <= last && first % kConfidenceBlockSize != 0) {
        include_sample(first++);
    }
    while (first + kConfidenceBlockSize - 1 <= last) {
        const std::size_t block = first / kConfidenceBlockSize;
        if (confidence_block_invalid_count_[block] != 0) {
            *invalid_response_model = true;
        }
        if (std::isfinite(confidence_block_min_[block])) {
            *minimum_confidence = std::min(
                *minimum_confidence, confidence_block_min_[block]);
            *confidence_valid = true;
        }
        first += kConfidenceBlockSize;
    }
    while (first <= last) include_sample(first++);
}

void CausalMotionLedger::include_confidence_logical_range(
    std::size_t first_logical,
    std::size_t last_logical,
    float* minimum_confidence,
    bool* confidence_valid,
    bool* invalid_response_model) const noexcept {
    if (size_ == 0 || first_logical > last_logical ||
        last_logical >= size_) {
        return;
    }
    const std::size_t first_physical =
        (head_ + first_logical) % kCapacity;
    const std::size_t last_physical =
        (head_ + last_logical) % kCapacity;
    if (first_physical <= last_physical) {
        include_confidence_range(
            first_physical, last_physical, minimum_confidence,
            confidence_valid, invalid_response_model);
        return;
    }
    include_confidence_range(
        first_physical, kCapacity - 1, minimum_confidence,
        confidence_valid, invalid_response_model);
    include_confidence_range(
        0, last_physical, minimum_confidence, confidence_valid,
        invalid_response_model);
}

void CausalMotionLedger::invalidate(
    CausalMotionLedgerStatus reason,
    std::uint64_t physical_actuator_epoch) noexcept {
    clear_samples();
    if (physical_actuator_epoch != 0 &&
        (physical_actuator_epoch_ == 0 ||
         physical_actuator_epoch == physical_actuator_epoch_)) {
        physical_actuator_epoch_ = physical_actuator_epoch;
    }
    invalidation_status_ = reason;
}

bool CausalMotionLedger::observe(
    const DeliveredFinalCommandSample& sample) noexcept {
    const bool valid = std::isfinite(sample.delivered_at_seconds) &&
        sample.delivered_at_seconds > 0.0 &&
        pipeline_contract::finite(sample.final_stick) &&
        pipeline_contract::finite(sample.camera_velocity_px_per_second) &&
        sample.physical_actuator_epoch != 0;
    if (!sample.delivered || !sample.output_enabled) {
        invalidate(
            CausalMotionLedgerStatus::BackendStateUnknown,
            sample.physical_actuator_epoch);
        return false;
    }
    if (!valid) {
        invalidate(
            CausalMotionLedgerStatus::InvalidSample,
            sample.physical_actuator_epoch);
        return false;
    }
    const bool history_requires_new_epoch =
        invalidation_status_ == CausalMotionLedgerStatus::BackendStateUnknown ||
        invalidation_status_ == CausalMotionLedgerStatus::InvalidSample ||
        invalidation_status_ == CausalMotionLedgerStatus::NonMonotonicClock;
    if (history_requires_new_epoch && physical_actuator_epoch_ != 0 &&
        sample.physical_actuator_epoch == physical_actuator_epoch_) {
        // A failed/disconnected backend must be explicitly re-identified by
        // the caller before a successful report can re-establish authority.
        // The same rule applies to lost/invalid clock history: old physical
        // work did not disappear when bookkeeping became unknowable.
        return false;
    }
    if (physical_actuator_epoch_ != 0 &&
        sample.physical_actuator_epoch != physical_actuator_epoch_) {
        // A physical device epoch is the only lifecycle boundary that can
        // discard actuator history.  Target/ADS metadata never gets here.
        clear_samples();
        invalidation_status_ = CausalMotionLedgerStatus::DeviceEpochChanged;
        physical_actuator_epoch_ = sample.physical_actuator_epoch;
    } else if (physical_actuator_epoch_ == 0) {
        physical_actuator_epoch_ = sample.physical_actuator_epoch;
    }
    if (last_delivery_seconds_ != 0.0 &&
        sample.delivered_at_seconds <= last_delivery_seconds_) {
        invalidate(
            CausalMotionLedgerStatus::NonMonotonicClock,
            sample.physical_actuator_epoch);
        return false;
    }
    const std::size_t write_index = size_ < kCapacity
        ? (head_ + size_) % kCapacity : head_;
    if (size_ != 0) {
        const std::size_t previous_index =
            (head_ + size_ - 1) % kCapacity;
        const double dt = sample.delivered_at_seconds -
            samples_[previous_index].delivered_at_seconds;
        cumulative_area_[write_index] = cumulative_area_[previous_index];
        cumulative_area_[write_index].x +=
            samples_[previous_index].camera_velocity_px_per_second.x *
            static_cast<float>(dt);
        cumulative_area_[write_index].y +=
            samples_[previous_index].camera_velocity_px_per_second.y *
            static_cast<float>(dt);
    } else {
        cumulative_area_[write_index] = {};
    }
    samples_[write_index] = sample;
    if (size_ < kCapacity) {
        ++size_;
    } else {
        head_ = (head_ + 1) % kCapacity;
    }
    rebuild_confidence_block(write_index);
    if (size_ == 1) {
        // This is the first known actuator state for the current physical
        // epoch.  It is deliberately not extended backwards: a non-neutral
        // command may already have been held before bookkeeping began.
        history_coverage_start_seconds_ = sample.delivered_at_seconds;
    }
    last_delivery_seconds_ = sample.delivered_at_seconds;
    return true;
}

CausalMotionLedger::IntegralResult CausalMotionLedger::integrate(
    double begin_seconds,
    double end_seconds) const noexcept {
    IntegralResult result;
    if (!std::isfinite(begin_seconds) || !std::isfinite(end_seconds) ||
        end_seconds < begin_seconds) {
        result.status = CausalMotionLedgerStatus::InvalidRequest;
        return result;
    }
    if (size_ == 0) {
        result.status = invalidation_status_ == CausalMotionLedgerStatus::Empty
            ? CausalMotionLedgerStatus::Empty : invalidation_status_;
        return result;
    }
    if (begin_seconds == end_seconds) {
        result.status = CausalMotionLedgerStatus::Valid;
        result.valid = true;
        return result;
    }

    const double oldest_seconds = at(0).delivered_at_seconds;
    if (begin_seconds < history_coverage_start_seconds_ ||
        begin_seconds < oldest_seconds) {
        // Time before the first known state of this physical epoch is
        // unknown.  A successful neutral report can establish that state at
        // its own timestamp, but no target/lifecycle metadata may create a
        // synthetic zero hold before it.
        result.status = CausalMotionLedgerStatus::IncompleteHistory;
        return result;
    }
    if (end_seconds <= oldest_seconds) {
        // A non-empty interval ending before the first known state cannot be
        // integrated, even when the ring is not full.
        result.status = CausalMotionLedgerStatus::IncompleteHistory;
        return result;
    }

    pipeline_contract::Vec2f begin_area{};
    pipeline_contract::Vec2f end_area{};
    if (!primitive_at(begin_seconds, &begin_area) ||
        !primitive_at(end_seconds, &end_area)) {
        result.status = CausalMotionLedgerStatus::IncompleteHistory;
        return result;
    }
    result.displacement_px = {
        end_area.x - begin_area.x,
        end_area.y - begin_area.y,
    };
    const std::size_t upper_begin = upper_bound_index(begin_seconds);
    const std::size_t first_logical = upper_begin == 0
        ? 0 : upper_begin - 1;
    const std::size_t lower_end = lower_bound_index(end_seconds);
    const std::size_t last_logical = lower_end == 0
        ? 0 : lower_end - 1;
    float minimum_response_confidence = 1.0f;
    bool response_confidence_valid = false;
    bool invalid_response_model = false;
    include_confidence_logical_range(
        first_logical, last_logical, &minimum_response_confidence,
        &response_confidence_valid, &invalid_response_model);
    if (invalid_response_model) {
        result.status = CausalMotionLedgerStatus::InvalidResponseModel;
        return result;
    }
    result.response_confidence = response_confidence_valid
        ? minimum_response_confidence : 0.0f;
    result.response_confidence_valid = response_confidence_valid;
    if (!pipeline_contract::finite(result.displacement_px)) {
        result = {};
        result.status = CausalMotionLedgerStatus::InvalidSample;
        return result;
    }
    result.status = CausalMotionLedgerStatus::Valid;
    result.valid = true;
    return result;
}

CausalMotionPhaseEstimate CausalMotionLedger::estimate(
    const CausalMotionPhaseRequest& request) const noexcept {
    CausalMotionPhaseEstimate result;
    result.physical_actuator_epoch = physical_actuator_epoch_;
    result.previous_source_frame_id = request.previous_source_frame_id;
    result.previous_source_observation_id =
        request.previous_source_observation_id;
    result.previous_present_steady_ns = request.previous_present_steady_ns;
    result.previous_present_calibration_id =
        request.previous_present_calibration_id;
    result.previous_present_qpc_frequency =
        request.previous_present_qpc_frequency;
    result.previous_target_id = request.previous_target_id;
    result.previous_ads_epoch = request.previous_ads_epoch;
    result.source_frame_id = request.source_frame_id;
    result.source_observation_id = request.source_observation_id;
    result.current_target_id = request.current_target_id;
    result.current_ads_epoch = request.current_ads_epoch;
    result.current_present_steady_ns = request.current_present_steady_ns;
    result.present_calibration_id = request.present_calibration_id;
    result.present_qpc_frequency = request.present_qpc_frequency;
    result.present_time_valid = request.present_time_valid;
    if (size_ == 0) {
        result.status = invalidation_status_ == CausalMotionLedgerStatus::Empty
            ? CausalMotionLedgerStatus::Empty : invalidation_status_;
        return result;
    }
    if (!std::isfinite(request.previous_capture_seconds) ||
        !std::isfinite(request.current_capture_seconds) ||
        !std::isfinite(request.decision_seconds) ||
        !std::isfinite(request.response_delay_ms) ||
        !std::isfinite(request.memory_horizon_ms) ||
        request.current_capture_seconds <= 0.0 ||
        request.decision_seconds < request.current_capture_seconds ||
        request.previous_capture_seconds < 0.0 ||
        (request.previous_capture_seconds > 0.0 &&
         request.previous_capture_seconds >= request.current_capture_seconds) ||
        request.response_delay_ms < 0.0f ||
        request.memory_horizon_ms <= 0.0f ||
        request.memory_horizon_ms > 250.0f ||
        request.response_delay_ms >= request.memory_horizon_ms) {
        result.status = CausalMotionLedgerStatus::InvalidRequest;
        return result;
    }
    if (request.physical_actuator_epoch != 0 &&
        request.physical_actuator_epoch != physical_actuator_epoch_) {
        result.status = CausalMotionLedgerStatus::DeviceEpochChanged;
        return result;
    }

    const double delay_seconds =
        static_cast<double>(request.response_delay_ms) / 1000.0;
    const double horizon_seconds =
        static_cast<double>(request.memory_horizon_ms) / 1000.0;
    const double in_flight_begin =
        request.current_capture_seconds - delay_seconds;
    if (in_flight_begin < 0.0 ||
        request.decision_seconds - in_flight_begin > horizon_seconds) {
        result.status = CausalMotionLedgerStatus::HorizonExceeded;
        return result;
    }

    const auto in_flight = integrate(
        in_flight_begin,
        request.current_capture_seconds);
    if (!in_flight.valid) {
        result.status = in_flight.status;
        return result;
    }
    const auto scheduled = integrate(
        request.current_capture_seconds,
        request.decision_seconds);
    if (!scheduled.valid) {
        result.status = scheduled.status;
        return result;
    }
    result.in_flight_px = in_flight.displacement_px;
    result.scheduled_px = scheduled.displacement_px;
    result.pending_response_confidence = 1.0f;
    result.pending_response_confidence_valid = false;
    if (in_flight.response_confidence_valid) {
        result.pending_response_confidence = in_flight.response_confidence;
        result.pending_response_confidence_valid = true;
    }
    if (scheduled.response_confidence_valid) {
        result.pending_response_confidence =
            result.pending_response_confidence_valid
            ? std::min(result.pending_response_confidence,
                       scheduled.response_confidence)
            : scheduled.response_confidence;
        result.pending_response_confidence_valid = true;
    }
    result.pending_total_px = {
        result.in_flight_px.x + result.scheduled_px.x,
        result.in_flight_px.y + result.scheduled_px.y,
    };
    result.pending_valid = true;
    result.realized_status = CausalMotionLedgerStatus::Empty;

    if (request.previous_capture_seconds > 0.0 &&
        request.previous_capture_seconds < request.current_capture_seconds) {
        if (!request.capture_pair_compatible) {
            result.realized_status =
                CausalMotionLedgerStatus::CapturePairIncompatible;
        } else {
        const double realized_begin =
            request.previous_capture_seconds - delay_seconds;
        const double realized_end = in_flight_begin;
        if (realized_begin < 0.0 ||
            request.decision_seconds - realized_begin > horizon_seconds) {
                result.realized_status = CausalMotionLedgerStatus::HorizonExceeded;
            } else {
                const auto realized = integrate(realized_begin, realized_end);
                if (!realized.valid) {
                    result.realized_status = realized.status;
                } else {
                    result.realized_px = realized.displacement_px;
                    result.realized_response_confidence =
                        realized.response_confidence;
                    result.realized_response_confidence_valid =
                        realized.response_confidence_valid;
                    result.realized_valid = true;
                    result.realized_status = CausalMotionLedgerStatus::Valid;
                }
            }
        }
    }
    result.status = CausalMotionLedgerStatus::Valid;
    result.valid = result.pending_valid;
    return result;
}

void CausalMotionLedger::reset() noexcept {
    clear_samples();
    physical_actuator_epoch_ = 0;
    invalidation_status_ = CausalMotionLedgerStatus::Empty;
}

}  // namespace controller_native
