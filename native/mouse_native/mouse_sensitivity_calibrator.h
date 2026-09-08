#pragma once

#include "mouse_native/mouse_control_types.h"

#include <array>
#include <cstdint>

namespace mouse_native {

enum class MouseCalibrationState : std::uint8_t {
    Idle = 0,
    ProbeOutputPending = 1,
    AwaitingProbeObservation = 2,
    ReturnOutputPending = 3,
    AwaitingReturnObservation = 4,
};

enum class MouseCalibrationFailure : std::uint8_t {
    None = 0,
    InvalidObservation = 1,
    TargetChanged = 2,
    PhysicalMouseMoved = 3,
    OutputFailed = 4,
    WrongResponseDirection = 5,
    ResponseOutOfRange = 6,
    ReturnMissed = 7,
    TimedOut = 8,
    InvalidState = 9,
    AimModeChanged = 10,
};

struct MouseCalibrationObservation {
    std::uint64_t frame_id = 0;
    std::uint64_t target_id = 0;
    std::uint64_t target_generation = 0;
    std::uint64_t observed_at_ns = 0;
    float person_x_px = 0.0f;
    float person_y_px = 0.0f;
    bool fresh = false;
};

struct MouseCalibrationUpdate {
    MouseSourceCounts output_counts{};
    MouseResponseProfile profile{};
    MouseCalibrationFailure failure = MouseCalibrationFailure::None;
    bool output_requested = false;
    bool completed = false;
    bool failed = false;
};

struct MouseSensitivityCalibratorConfig {
    std::int32_t horizontal_probe_counts = 40;
    float reference_response_px_per_u_second = 500.0f;
    float minimum_probe_displacement_px = 2.0f;
    float maximum_probe_displacement_px = 240.0f;
    float maximum_return_error_px = 3.0f;
    std::uint64_t timeout_ns = 750'000'000;
};

class MouseSensitivityCalibrator {
public:
    explicit MouseSensitivityCalibrator(
        MouseSensitivityCalibratorConfig config = {});

    MouseCalibrationUpdate begin(
        MouseAimMode mode,
        const MouseCalibrationObservation& observation,
        std::uint64_t request_started_ns = 0) noexcept;
    MouseCalibrationUpdate acknowledge_output(
        bool delivered, std::uint64_t delivered_at_ns = 0) noexcept;
    MouseCalibrationUpdate observe(
        const MouseCalibrationObservation& observation,
        bool physical_mouse_moved) noexcept;
    MouseCalibrationUpdate check_timeout(std::uint64_t now_ns) noexcept;
    void cancel() noexcept;

    MouseCalibrationState state() const noexcept;
    MouseAimMode active_mode() const noexcept;
    const MouseResponseProfile& profile(MouseAimMode mode) const noexcept;

private:
    static std::size_t mode_index(MouseAimMode mode) noexcept;
    static bool valid_observation(
        const MouseCalibrationObservation& observation) noexcept;
    bool same_target(
        const MouseCalibrationObservation& observation) const noexcept;
    MouseCalibrationUpdate fail(MouseCalibrationFailure failure) noexcept;
    void reset_active() noexcept;

    MouseSensitivityCalibratorConfig config_{};
    std::array<MouseResponseProfile, 2> profiles_{};
    MouseCalibrationState state_ = MouseCalibrationState::Idle;
    MouseAimMode active_mode_ = MouseAimMode::Hipfire;
    MouseCalibrationObservation origin_{};
    MouseCalibrationObservation probe_observation_{};
    MouseResponseProfile candidate_{};
    std::uint64_t request_started_ns_ = 0;
    std::uint64_t output_delivered_ns_ = 0;
    std::uint64_t next_profile_generation_ = 1;
};

}  // namespace mouse_native
