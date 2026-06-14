#include "fps_tracker/projection_model.hpp"

#include <algorithm>

namespace fps {

Vec2 ProjectionModel::screenCenterPx() const {
    return {cfg_.screenSizePx.x * 0.5, cfg_.screenSizePx.y * 0.5};
}

Vec2 ProjectionModel::focalPx(const ModeState& mode) const {
    const double zoom = std::max(0.05, mode.zoom);
    const double sx = std::max(0.05, mode.fovScaleX);
    const double sy = std::max(0.05, mode.fovScaleY);

    // Larger zoom/fovScale means more pixels per same screen-angle-like movement.
    return {cfg_.baseFocalPx.x * zoom * sx, cfg_.baseFocalPx.y * zoom * sy};
}

Vec2 ProjectionModel::screenToTrack(Vec2 screenPx, const ModeState& mode) const {
    const Vec2 c = screenCenterPx();
    const Vec2 f = focalPx(mode);
    return {(screenPx.x - c.x) / f.x, (screenPx.y - c.y) / f.y};
}

Vec2 ProjectionModel::trackToScreen(Vec2 trackCoord, const ModeState& mode) const {
    const Vec2 c = screenCenterPx();
    const Vec2 f = focalPx(mode);
    return {c.x + trackCoord.x * f.x, c.y + trackCoord.y * f.y};
}

Box2 ProjectionModel::projectBox(Vec2 centerTrackScreenLike, Vec2 sizePx, const ModeState& mode) const {
    const Vec2 centerPx = trackToScreen(centerTrackScreenLike, mode);
    return Box2::fromCenterSize(centerPx, sizePx);
}

} // namespace fps
