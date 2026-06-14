#include "fps_tracker/recoil_visual_model.hpp"

#include <algorithm>
#include <cmath>

namespace fps {

void RecoilVisualModel::onShot(TimeSec shotTime, WeaponId weapon, std::uint32_t shotIndex) {
    if (!cfg_.enabled) {
        return;
    }
    const double scale = 1.0 + static_cast<double>(shotIndex) * cfg_.shotIndexScale;
    events_.push_back({shotTime, weapon, shotIndex, cfg_.baseKickTrackUnits * scale});
    trimBefore(shotTime - cfg_.eventTtlSec);
}

void RecoilVisualModel::trimBefore(TimeSec t) {
    while (!events_.empty() && events_.front().time + cfg_.eventTtlSec < t) {
        events_.pop_front();
    }
}

Vec2 RecoilVisualModel::displacement(TimeSec t) const {
    if (!cfg_.enabled) {
        return {0.0, 0.0};
    }

    Vec2 total{};
    const double rise = std::max(cfg_.riseTimeSec, 1e-4);
    const double decay = std::max(cfg_.decayTimeSec, 1e-4);

    for (const ShotEvent& ev : events_) {
        const double age = t - ev.time;
        if (age < 0.0 || age > cfg_.eventTtlSec) {
            continue;
        }

        // Smooth impulse: starts at 0, rises quickly, then decays toward 0.
        const double envelope = (1.0 - std::exp(-age / rise)) * std::exp(-age / decay);
        total += ev.kick * envelope;
    }
    return total;
}

} // namespace fps
