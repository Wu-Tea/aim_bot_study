#include "association.h"

#include <algorithm>

namespace tracking_native {

float squared_distance(common_native::Vec2f a, common_native::Vec2f b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

bool within_association_radius(common_native::Vec2f a, common_native::Vec2f b, float radius_px) {
    const float radius = std::max(0.0f, radius_px);
    return squared_distance(a, b) <= radius * radius;
}

}  // namespace tracking_native
