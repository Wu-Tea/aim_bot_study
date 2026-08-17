#pragma once

#include "../pipeline_contract/control_event.h"
#include "xinput_reader.h"

#include <algorithm>
#include <cstdint>

namespace controller_native {

constexpr float kPhysicalAdsPressThreshold = 0.05f;
constexpr float kPhysicalAdsIdleThreshold = 0.03f;
constexpr unsigned int kPhysicalAdsIdleDebounceSamples = 3;
constexpr float kManualFireTriggerThreshold = 0.04f;

struct InputEdgeSnapshot {
    bool physical_ads_active = false;
    bool manual_fire_signal_active = false;
    bool physical_ads_pressed = false;
    bool physical_ads_released = false;
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
        previous_manual_fire_signal_ = false;
        physical_ads_idle_samples_ = 0;
    }

    InputEdgeSnapshot sample(
        const PhysicalGamepadState& physical,
        bool rb_counts_as_physical_ads,
        std::uint64_t* next_event_sequence) noexcept {
        InputEdgeSnapshot result{};
        const bool previous_physical_ads = physical_ads_active_;
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
        if (result.physical_ads_pressed || result.physical_ads_released) {
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
    bool previous_manual_fire_signal_ = false;
    unsigned int physical_ads_idle_samples_ = 0;
};

}  // namespace controller_native
