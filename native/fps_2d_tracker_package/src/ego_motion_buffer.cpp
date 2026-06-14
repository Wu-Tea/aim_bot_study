#include "fps_tracker/ego_motion_buffer.hpp"

#include <algorithm>

namespace fps {

EgoMotionBuffer::EgoMotionBuffer(StickProjector stick, RecoilVisualModel recoil)
    : stick_(std::move(stick)), recoil_(std::move(recoil)) {}

void EgoMotionBuffer::push(const ControlSample& sample) {
    if (!samples_.empty() && sample.applyTime < samples_.back().applyTime) {
        // Reference implementation: ignore out-of-order control samples. Production code
        // should either sort within a fixed-lag window or replay the integration buffer.
        return;
    }

    samples_.push_back(sample);
    while (samples_.size() > kMaxSamples) {
        samples_.pop_front();
    }

    if (sample.fireButton) {
        const bool newShot = !haveLastShot_ ||
            sample.weapon != lastShotWeapon_ ||
            sample.shotIndex != lastShotIndex_ ||
            (sample.applyTime - lastShotTime_) > 0.020;
        if (newShot) {
            recoil_.onShot(sample.applyTime, sample.weapon, sample.shotIndex);
            haveLastShot_ = true;
            lastShotWeapon_ = sample.weapon;
            lastShotIndex_ = sample.shotIndex;
            lastShotTime_ = sample.applyTime;
        }
    }

    recoil_.trimBefore(sample.applyTime - 1.0);
}

Vec2 EgoMotionBuffer::cumulativeStick(TimeSec t) const {
    if (samples_.empty()) {
        return {0.0, 0.0};
    }

    Vec2 total{};
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const ControlSample& s = samples_[i];
        if (s.applyTime >= t) {
            break;
        }
        const TimeSec nextTime = (i + 1 < samples_.size()) ? samples_[i + 1].applyTime : t;
        const TimeSec end = std::min(t, nextTime);
        const double dt = std::max(0.0, end - s.applyTime);
        if (dt > 0.0) {
            total += stick_.screenRateTrackUnitsPerSec(s) * dt;
        }
    }
    return total;
}

Vec2 EgoMotionBuffer::cumulative(TimeSec t) const {
    return cumulativeStick(t) + recoil_.displacement(t);
}

} // namespace fps
