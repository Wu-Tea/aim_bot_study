#pragma once

#include "fps_tracker/types.hpp"

namespace fps {

class StickProjector {
public:
    explicit StickProjector(StickProjectorConfig cfg = {}) : cfg_(cfg) {}

    [[nodiscard]] Vec2 normalizedStick(Vec2 finalStick) const;
    [[nodiscard]] Vec2 screenRateTrackUnitsPerSec(const ControlSample& sample) const;
    [[nodiscard]] const StickProjectorConfig& config() const { return cfg_; }

private:
    StickProjectorConfig cfg_{};
};

} // namespace fps
