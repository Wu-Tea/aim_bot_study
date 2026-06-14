#pragma once

#include "fps_tracker/types.hpp"

namespace fps {

class ProjectionModel {
public:
    explicit ProjectionModel(ProjectionConfig cfg = {}) : cfg_(cfg) {}

    [[nodiscard]] Vec2 screenCenterPx() const;
    [[nodiscard]] Vec2 focalPx(const ModeState& mode) const;

    [[nodiscard]] Vec2 screenToTrack(Vec2 screenPx, const ModeState& mode) const;
    [[nodiscard]] Vec2 trackToScreen(Vec2 trackCoord, const ModeState& mode) const;
    [[nodiscard]] Box2 projectBox(Vec2 centerTrackScreenLike, Vec2 sizePx, const ModeState& mode) const;

    [[nodiscard]] const ProjectionConfig& config() const { return cfg_; }

private:
    ProjectionConfig cfg_{};
};

} // namespace fps
