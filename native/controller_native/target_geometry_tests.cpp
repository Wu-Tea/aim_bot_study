#include "target_geometry.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace {

void require_near(float actual, float expected) {
    if (std::fabs(actual - expected) > 0.001f) std::abort();
}

void test_upright_and_crouched_boxes_use_relative_height() {
    const controller_native::TargetGeometryConfig config{0.365f};
    for (const common_native::Box2f box : {
             common_native::Box2f{280.0f, 100.0f, 80.0f, 200.0f},
             common_native::Box2f{280.0f, 150.0f, 100.0f, 120.0f}}) {
        const auto result = controller_native::resolve_target_geometry(
            {{320.0f, 250.0f}, box, true}, config);
        require_near(result.aim_px.x, 320.0f);
        require_near(result.aim_px.y, box.y + box.h * 0.365f);
        if (!result.geometry_resolved) std::abort();
    }
}

void test_wide_low_box_preserves_and_clamps_vision_point() {
    const common_native::Box2f box{100.0f, 200.0f, 200.0f, 100.0f};
    const auto inside = controller_native::resolve_target_geometry(
        {{180.0f, 240.0f}, box, true}, {0.365f});
    require_near(inside.aim_px.x, 180.0f);
    require_near(inside.aim_px.y, 240.0f);
    const auto outside = controller_native::resolve_target_geometry(
        {{350.0f, 180.0f}, box, true}, {0.365f});
    require_near(outside.aim_px.x, 300.0f);
    require_near(outside.aim_px.y, 200.0f);
}

void test_missing_or_invalid_box_preserves_vision_point() {
    for (const auto input : {
             controller_native::TargetGeometryInput{{321.0f, 222.0f}, {}, false},
             controller_native::TargetGeometryInput{
                 {321.0f, 222.0f}, {100.0f, 100.0f, 0.0f, 200.0f}, true}}) {
        const auto result = controller_native::resolve_target_geometry(input, {0.365f});
        require_near(result.aim_px.x, 321.0f);
        require_near(result.aim_px.y, 222.0f);
        if (result.geometry_resolved) std::abort();
    }
}

}  // namespace

int main() {
    test_upright_and_crouched_boxes_use_relative_height();
    test_wide_low_box_preserves_and_clamps_vision_point();
    test_missing_or_invalid_box_preserves_vision_point();
    return 0;
}
