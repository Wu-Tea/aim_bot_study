#pragma once

#include "input_edge_reducer.h"

#include <cstdint>

namespace controller_native {

enum class AimScopeSource : unsigned char {
    None = 0,
    PhysicalAds = 1,
    ManualFire = 2,
    PhysicalAdsAndManualFire = 3,
    TouchpadFire = 4,
    PhysicalAdsAndTouchpadFire = 5,
    ManualFireAndTouchpadFire = 6,
    PhysicalAdsAndManualFireAndTouchpadFire = 7,
};

struct AimScopeSnapshot {
    AimScopeSource source = AimScopeSource::None;
    bool physical_ads_active = false;
    bool manual_fire_active = false;
    bool touch_fire_active = false;
    bool touch_fire_pressed = false;
    bool touch_fire_released = false;
    bool assist_active = false;
    bool physical_ads_pressed = false;
    bool physical_ads_released = false;
    bool physical_ads_ready = false;
    bool physical_ads_ready_pressed = false;
    bool physical_ads_ready_released = false;
    bool manual_fire_pressed = false;
    bool manual_fire_released = false;
    bool scope_acquired = false;
    bool scope_released = false;
    std::uint64_t scope_epoch = 0;
    std::uint64_t physical_ads_epoch = 0;
    std::uint64_t manual_fire_epoch = 0;
    double physical_ads_started_seconds = 0.0;
};

// Sole owner of aim-scope leases. It consumes typed input facts and never
// samples a controller or inspects a target.
class AimScopeReducer {
public:
    void reset() noexcept {
        physical_ads_active_ = false;
        manual_fire_active_ = false;
        previous_assist_active_ = false;
        scope_epoch_ = 0;
        physical_ads_epoch_ = 0;
        manual_fire_epoch_ = 0;
        physical_ads_started_seconds_ = 0.0;
    }

    AimScopeSnapshot reduce(
        const InputEdgeSnapshot& input,
        bool manual_fire_activates_aim,
        double now_seconds = 0.0) noexcept {
        AimScopeSnapshot snapshot{};
        snapshot.physical_ads_pressed = input.physical_ads_pressed;
        snapshot.touch_fire_active = input.touch_fire_active;
        snapshot.touch_fire_pressed = input.touch_fire_pressed;
        snapshot.touch_fire_released = input.touch_fire_released;
        snapshot.physical_ads_released = input.physical_ads_released;
        snapshot.physical_ads_ready = input.physical_ads_ready;
        snapshot.physical_ads_ready_pressed =
            input.physical_ads_ready_pressed;
        snapshot.physical_ads_ready_released =
            input.physical_ads_ready_released;
        if (snapshot.physical_ads_pressed) {
            ++physical_ads_epoch_;
            physical_ads_started_seconds_ = now_seconds;
        }
        if (manual_fire_activates_aim) {
            snapshot.manual_fire_pressed = input.manual_fire_pressed;
            snapshot.manual_fire_released = input.manual_fire_released;
            if (snapshot.manual_fire_pressed) ++manual_fire_epoch_;
        }

        physical_ads_active_ = input.physical_ads_active;
        if (!physical_ads_active_) physical_ads_started_seconds_ = 0.0;
        manual_fire_active_ = manual_fire_activates_aim &&
            input.manual_fire_signal_active;
        const bool assist_active =
            physical_ads_active_ || manual_fire_active_ || input.touch_fire_active;
        snapshot.scope_acquired =
            assist_active && !previous_assist_active_;
        snapshot.scope_released =
            !assist_active && previous_assist_active_;
        if (snapshot.scope_acquired) ++scope_epoch_;

        snapshot.physical_ads_active = physical_ads_active_;
        snapshot.manual_fire_active = manual_fire_active_;
        snapshot.assist_active = assist_active;
        snapshot.scope_epoch = scope_epoch_;
        snapshot.physical_ads_epoch = physical_ads_epoch_;
        snapshot.physical_ads_started_seconds = physical_ads_started_seconds_;
        snapshot.manual_fire_epoch = manual_fire_epoch_;
        // Existing enum values encode ADS=1, manual=2; touch adds bit 4.
        snapshot.source = static_cast<AimScopeSource>((physical_ads_active_ ? 1 : 0) |
            (manual_fire_active_ ? 2 : 0) | (input.touch_fire_active ? 4 : 0));
        previous_assist_active_ = assist_active;
        return snapshot;
    }

private:
    bool physical_ads_active_ = false;
    bool manual_fire_active_ = false;
    bool previous_assist_active_ = false;
    std::uint64_t scope_epoch_ = 0;
    std::uint64_t physical_ads_epoch_ = 0;
    std::uint64_t manual_fire_epoch_ = 0;
    double physical_ads_started_seconds_ = 0.0;
};

}  // namespace controller_native
