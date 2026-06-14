#include "fps_tracker/stick_projector.hpp"

#include <algorithm>
#include <cmath>

namespace fps {

Vec2 StickProjector::normalizedStick(Vec2 finalStick) const {
    double mag = finalStick.length();
    if (mag <= cfg_.deadzone) {
        return {0.0, 0.0};
    }

    const double safeMag = std::max(mag, kEps);
    Vec2 dir = finalStick / safeMag;

    double norm = (mag - cfg_.deadzone) / std::max(1.0 - cfg_.deadzone, kEps);
    norm = clamp(norm, 0.0, 1.0);
    norm = std::pow(norm, std::max(0.05, cfg_.responseExponent));

    if (cfg_.antiDeadzone > 0.0 && norm > 0.0) {
        norm = cfg_.antiDeadzone + (1.0 - cfg_.antiDeadzone) * norm;
    }

    Vec2 out = dir * norm;
    // Respect square stick range after response shaping.
    out.x = clamp(out.x, -1.0, 1.0);
    out.y = clamp(out.y, -1.0, 1.0);
    return out;
}

Vec2 StickProjector::screenRateTrackUnitsPerSec(const ControlSample& sample) const {
    const Vec2 u = normalizedStick(sample.finalRightStick);
    const double adsMul = sample.mode.ads ? cfg_.adsRateMultiplier : 1.0;
    const double zoomMul = std::pow(std::max(0.05, sample.mode.zoom), cfg_.zoomRatePower);
    const double sens = std::max(0.0, sample.mode.sensitivity);
    return {u.x * cfg_.hipfireRateTrackUnitsPerSec.x * adsMul * zoomMul * sens,
            u.y * cfg_.hipfireRateTrackUnitsPerSec.y * adsMul * zoomMul * sens};
}

} // namespace fps
