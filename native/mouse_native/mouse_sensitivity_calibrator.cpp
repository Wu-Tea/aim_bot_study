#include "mouse_native/mouse_sensitivity_calibrator.h"

#include <algorithm>
#include <cmath>

namespace mouse_native {

MouseSensitivityCalibrator::MouseSensitivityCalibrator(
    MouseSensitivityCalibratorConfig config)
    : config_(config) {
    if (config_.horizontal_probe_counts <= 0) {
        config_.horizontal_probe_counts = 40;
    }
    if (!std::isfinite(config_.reference_response_px_per_u_second) ||
        config_.reference_response_px_per_u_second <= 0.0f) {
        config_.reference_response_px_per_u_second = 500.0f;
    }
    if (!std::isfinite(config_.minimum_probe_displacement_px) ||
        config_.minimum_probe_displacement_px <= 0.0f) {
        config_.minimum_probe_displacement_px = 2.0f;
    }
    if (!std::isfinite(config_.maximum_probe_displacement_px) ||
        config_.maximum_probe_displacement_px <
            config_.minimum_probe_displacement_px) {
        config_.maximum_probe_displacement_px = 240.0f;
    }
    if (!std::isfinite(config_.maximum_return_error_px) ||
        config_.maximum_return_error_px < 0.0f) {
        config_.maximum_return_error_px = 3.0f;
    }
    if (config_.timeout_ns == 0) {
        config_.timeout_ns = 750'000'000;
    }
}

MouseCalibrationUpdate MouseSensitivityCalibrator::begin(
    MouseAimMode mode,
    const MouseCalibrationObservation& observation,
    std::uint64_t request_started_ns) noexcept {
    if (state_ != MouseCalibrationState::Idle) {
        return fail(MouseCalibrationFailure::InvalidState);
    }
    if (!valid_observation(observation)) {
        return fail(MouseCalibrationFailure::InvalidObservation);
    }

    active_mode_ = mode;
    origin_ = observation;
    request_started_ns_ = request_started_ns != 0
        ? request_started_ns
        : observation.observed_at_ns;
    candidate_ = {};
    state_ = MouseCalibrationState::ProbeOutputPending;

    MouseCalibrationUpdate update{};
    update.output_counts.dx = config_.horizontal_probe_counts;
    update.output_requested = true;
    return update;
}

MouseCalibrationUpdate MouseSensitivityCalibrator::acknowledge_output(
    bool delivered, std::uint64_t delivered_at_ns) noexcept {
    if (state_ != MouseCalibrationState::ProbeOutputPending &&
        state_ != MouseCalibrationState::ReturnOutputPending) {
        return fail(MouseCalibrationFailure::InvalidState);
    }
    if (!delivered) {
        return fail(MouseCalibrationFailure::OutputFailed);
    }

    const auto earliest = state_ == MouseCalibrationState::ProbeOutputPending
        ? request_started_ns_ : probe_observation_.observed_at_ns;
    output_delivered_ns_ = std::max(earliest, delivered_at_ns);
    state_ = state_ == MouseCalibrationState::ProbeOutputPending
        ? MouseCalibrationState::AwaitingProbeObservation
        : MouseCalibrationState::AwaitingReturnObservation;
    return {};
}

MouseCalibrationUpdate MouseSensitivityCalibrator::observe(
    const MouseCalibrationObservation& observation,
    bool physical_mouse_moved) noexcept {
    if (state_ != MouseCalibrationState::AwaitingProbeObservation &&
        state_ != MouseCalibrationState::AwaitingReturnObservation) {
        return fail(MouseCalibrationFailure::InvalidState);
    }
    if (physical_mouse_moved) {
        return fail(MouseCalibrationFailure::PhysicalMouseMoved);
    }
    if (!valid_observation(observation)) {
        return fail(MouseCalibrationFailure::InvalidObservation);
    }
    // Capture time, not delivery order/frame id, determines whether a frame
    // could contain the output. Vision is asynchronous with the controller.
    if (observation.observed_at_ns <= output_delivered_ns_) return {};
    if (!same_target(observation)) {
        return fail(MouseCalibrationFailure::TargetChanged);
    }
    if (observation.observed_at_ns > request_started_ns_ &&
        observation.observed_at_ns - request_started_ns_ >
            config_.timeout_ns) {
        return fail(MouseCalibrationFailure::TimedOut);
    }

    if (state_ == MouseCalibrationState::AwaitingProbeObservation) {
        if (observation.frame_id <= origin_.frame_id) {
            return {};
        }
        const float target_delta_x = observation.person_x_px - origin_.person_x_px;
        if (std::isfinite(target_delta_x) &&
            std::abs(target_delta_x) < config_.minimum_probe_displacement_px) {
            return {}; // The game has not rendered a measurable response yet.
        }
        // Positive mouse X turns the camera right, so a stationary person
        // moves left in capture coordinates. The actuator uses this polarity.
        if (!std::isfinite(target_delta_x) || target_delta_x >= 0.0f) {
            return fail(MouseCalibrationFailure::WrongResponseDirection);
        }
        const float displacement_px = std::abs(target_delta_x);
        if (displacement_px < config_.minimum_probe_displacement_px ||
            displacement_px > config_.maximum_probe_displacement_px) {
            return fail(MouseCalibrationFailure::ResponseOutOfRange);
        }

        const float px_per_count = displacement_px /
            static_cast<float>(config_.horizontal_probe_counts);
        const float counts_per_u_second =
            config_.reference_response_px_per_u_second / px_per_count;
        if (!std::isfinite(px_per_count) || px_per_count <= 0.0f ||
            !std::isfinite(counts_per_u_second) ||
            counts_per_u_second <= 0.0f) {
            return fail(MouseCalibrationFailure::ResponseOutOfRange);
        }

        candidate_.px_per_count_x = px_per_count;
        // Initial mouse support uses the game's one mouse-sensitivity scale
        // for both axes. A separate Y probe is added only after evidence that
        // the target game exposes an independent vertical scale.
        candidate_.px_per_count_y = px_per_count;
        candidate_.counts_per_u_second_x = counts_per_u_second;
        candidate_.counts_per_u_second_y = counts_per_u_second;
        candidate_.confidence = 1.0f;
        candidate_.generation = next_profile_generation_;
        candidate_.calibrated = true;
        probe_observation_ = observation;
        state_ = MouseCalibrationState::ReturnOutputPending;

        MouseCalibrationUpdate update{};
        update.output_counts.dx = -config_.horizontal_probe_counts;
        update.output_requested = true;
        return update;
    }

    if (observation.frame_id <= probe_observation_.frame_id) {
        return {};
    }
    const float return_error_x =
        std::abs(observation.person_x_px - origin_.person_x_px);
    if (!std::isfinite(return_error_x)) {
        return fail(MouseCalibrationFailure::ReturnMissed);
    }
    if (return_error_x > config_.maximum_return_error_px) return {};

    const float confidence = config_.maximum_return_error_px > 0.0f
        ? std::clamp(
              1.0f - 0.5f * return_error_x /
                  config_.maximum_return_error_px,
              0.5f,
              1.0f)
        : 1.0f;
    candidate_.confidence = confidence;
    profiles_[mode_index(active_mode_)] = candidate_;
    ++next_profile_generation_;
    if (next_profile_generation_ == 0) ++next_profile_generation_;

    MouseCalibrationUpdate update{};
    update.profile = candidate_;
    update.completed = true;
    reset_active();
    return update;
}

MouseCalibrationUpdate MouseSensitivityCalibrator::check_timeout(
    std::uint64_t now_ns) noexcept {
    if (state_ == MouseCalibrationState::Idle ||
        request_started_ns_ == 0 || now_ns < request_started_ns_) {
        return {};
    }
    if (now_ns - request_started_ns_ > config_.timeout_ns) {
        return fail(state_ == MouseCalibrationState::AwaitingReturnObservation
            ? MouseCalibrationFailure::ReturnMissed : MouseCalibrationFailure::TimedOut);
    }
    return {};
}

void MouseSensitivityCalibrator::cancel() noexcept {
    reset_active();
}

MouseCalibrationState MouseSensitivityCalibrator::state() const noexcept {
    return state_;
}

MouseAimMode MouseSensitivityCalibrator::active_mode() const noexcept {
    return active_mode_;
}

const MouseResponseProfile& MouseSensitivityCalibrator::profile(
    MouseAimMode mode) const noexcept {
    return profiles_[mode_index(mode)];
}

std::size_t MouseSensitivityCalibrator::mode_index(MouseAimMode mode) noexcept {
    return mode == MouseAimMode::Ads ? 1u : 0u;
}

bool MouseSensitivityCalibrator::valid_observation(
    const MouseCalibrationObservation& observation) noexcept {
    return observation.fresh && observation.frame_id != 0 &&
        observation.target_id != 0 && observation.target_generation != 0 &&
        observation.observed_at_ns != 0 &&
        std::isfinite(observation.person_x_px) &&
        std::isfinite(observation.person_y_px);
}

bool MouseSensitivityCalibrator::same_target(
    const MouseCalibrationObservation& observation) const noexcept {
    return observation.target_id == origin_.target_id &&
        observation.target_generation == origin_.target_generation;
}

MouseCalibrationUpdate MouseSensitivityCalibrator::fail(
    MouseCalibrationFailure failure) noexcept {
    MouseCalibrationUpdate update{};
    update.failure = failure;
    update.failed = true;
    reset_active();
    return update;
}

void MouseSensitivityCalibrator::reset_active() noexcept {
    state_ = MouseCalibrationState::Idle;
    origin_ = {};
    probe_observation_ = {};
    candidate_ = {};
    request_started_ns_ = 0;
    output_delivered_ns_ = 0;
}

}  // namespace mouse_native
