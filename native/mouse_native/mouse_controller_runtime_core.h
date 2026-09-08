#pragma once

#include "mouse_native/mouse_controller_facade.h"
#include "mouse_native/mouse_sensitivity_calibrator.h"
#include "mouse_native/mouse_cod_default_profile.h"

#include <cstdint>

namespace mouse_native {

enum class MouseRuntimeOutputKind : std::uint8_t {
    ControllerFinal = 0,
    CalibrationProbe = 1,
    CalibrationReturn = 2,
};

struct MouseRuntimeOutput {
    MouseSourceCounts counts{};
    MouseRuntimeOutputKind kind = MouseRuntimeOutputKind::ControllerFinal;
    bool requested = false;
};

struct MouseCalibrationRequestResult {
    MouseRuntimeOutput output{};
    MouseCalibrationFailure failure = MouseCalibrationFailure::None;
    MouseAimMode mode = MouseAimMode::Hipfire;
    bool started = false;
};

struct MouseControllerRuntimeTickInput {
    MouseSourceCounts source_counts{};
    double now_seconds = 0.0;
    std::uint64_t tick_id = 0;
    bool right_button_down = false;
    bool left_button_down = false;
};

struct MouseControllerRuntimeTickResult {
    MouseControllerTickResult controller{};
    MouseRuntimeOutput output{};
    MouseCalibrationFailure calibration_failure = MouseCalibrationFailure::None;
    std::uint64_t calibration_physical_abs_counts = 0;
    MouseAimMode mode = MouseAimMode::Hipfire;
    bool calibration_active = false;
    bool calibration_completed = false;
};

// Small composition owner for the already-existing pieces.  It selects the
// in-memory hipfire/ADS profile, gives calibration output precedence while a
// range-dummy probe is active, and otherwise forwards the facade's one final-T
// report unchanged.
class MouseControllerRuntimeCore {
public:
    explicit MouseControllerRuntimeCore(
        MouseControllerFacadeConfig controller_config = {},
        MouseSensitivityCalibratorConfig calibration_config = {},
        MouseCodDefaultConfig default_config = {});

    void submit_vision_snapshot(
        const controller_native::ControllerVisionSnapshot& snapshot);
    MouseCalibrationRequestResult begin_calibration(
        bool right_button_down,
        std::uint64_t now_ns) noexcept;
    MouseCalibrationUpdate acknowledge_calibration_output(
        bool delivered, std::uint64_t delivered_at_ns = 0) noexcept;
    MouseControllerRuntimeTickResult tick(
        const MouseControllerRuntimeTickInput& input);
    void cancel_calibration() noexcept;
    void reset();

    const MouseResponseProfile& profile(MouseAimMode mode) const noexcept;
    const MouseResponseProfile& effective_profile(MouseAimMode mode) const noexcept;
    void set_default_view_height(int height_px) noexcept;
    MouseCalibrationState calibration_state() const noexcept;
    const MouseControllerFacade& facade() const noexcept;

private:
    static MouseAimMode mode_from_button(bool right_button_down) noexcept;
    static std::uint64_t seconds_to_ns(double seconds) noexcept;
    static MouseRuntimeOutput calibration_output(
        const MouseCalibrationUpdate& update,
        MouseRuntimeOutputKind kind) noexcept;

    MouseControllerFacade facade_;
    MouseSensitivityCalibrator calibrator_;
    MouseCodDefaultConfig default_config_;
    MouseResponseProfile default_hipfire_{};
    MouseResponseProfile default_ads_{};
    std::uint64_t default_generation_ = (std::uint64_t{1} << 63);
    std::uint64_t calibration_physical_abs_counts_ = 0;
};

}  // namespace mouse_native
