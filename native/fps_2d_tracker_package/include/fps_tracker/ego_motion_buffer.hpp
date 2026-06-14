#pragma once

#include <deque>

#include "fps_tracker/recoil_visual_model.hpp"
#include "fps_tracker/stick_projector.hpp"

namespace fps {

class EgoMotionBuffer {
public:
    EgoMotionBuffer(StickProjector stick = StickProjector{},
                    RecoilVisualModel recoil = RecoilVisualModel{});

    void push(const ControlSample& sample);

    [[nodiscard]] Vec2 cumulativeStick(TimeSec t) const;
    [[nodiscard]] Vec2 cumulative(TimeSec t) const;
    [[nodiscard]] Vec2 delta(TimeSec a, TimeSec b) const { return cumulative(b) - cumulative(a); }

    [[nodiscard]] const StickProjector& stickProjector() const { return stick_; }
    [[nodiscard]] const RecoilVisualModel& recoilModel() const { return recoil_; }

private:
    std::deque<ControlSample> samples_;
    StickProjector stick_;
    RecoilVisualModel recoil_;

    bool haveLastShot_ = false;
    WeaponId lastShotWeapon_ = 0;
    std::uint32_t lastShotIndex_ = 0;
    TimeSec lastShotTime_ = -1.0;

    static constexpr std::size_t kMaxSamples = 1024;
};

} // namespace fps
