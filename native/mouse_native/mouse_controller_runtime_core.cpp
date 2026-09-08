#include "mouse_native/mouse_controller_runtime_core.h"

#include <cmath>
#include <limits>

namespace mouse_native {
namespace {

constexpr std::uint64_t kCalibrationPhysicalJitterBudgetCounts = 4;
constexpr std::uint64_t kMaximumCalibrationOriginAgeNs = 250'000'000;

std::uint64_t absolute_count(std::int32_t value) noexcept {
    return value < 0
        ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(value))
        : static_cast<std::uint64_t>(value);
}

}  // namespace

MouseControllerRuntimeCore::MouseControllerRuntimeCore(
    MouseControllerFacadeConfig controller_config,
    MouseSensitivityCalibratorConfig calibration_config,
    MouseCodDefaultConfig default_config)
    : facade_(controller_config), calibrator_(calibration_config),
      default_config_(default_config) {
    default_hipfire_ = make_cod_default_profile(default_config_, MouseAimMode::Hipfire, default_generation_);
    default_ads_ = make_cod_default_profile(default_config_, MouseAimMode::Ads, default_generation_ + 1);
}

void MouseControllerRuntimeCore::submit_vision_snapshot(
    const controller_native::ControllerVisionSnapshot& snapshot) {
    facade_.submit_vision_snapshot(snapshot);
}

MouseCalibrationRequestResult MouseControllerRuntimeCore::begin_calibration(
    bool right_button_down,
    std::uint64_t now_ns) noexcept {
    MouseCalibrationRequestResult result{};
    result.mode = mode_from_button(right_button_down);

    MouseCalibrationObservation observation{};
    if (!facade_.last_calibration_observation(&observation)) {
        result.failure = MouseCalibrationFailure::InvalidObservation;
        return result;
    }

    if (now_ns == 0 || observation.observed_at_ns == 0 ||
        (now_ns >= observation.observed_at_ns &&
         now_ns - observation.observed_at_ns >
             kMaximumCalibrationOriginAgeNs)) {
        result.failure = MouseCalibrationFailure::InvalidObservation;
        return result;
    }

    const MouseCalibrationUpdate update =
        calibrator_.begin(result.mode, observation, now_ns);
    if (update.failed) {
        result.failure = update.failure;
        return result;
    }
    result.output = calibration_output(
        update, MouseRuntimeOutputKind::CalibrationProbe);
    result.started = result.output.requested;
    if (result.started) calibration_physical_abs_counts_ = 0;
    return result;
}

MouseCalibrationUpdate MouseControllerRuntimeCore::acknowledge_calibration_output(
    bool delivered, std::uint64_t delivered_at_ns) noexcept {
    return calibrator_.acknowledge_output(delivered, delivered_at_ns);
}

MouseControllerRuntimeTickResult MouseControllerRuntimeCore::tick(
    const MouseControllerRuntimeTickInput& input) {
    MouseControllerRuntimeTickResult result{};
    result.mode = mode_from_button(input.right_button_down);
    const std::uint64_t now_ns = seconds_to_ns(input.now_seconds);

    if (calibrator_.state() != MouseCalibrationState::Idle &&
        calibrator_.active_mode() != result.mode) {
        calibrator_.cancel();
        result.calibration_failure = MouseCalibrationFailure::AimModeChanged;
    }

    MouseCalibrationUpdate calibration_update = calibrator_.check_timeout(now_ns);
    if (calibration_update.failed) {
        result.calibration_failure = calibration_update.failure;
    }

    const bool calibration_was_active =
        calibrator_.state() != MouseCalibrationState::Idle;
    if (calibration_was_active) {
        const std::uint64_t sample_motion =
            absolute_count(input.source_counts.dx) +
            absolute_count(input.source_counts.dy);
        const std::uint64_t remaining =
            std::numeric_limits<std::uint64_t>::max() -
            calibration_physical_abs_counts_;
        calibration_physical_abs_counts_ = sample_motion > remaining
            ? std::numeric_limits<std::uint64_t>::max()
            : calibration_physical_abs_counts_ + sample_motion;
    }
    result.calibration_physical_abs_counts =
        calibration_physical_abs_counts_;
    MouseResponseProfile active_profile{};
    if (!calibration_was_active) {
        active_profile = effective_profile(result.mode);
    }

    MouseControllerTickInput facade_input{};
    facade_input.source_counts = input.source_counts;
    facade_input.response_profile = active_profile;
    facade_input.now_seconds = input.now_seconds;
    facade_input.tick_id = input.tick_id;
    facade_input.right_button_down = input.right_button_down;
    facade_input.left_button_down = input.left_button_down;
    result.controller = facade_.tick(facade_input);
    result.output.counts = {
        result.controller.actuation.dx,
        result.controller.actuation.dy};
    result.output.kind = MouseRuntimeOutputKind::ControllerFinal;
    result.output.requested = result.controller.actuation.valid;

    const MouseCalibrationState state = calibrator_.state();
    const bool awaiting_observation =
        state == MouseCalibrationState::AwaitingProbeObservation ||
        state == MouseCalibrationState::AwaitingReturnObservation;
    if (awaiting_observation) {
        MouseCalibrationObservation observation{};
        if (facade_.last_calibration_observation(&observation)) {
            const bool physical_mouse_moved =
                calibration_physical_abs_counts_ >
                kCalibrationPhysicalJitterBudgetCounts;
            calibration_update = calibrator_.observe(
                observation, physical_mouse_moved);
            if (calibration_update.output_requested) {
                calibration_physical_abs_counts_ = 0;
                result.output = calibration_output(
                    calibration_update,
                    MouseRuntimeOutputKind::CalibrationReturn);
                // Calibration owns the probe delta, not this tick's physical
                // packet. Preserve allowed sensor jitter in the same report.
                // observe() has already rejected motion above the four-count
                // budget, so these sums cannot overflow the report range.
                result.output.counts.dx += input.source_counts.dx;
                result.output.counts.dy += input.source_counts.dy;
            }
            if (calibration_update.failed) {
                calibration_physical_abs_counts_ = 0;
                result.calibration_failure = calibration_update.failure;
            }
            result.calibration_completed = calibration_update.completed;
            if (calibration_update.completed) {
                calibration_physical_abs_counts_ = 0;
            }
        }
    }

    result.calibration_active =
        calibrator_.state() != MouseCalibrationState::Idle;
    return result;
}

void MouseControllerRuntimeCore::cancel_calibration() noexcept {
    calibrator_.cancel();
    calibration_physical_abs_counts_ = 0;
}

void MouseControllerRuntimeCore::reset() {
    calibrator_.cancel();
    calibration_physical_abs_counts_ = 0;
    facade_.reset();
}

const MouseResponseProfile& MouseControllerRuntimeCore::profile(
    MouseAimMode mode) const noexcept {
    return calibrator_.profile(mode);
}

const MouseResponseProfile& MouseControllerRuntimeCore::effective_profile(
    MouseAimMode mode) const noexcept {
    const auto& measured = calibrator_.profile(mode);
    if (valid(measured)) return measured;
    return mode == MouseAimMode::Ads ? default_ads_ : default_hipfire_;
}

void MouseControllerRuntimeCore::set_default_view_height(int height_px) noexcept {
    if (height_px <= 0 || height_px == default_config_.view_height_px) return;
    default_config_.view_height_px = height_px;
    default_generation_ += 2;
    default_hipfire_ = make_cod_default_profile(default_config_, MouseAimMode::Hipfire, default_generation_);
    default_ads_ = make_cod_default_profile(default_config_, MouseAimMode::Ads, default_generation_ + 1);
}

MouseCalibrationState MouseControllerRuntimeCore::calibration_state() const noexcept {
    return calibrator_.state();
}

const MouseControllerFacade& MouseControllerRuntimeCore::facade() const noexcept {
    return facade_;
}

MouseAimMode MouseControllerRuntimeCore::mode_from_button(
    bool right_button_down) noexcept {
    return right_button_down ? MouseAimMode::Ads : MouseAimMode::Hipfire;
}

std::uint64_t MouseControllerRuntimeCore::seconds_to_ns(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0;
    const double ns = seconds * 1.0e9;
    if (ns >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(ns);
}

MouseRuntimeOutput MouseControllerRuntimeCore::calibration_output(
    const MouseCalibrationUpdate& update,
    MouseRuntimeOutputKind kind) noexcept {
    MouseRuntimeOutput output{};
    output.counts = update.output_counts;
    output.kind = kind;
    output.requested = update.output_requested;
    return output;
}

}  // namespace mouse_native
