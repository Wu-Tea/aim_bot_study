#pragma once

#include "../common_native/screen_geometry.h"

namespace tracking_native {

float squared_distance(common_native::Vec2f a, common_native::Vec2f b);
bool within_association_radius(common_native::Vec2f a, common_native::Vec2f b, float radius_px);

}  // namespace tracking_native
