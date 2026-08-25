#pragma once

#include "pipeline_contract/target_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace controller_native {

struct BodylockTargetMotionObserverConfig {
    float minimum_interval_seconds = 0.004f;
    float maximum_interval_seconds = 0.040f;
    float minimum_response_px_per_stick_second = 50.0f;
    float maximum_response_px_per_stick_second = 4000.0f;
    float maximum_target_motion_stick = 1.25f;
    // The median rejects a one-frame geometry spike. These continuous slew
    // limits then bound persistent measurement noise without reintroducing a
    // start threshold or a controller-rate actuation delay.
    float rise_slew_stick_per_second = 8.0f;
    float decay_slew_stick_per_second = 20.0f;
    float maximum_hold_seconds = 0.055f;
    std::uint32_t minimum_samples = 2;
};

struct BodylockTargetMotionObservation {
    std::uint64_t target_id = 0;
    double capture_seconds = 0.0;
    float interval_seconds = 0.0f;
    pipeline_contract::Vec2f observed_screen_rate_px_per_sec{};
    // Normalized camera-response command, not raw post-curve virtual stick.
    pipeline_contract::Vec2f average_delivered_stick{};
    float response_px_per_stick_second = 500.0f;
    float reliability = 0.0f;
    bool direct_person_observation = false;
};

struct BodylockTargetMotionEstimate {
    // Total camera command that would make the selected target stationary on
    // screen. This is target motion, not residual screen motion and not an AI
    // delta to add to the user's stick.
    pipeline_contract::Vec2f target_motion_stick{};
    pipeline_contract::Vec2f aligned_delivered_stick{};
    float confidence = 0.0f;
    std::uint32_t accepted_samples = 0;
    bool valid = false;
};

class BodylockTargetMotionObserver {
public:
    explicit BodylockTargetMotionObserver(
        BodylockTargetMotionObserverConfig config = {}) noexcept;

    bool update(const BodylockTargetMotionObservation& observation) noexcept;
    BodylockTargetMotionEstimate estimate(
        std::uint64_t target_id,
        double now_seconds) const noexcept;
    void begin_target(std::uint64_t target_id) noexcept;
    void reset() noexcept;

private:
    static constexpr std::size_t kMedianWindow = 3;

    pipeline_contract::Vec2f robust_measurement() const noexcept;
    float slew_axis(float current, float target, float dt_seconds) const noexcept;

    BodylockTargetMotionObserverConfig config_{};
    std::array<pipeline_contract::Vec2f, kMedianWindow> measurements_{};
    std::size_t measurement_count_ = 0;
    std::size_t measurement_write_index_ = 0;
    pipeline_contract::Vec2f filtered_motion_stick_{};
    pipeline_contract::Vec2f last_aligned_delivered_stick_{};
    std::uint64_t target_id_ = 0;
    double last_capture_seconds_ = 0.0;
    float confidence_ = 0.0f;
    std::uint32_t accepted_samples_ = 0;
    bool initialized_ = false;
};

}  // namespace controller_native
