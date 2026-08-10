#include "telemetry_target_identity.h"

#include <algorithm>
#include <cmath>

namespace runtime_app {
namespace {

float area(float x1, float y1, float x2, float y2) noexcept {
    return std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
}

float intersection_over_union(
    const TargetIdentityObservation& a,
    const TargetIdentityObservation& b) noexcept {
    const float ix1 = std::max(a.x1, b.x1);
    const float iy1 = std::max(a.y1, b.y1);
    const float ix2 = std::min(a.x2, b.x2);
    const float iy2 = std::min(a.y2, b.y2);
    const float intersection = area(ix1, iy1, ix2, iy2);
    const float total = area(a.x1, a.y1, a.x2, a.y2) +
        area(b.x1, b.y1, b.x2, b.y2) - intersection;
    return total > 0.0f ? intersection / total : 0.0f;
}

} // namespace

TargetIdentityResult TelemetryTargetIdentity::observe(
    const TargetIdentityObservation& observation) noexcept {
    TargetIdentityResult result;
    result.previous_track_id = active_id_;

    if (!observation.live) {
        result.track_id = active_id_;
        result.event = active_id_ != 0 && !lost_ ? TargetEventKind::Lost : TargetEventKind::None;
        lost_ = active_id_ != 0;
        return result;
    }

    if (observation.association_ambiguous) {
        result.track_id = active_id_;
        result.quality = TargetIdentityQuality::Ambiguous;
        return result;
    }

    const bool compatible = geometry_compatible(observation);
    const bool matched = compatible && strong_match(observation);
    const bool needs_new_id = active_id_ == 0 || observation.explicit_switch || !matched;
    if (needs_new_id) {
        const std::uint64_t previous = active_id_;
        active_id_ = issue_id();
        result.previous_track_id = previous;
        if (previous == 0) result.event = TargetEventKind::Created;
        else if (lost_) result.event = TargetEventKind::Reacquired;
        else result.event = TargetEventKind::Switched;
    } else if (lost_) {
        result.event = TargetEventKind::Reacquired;
    }

    result.track_id = active_id_;
    result.quality = observation.production_associated
        ? TargetIdentityQuality::ProductionAssociated
        : TargetIdentityQuality::StrongGeometricMatch;
    last_live_ = observation;
    has_last_live_ = true;
    lost_ = false;
    return result;
}

void TelemetryTargetIdentity::reset() noexcept {
    next_id_ = 1;
    active_id_ = 0;
    last_live_ = TargetIdentityObservation{};
    has_last_live_ = false;
    lost_ = false;
}

std::uint64_t TelemetryTargetIdentity::issue_id() noexcept {
    return next_id_++;
}

bool TelemetryTargetIdentity::geometry_compatible(
    const TargetIdentityObservation& observation) const noexcept {
    return !has_last_live_ ||
        (observation.frame_width == last_live_.frame_width &&
         observation.frame_height == last_live_.frame_height);
}

bool TelemetryTargetIdentity::strong_match(
    const TargetIdentityObservation& observation) const noexcept {
    if (!has_last_live_) return true;
    const float dx = observation.target_x - last_live_.target_x;
    const float dy = observation.target_y - last_live_.target_y;
    const float center_distance = std::sqrt(dx * dx + dy * dy);
    const float box_width = std::max(1.0f, last_live_.x2 - last_live_.x1);
    const float box_height = std::max(1.0f, last_live_.y2 - last_live_.y1);
    const float distance_limit = std::max(32.0f, 0.75f * std::max(box_width, box_height));
    return intersection_over_union(last_live_, observation) >= 0.20f &&
        center_distance <= distance_limit;
}

} // namespace runtime_app
