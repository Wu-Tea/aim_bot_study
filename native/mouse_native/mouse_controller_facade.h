#pragma once

#include "controller_native/controller_vision_snapshot.h"
#include "controller_native/native_gamepad_controller.h"
#include "controller_native/runtime_config.h"
#include "mouse_native/mouse_actuator_adapter.h"
#include "mouse_native/mouse_rate_adapter.h"
#include "mouse_native/mouse_sensitivity_calibrator.h"
#include "mouse_native/mouse_recoil.h"

#include <cstdint>

namespace mouse_native {

struct MouseControllerTuning {
    // 1/1/0 retains the original adapter. Production defaults come from [mouse].
    float speed = 1.0f;
    float breakaway = 1.0f;
    float bodylock_deadzone = 0.0f;
    float bodylock_range_px = 0.0f; // Zero inherits the shared controller setting.
    float bodylock_accel_ms = 0.0f;
    float bodylock_decel_ms = 0.0f;
    float bodylock_point_tolerance_px = 0.0f;
};

bool valid(MouseControllerTuning tuning) noexcept;

struct MouseControllerFacadeConfig {
    controller_native::GamepadRuntimeConfig controller{};
    MouseRateAdapterConfig rate_adapter{};
    MouseActuatorAdapterConfig actuator_adapter{};
    MouseControllerTuning tuning{};
    MouseRecoilConfig recoil{};
};

struct MouseControllerTickInput {
    MouseSourceCounts source_counts{};
    MouseResponseProfile response_profile{};
    double now_seconds = 0.0;
    std::uint64_t tick_id = 0;
    bool right_button_down = false;
    bool left_button_down = false;
};

struct MouseControllerTickResult {
    pipeline_contract::UserAimIntent vision_intent{};
    MouseNormalizedInput manual_input{};
    MouseActuationResult actuation{};
    MouseSourceCounts aim_counts{};
    MouseRecoilSample recoil{};
    double aim_residual_x = 0, aim_residual_y = 0;
    float dt_seconds = 0.001f;
    float final_u_x = 0.0f;
    float final_u_y = 0.0f;
    bool controller_used = false;
    bool auto_fire_active = false;
    bool transparent = true;
};

controller_native::GamepadRuntimeConfig make_mouse_controller_config(
    controller_native::GamepadRuntimeConfig config = {}, MouseControllerTuning tuning = {});

class MouseControllerFacade {
public:
    explicit MouseControllerFacade(MouseControllerFacadeConfig config = {});

    void submit_vision_snapshot(
        const controller_native::ControllerVisionSnapshot& snapshot);
    MouseControllerTickResult tick(const MouseControllerTickInput& input);
    void reset();

    bool last_calibration_observation(
        MouseCalibrationObservation* observation) const noexcept;
    const controller_native::NativeGamepadController& controller() const noexcept;
    float bodylock_effective_range_px() const noexcept;
    float bodylock_point_tolerance_px() const noexcept { return tuning_.bodylock_point_tolerance_px; }

private:
    MouseControllerTickResult tick_aim(const MouseControllerTickInput& input);
    static float tick_dt(double now_seconds, double previous_seconds) noexcept;
    void reset_controller_path() noexcept;

    controller_native::GamepadRuntimeConfig controller_config_{};
    double controller_clock_seconds_ = 0.0;
    controller_native::NativeGamepadController controller_;
    MouseRateAdapter rate_adapter_;
    MouseActuatorAdapter actuator_adapter_;
    MouseControllerTuning tuning_{};
    MouseRecoil recoil_;
    controller_native::ControllerVisionSnapshot pending_snapshot_{};
    bool has_pending_snapshot_ = false;
    MouseCalibrationObservation last_calibration_observation_{};
    bool has_calibration_observation_ = false;
    bool controller_active_ = false;
    std::uint64_t active_profile_generation_ = 0;
    std::uint64_t next_tick_id_ = 1;
    double previous_tick_seconds_ = 0.0;
};

}  // namespace mouse_native
