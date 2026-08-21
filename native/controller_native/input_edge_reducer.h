#pragma once

#include "../pipeline_contract/control_event.h"
#include "xinput_reader.h"

#include <algorithm>
#include <cstdint>

namespace controller_native {

constexpr float kPhysicalAdsPressThreshold = 0.05f;
constexpr float kPhysicalAdsIdleThreshold = 0.03f;
constexpr unsigned int kPhysicalAdsIdleDebounceSamples = 3;
constexpr float kPhysicalAdsReadyHysteresis = 0.05f;
constexpr float kManualFireTriggerThreshold = 0.04f;

struct InputEdgeSnapshot {
    bool physical_ads_active = false;
    bool manual_fire_signal_active = false;
    bool physical_ads_pressed = false;
    bool physical_ads_released = false;
    bool physical_ads_ready = false;
    bool physical_ads_ready_pressed = false;
    bool physical_ads_ready_released = false;
    bool manual_fire_pressed = false;
    bool manual_fire_released = false;
    pipeline_contract::EventSequence cause_event{};
};

// Sole owner of sampled LT/fire hysteresis and edge detection. The snapshot is
// the typed event boundary consumed by AimScopeReducer; no duplicate event bus
// mirrors the same facts.
class InputEdgeReducer {
public:
    void reset() noexcept {
        physical_ads_active_ = false;
        physical_ads_ready_ = false;
        previous_manual_fire_signal_ = false;
        physical_ads_idle_samples_ = 0;
    }

    InputEdgeSnapshot sample(
        const PhysicalGamepadState& physical,
        bool rb_counts_as_physical_ads,
        std::uint64_t* next_event_sequence,
        float ads_ready_threshold = 0.80f) noexcept {
        InputEdgeSnapshot result{};
        const bool previous_physical_ads = physical_ads_active_;
        const bool previous_physical_ads_ready = physical_ads_ready_;
        const float left_trigger = std::clamp(physical.left_trigger, 0.0f, 1.0f);
        const bool rb_ads = rb_counts_as_physical_ads && physical.rb;
        if (rb_ads) {
            physical_ads_active_ = true;
            physical_ads_idle_samples_ = 0;
        } else if (left_trigger <= kPhysicalAdsIdleThreshold) {
            if (physical_ads_active_ &&
                ++physical_ads_idle_samples_ >=
                    kPhysicalAdsIdleDebounceSamples) {
                physical_ads_active_ = false;
                physical_ads_idle_samples_ = 0;
            }
        } else {
            physical_ads_idle_samples_ = 0;
            if (!physical_ads_active_ &&
                left_trigger > kPhysicalAdsPressThreshold) {
                physical_ads_active_ = true;
            }
        }

        const bool manual_fire_signal =
            physical.rb || physical.right_trigger > kManualFireTriggerThreshold;
        result.physical_ads_active = physical_ads_active_;
        result.manual_fire_signal_active = manual_fire_signal;
        result.physical_ads_pressed =
            physical_ads_active_ && !previous_physical_ads;
        result.physical_ads_released =
            !physical_ads_active_ && previous_physical_ads;
        const float ready_threshold = std::clamp(
            ads_ready_threshold,
            kPhysicalAdsPressThreshold + 0.001f,
            1.0f);
        if (!physical_ads_active_) {
            physical_ads_ready_ = false;
        } else if (rb_ads) {
            physical_ads_ready_ = true;
        } else if (physical_ads_ready_) {
            physical_ads_ready_ = left_trigger >= std::max(
                kPhysicalAdsPressThreshold,
                ready_threshold - kPhysicalAdsReadyHysteresis);
        } else {
            physical_ads_ready_ = left_trigger >= ready_threshold;
        }
        result.physical_ads_ready = physical_ads_ready_;
        result.physical_ads_ready_pressed =
            physical_ads_ready_ && !previous_physical_ads_ready;
        result.physical_ads_ready_released =
            !physical_ads_ready_ && previous_physical_ads_ready;
        result.manual_fire_pressed =
            manual_fire_signal && !previous_manual_fire_signal_;
        result.manual_fire_released =
            !manual_fire_signal && previous_manual_fire_signal_;

        const auto assign_cause = [&]() noexcept {
            if (next_event_sequence == nullptr || *next_event_sequence == 0) {
                return;
            }
            result.cause_event = pipeline_contract::EventSequence::from(
                (*next_event_sequence)++);
        };
        if (result.physical_ads_pressed || result.physical_ads_released ||
            result.physical_ads_ready_pressed ||
            result.physical_ads_ready_released) {
            assign_cause();
        }
        if (result.manual_fire_pressed || result.manual_fire_released) {
            assign_cause();
        }
        previous_manual_fire_signal_ = manual_fire_signal;
        return result;
    }

private:
    bool physical_ads_active_ = false;
    bool physical_ads_ready_ = false;
    bool previous_manual_fire_signal_ = false;
    unsigned int physical_ads_idle_samples_ = 0;
};

}  // namespace controller_native
