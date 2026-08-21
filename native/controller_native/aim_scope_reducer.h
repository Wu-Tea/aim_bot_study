#pragma once

#include "input_edge_reducer.h"

#include <cstdint>

namespace controller_native {

enum class AimScopeSource : unsigned char {
    None,
    PhysicalAds,
    ManualFire,
    PhysicalAdsAndManualFire,
};

struct AimScopeSnapshot {
    AimScopeSource source = AimScopeSource::None;
    bool physical_ads_active = false;
    bool manual_fire_active = false;
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
    }

    AimScopeSnapshot reduce(
        const InputEdgeSnapshot& input,
        bool manual_fire_activates_aim) noexcept {
        AimScopeSnapshot snapshot{};
        snapshot.physical_ads_pressed = input.physical_ads_pressed;
        snapshot.physical_ads_released = input.physical_ads_released;
        snapshot.physical_ads_ready = input.physical_ads_ready;
        snapshot.physical_ads_ready_pressed =
            input.physical_ads_ready_pressed;
        snapshot.physical_ads_ready_released =
            input.physical_ads_ready_released;
        if (snapshot.physical_ads_pressed) ++physical_ads_epoch_;
        if (manual_fire_activates_aim) {
            snapshot.manual_fire_pressed = input.manual_fire_pressed;
            snapshot.manual_fire_released = input.manual_fire_released;
            if (snapshot.manual_fire_pressed) ++manual_fire_epoch_;
        }

        physical_ads_active_ = input.physical_ads_active;
        manual_fire_active_ = manual_fire_activates_aim &&
            input.manual_fire_signal_active;
        const bool assist_active =
            physical_ads_active_ || manual_fire_active_;
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
        snapshot.manual_fire_epoch = manual_fire_epoch_;
        snapshot.source = physical_ads_active_
            ? manual_fire_active_
                ? AimScopeSource::PhysicalAdsAndManualFire
                : AimScopeSource::PhysicalAds
            : manual_fire_active_
                ? AimScopeSource::ManualFire
                : AimScopeSource::None;
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
};

}  // namespace controller_native
