#pragma once

#include "controller_tick_context.h"
#include "runtime_config.h"
#include "virtual_gamepad.h"

#include <cstdint>

namespace controller_native {

enum class AutoFireBlockReason : std::uint8_t {
    None,
    NotAiming,
    AimNotReady,
    NotRequested,
    NoTarget,
    NoFireAuthority,
    StaleSource,
    ManualFire,
    ManualTakeoverGuard,
};

const char* auto_fire_block_reason_name(AutoFireBlockReason reason);

struct NativeAutoFireCounters {
    std::uint64_t requested = 0;
    std::uint64_t allowed = 0;
    std::uint64_t blocked = 0;
};

struct AutoFireGateInput {
    NativeControllerVisionState vision_state;
    bool aiming = false;
    bool ads_min_elapsed = true;
    bool manual_fire_pressed = false;
    double now_seconds = 0.0;
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
    float output_right_x = 0.0f;
    float output_right_y = 0.0f;
    float settle_dx = 0.0f;
    float settle_dy = 0.0f;
};

struct AutoFireGateDecision {
    bool aim_ready = true;
    bool pre_takeover_should_fire = false;
    bool should_fire = false;
    bool before_auto_fire_active = false;
    bool after_auto_fire_active = false;
    bool release_fire_output = false;
    AutoFireBlockReason block_reason = AutoFireBlockReason::None;
    NativeAutoFireCounters counters;
};

class AutoFireGate {
public:
    AutoFireGate(
        GamepadAutoFireConfig auto_fire_config = {},
        GamepadAiAimConfig ai_config = {});

    void reset();
    void reset_readiness();
    AutoFireGateDecision evaluate(const AutoFireGateInput& input);
    NativeAutoFireCounters counters() const;
    bool active() const;
    void apply_fire_output(GamepadOutputState& output, bool should_fire) const;
    void release_fire_output(GamepadOutputState& output) const;

private:
    bool aim_ready_for_input(const AutoFireGateInput& input);
    AutoFireBlockReason block_reason_for_input(
        const AutoFireGateInput& input,
        bool aim_ready) const;
    bool has_fresh_auto_fire_source(
        const NativeControllerVisionState& vision_state,
        double now_seconds) const;
    bool has_fresh_aim_target(
        const NativeControllerVisionState& vision_state,
        double now_seconds) const;
    bool is_strong_fire_target(const NativeControllerVisionState& vision_state) const;
    double manual_takeover_elapsed(double now_seconds) const;
    double manual_takeover_total_seconds() const;

    GamepadAutoFireConfig auto_fire_config_;
    GamepadAiAimConfig ai_config_;
    NativeAutoFireCounters counters_;
    bool manual_fire_was_pressed_ = false;
    bool auto_fire_was_active_ = false;
    double manual_takeover_started_at_seconds_ = -1.0;
    int ready_frames_ = 0;
    bool has_ready_vision_sequence_ = false;
    std::uint64_t ready_vision_sequence_ = 0;
};

}  // namespace controller_native
