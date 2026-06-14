#pragma once

#include <deque>

#include "fps_tracker/types.hpp"

namespace fps {

class RecoilVisualModel {
public:
    explicit RecoilVisualModel(RecoilVisualConfig cfg = {}) : cfg_(cfg) {}

    void onShot(TimeSec shotTime, WeaponId weapon, std::uint32_t shotIndex);
    void trimBefore(TimeSec t);

    [[nodiscard]] Vec2 displacement(TimeSec t) const;
    [[nodiscard]] const RecoilVisualConfig& config() const { return cfg_; }

private:
    struct ShotEvent {
        TimeSec time = 0.0;
        WeaponId weapon = 0;
        std::uint32_t shotIndex = 0;
        Vec2 kick{};
    };

    RecoilVisualConfig cfg_{};
    std::deque<ShotEvent> events_;
};

} // namespace fps
