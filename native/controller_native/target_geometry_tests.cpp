#include "target_geometry.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <initializer_list>
#include <stdexcept>

namespace {

void require_near(float actual, float expected) {
    if (std::fabs(actual - expected) > 0.001f) {
        throw std::runtime_error("target geometry value was outside tolerance");
    }
}

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
        require_true(result.geometry_resolved, "valid body box was not resolved");
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
    require_true(!result.geometry_resolved, "invalid body box was resolved");
    }
}

void test_stable_body_aim_rejects_single_edge_weapon_occlusion_without_debt() {
    controller_native::StableBodyAimTracker tracker;
    const common_native::Box2f base{100.0f, 100.0f, 40.0f, 100.0f};
    const auto initial = tracker.update(
        {120.0f, 136.5f}, base, false, true, {118.0f, 125.0f});

    const common_native::Box2f occluded{
        100.0f, 100.0f, 56.0f, 120.0f};
    const auto disturbed = tracker.update(
        {128.0f, 143.8f}, occluded, true, true, {118.0f, 125.0f});
    require_near(disturbed.aim_px.x, initial.aim_px.x);
    require_near(disturbed.aim_px.y, initial.aim_px.y);
    require_true(disturbed.rejected_shape_motion, "shape disturbance was not rejected");

    const auto restored = tracker.update(
        {120.0f, 136.5f}, base, true, true, {118.0f, 125.0f});
    require_near(restored.aim_px.x, initial.aim_px.x);
    require_near(restored.aim_px.y, initial.aim_px.y);
}

void test_stable_body_aim_preserves_rigid_target_translation() {
    controller_native::StableBodyAimTracker tracker;
    tracker.update(
        {120.0f, 136.5f},
        {100.0f, 100.0f, 40.0f, 100.0f},
        false,
        true,
        {118.0f, 125.0f});
    const auto translated = tracker.update(
        {125.0f, 132.5f},
        {105.0f, 96.0f, 40.0f, 100.0f},
        true,
        true,
        {123.0f, 121.0f});
    require_near(translated.aim_px.x, 125.0f);
    require_near(translated.aim_px.y, 132.5f);
    require_true(!translated.rejected_shape_motion, "rigid translation was rejected");
}

void test_stable_body_aim_bridges_one_anchor_dropout_without_box_debt() {
    controller_native::StableBodyAimTracker tracker;
    const common_native::Box2f box{100.0f, 100.0f, 40.0f, 100.0f};
    tracker.update(
        {120.0f, 136.5f}, box, false, true, {118.0f, 125.0f});
    tracker.update(
        {122.0f, 136.5f},
        {102.0f, 100.0f, 40.0f, 100.0f},
        true,
        true,
        {120.0f, 125.0f});
    const auto missing = tracker.update(
        {134.0f, 143.8f},
        {104.0f, 100.0f, 56.0f, 120.0f},
        true,
        false);
    require_near(missing.aim_px.x, 124.0f);
    require_near(missing.aim_px.y, 136.5f);

    const auto recovered = tracker.update(
        {126.0f, 136.5f},
        {106.0f, 100.0f, 40.0f, 100.0f},
        true,
        true,
        {124.0f, 125.0f});
    require_near(recovered.aim_px.x, 126.0f);
    require_near(recovered.aim_px.y, 136.5f);
}

}  // namespace

void register_target_geometry_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "upright_and_crouched_boxes_use_relative_height", test_upright_and_crouched_boxes_use_relative_height);
    registry.add_case("BaseBodyLock", "wide_low_box_preserves_vision_point", test_wide_low_box_preserves_and_clamps_vision_point);
    registry.add_case("BaseBodyLock", "invalid_box_preserves_vision_point", test_missing_or_invalid_box_preserves_vision_point);
    registry.add_case("BaseBodyLock", "edge_weapon_occlusion_has_no_geometry_debt", test_stable_body_aim_rejects_single_edge_weapon_occlusion_without_debt);
    registry.add_case("BaseBodyLock", "stable_aim_preserves_rigid_translation", test_stable_body_aim_preserves_rigid_target_translation);
    registry.add_case("BaseBodyLock", "stable_aim_bridges_one_anchor_dropout", test_stable_body_aim_bridges_one_anchor_dropout_without_box_debt);
}
