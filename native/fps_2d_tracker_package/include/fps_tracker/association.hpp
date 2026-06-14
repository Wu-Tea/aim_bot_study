#pragma once

#include <optional>
#include <vector>

#include "fps_tracker/kalman_cv2d.hpp"
#include "fps_tracker/types.hpp"

namespace fps {

struct AssociationMeasurement {
    std::size_t detectionIndex = 0;
    Detection detection{};
    Vec2 bodyCompTrack{};
    Vec2 aimOffsetTrack{};
    Mat2 R{};
};

struct AssociationTrackView {
    std::size_t trackIndex = 0;
    TrackId trackId = kInvalidTrackId;
    Vec2 predictedBodyCompTrack{};
    Mat2 predictedPosCov{};
    Box2 predictedBoxPx{};
    Vec2 boxSizePx{};
    TargetClass cls = TargetClass::Player;
    TargetTier tier = TargetTier::Unknown;
    bool confirmed = false;
};

struct AssociationPair {
    std::size_t trackIndex = 0;
    std::size_t measurementIndex = 0;
    double cost = 0.0;
    double d2 = 0.0;
    double iou = 0.0;
};

struct AssociationResult {
    std::vector<AssociationPair> matches;
    std::vector<std::size_t> unmatchedTrackViews;
    std::vector<std::size_t> unmatchedMeasurements;
};

class Associator {
public:
    explicit Associator(AssociationConfig cfg = {}) : cfg_(cfg) {}

    [[nodiscard]] AssociationResult match(const std::vector<AssociationTrackView>& tracks,
                                          const std::vector<AssociationMeasurement>& measurements) const;

private:
    [[nodiscard]] bool compatibleClass(TargetClass a, TargetClass b) const;
    [[nodiscard]] std::optional<AssociationPair> scorePair(const AssociationTrackView& tr,
                                                           const AssociationMeasurement& m) const;

    AssociationConfig cfg_{};
};

} // namespace fps
