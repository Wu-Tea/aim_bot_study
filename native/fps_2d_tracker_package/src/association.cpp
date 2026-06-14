#include "fps_tracker/association.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fps {

bool Associator::compatibleClass(TargetClass a, TargetClass b) const {
    if (a == TargetClass::Unknown || b == TargetClass::Unknown) {
        return true;
    }
    if (a == b) {
        return true;
    }
    // In many detectors, Player/Body labels are interchangeable for the same identity.
    const bool ap = (a == TargetClass::Player || a == TargetClass::Body);
    const bool bp = (b == TargetClass::Player || b == TargetClass::Body);
    return ap && bp;
}

std::optional<AssociationPair> Associator::scorePair(const AssociationTrackView& tr,
                                                     const AssociationMeasurement& m) const {
    if (!compatibleClass(tr.cls, m.detection.cls)) {
        return std::nullopt;
    }

    const Vec2 innov = m.bodyCompTrack - tr.predictedBodyCompTrack;
    const Mat2 S = tr.predictedPosCov + m.R;
    const Vec2 v = S.inverse() * innov;
    const double d2 = dot(innov, v);
    if (!std::isfinite(d2) || d2 > cfg_.gateD2) {
        return std::nullopt;
    }

    const double pixelDist = (m.detection.bodyBoxPx.center() - tr.predictedBoxPx.center()).length();
    if (pixelDist > cfg_.maxPixelDistance) {
        return std::nullopt;
    }

    const double boxIou = iou(tr.predictedBoxPx, m.detection.bodyBoxPx);
    const double areaCost = std::abs(safeLogRatio(m.detection.bodyBoxPx.area(), tr.predictedBoxPx.area()));
    const double aspectCost = std::abs(safeLogRatio(m.detection.bodyBoxPx.aspect(), tr.predictedBoxPx.aspect()));
    const double tierCost = (tr.tier == TargetTier::Unknown || m.detection.tier == TargetTier::Unknown || tr.tier == m.detection.tier) ? 0.0 : 1.0;

    const double posCost = clamp(d2 / cfg_.gateD2, 0.0, 1.0);
    double cost = 0.0;
    cost += cfg_.wPos * posCost;
    cost += cfg_.wIou * (1.0 - boxIou);
    cost += cfg_.wSize * clamp(areaCost, 0.0, 2.0) * 0.5;
    cost += cfg_.wAspect * clamp(aspectCost, 0.0, 2.0) * 0.5;
    cost += cfg_.wTier * tierCost;
    cost -= cfg_.wConfidence * clamp(m.detection.confidence, 0.0, 1.0);

    if (!tr.confirmed) {
        // Tentative tracks should not pull detections aggressively.
        cost += 0.03;
    }

    if (!std::isfinite(cost) || cost > cfg_.matchCostThreshold) {
        return std::nullopt;
    }

    return AssociationPair{tr.trackIndex, m.detectionIndex, cost, d2, boxIou};
}

AssociationResult Associator::match(const std::vector<AssociationTrackView>& tracks,
                                    const std::vector<AssociationMeasurement>& measurements) const {
    AssociationResult result;
    result.unmatchedTrackViews.reserve(tracks.size());
    result.unmatchedMeasurements.reserve(measurements.size());

    std::vector<AssociationPair> pairs;
    for (const auto& tr : tracks) {
        for (const auto& m : measurements) {
            if (auto pair = scorePair(tr, m)) {
                pairs.push_back(*pair);
            }
        }
    }

    std::sort(pairs.begin(), pairs.end(), [](const AssociationPair& a, const AssociationPair& b) {
        if (a.cost != b.cost) return a.cost < b.cost;
        return a.d2 < b.d2;
    });

    std::vector<bool> usedTracks(tracks.size(), false);
    std::vector<bool> usedMeasurements(measurements.size(), false);

    for (const auto& pair : pairs) {
        if (pair.trackIndex >= usedTracks.size() || pair.measurementIndex >= usedMeasurements.size()) {
            continue;
        }
        if (usedTracks[pair.trackIndex] || usedMeasurements[pair.measurementIndex]) {
            continue;
        }
        usedTracks[pair.trackIndex] = true;
        usedMeasurements[pair.measurementIndex] = true;
        result.matches.push_back(pair);
    }

    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (!usedTracks[i]) {
            result.unmatchedTrackViews.push_back(i);
        }
    }
    for (std::size_t i = 0; i < measurements.size(); ++i) {
        if (!usedMeasurements[i]) {
            result.unmatchedMeasurements.push_back(i);
        }
    }

    return result;
}

} // namespace fps
