#include "target_state_reducers.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::VisionCandidate candidate() {
    pipeline_contract::VisionCandidate value{};
    value.has_aim_point = true;
    value.aim_px = {50.0f, 30.0f};
    value.has_aim_region = true;
    value.aim_region_px = {0.0f, 0.0f, 100.0f, 100.0f};
    value.aim_region_source =
        pipeline_contract::AimRegionSource::VisionGeometry;
    return value;
}

void test_geometry_and_desired_point_have_separate_state() {
    controller_native::TargetGeometryReducer geometry;
    controller_native::DesiredPointReducer desired({100.0f, 50.0f});
    geometry.adopt(candidate(), false);
    desired.adopt_geometry(geometry.snapshot(), false, true, false);
    require(geometry.snapshot().available, "R was not admitted");
    require(std::fabs(desired.snapshot().normalized.x - 0.5f) < 0.001f,
            "D did not initialize from the source point");

    pipeline_contract::IntentState intent{};
    intent.ads = true;
    intent.right_purpose =
        pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    intent.filtered_right = {1.0f, 0.0f};
    desired.reduce_manual(intent, geometry.snapshot(), true, false, 0.01f);
    require(desired.snapshot().normalized.x > 0.5f,
            "manual correction did not move D");
    require(std::fabs(geometry.snapshot().source_position.x - 50.0f) < 0.001f,
            "D correction rewrote source-owned R geometry");
}

void test_geometry_refresh_preserves_user_corrected_d() {
    controller_native::TargetGeometryReducer geometry;
    controller_native::DesiredPointReducer desired({100.0f, 50.0f});
    auto observed = candidate();
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, true, false);
    pipeline_contract::IntentState intent{};
    intent.ads = true;
    intent.right_purpose =
        pipeline_contract::UserAimIntentPurpose::CorrectCurrentTarget;
    intent.filtered_right = {1.0f, 0.0f};
    desired.reduce_manual(intent, geometry.snapshot(), true, false, 0.02f);
    const float corrected = desired.snapshot().normalized.x;

    const bool had_geometry = geometry.snapshot().available;
    observed.aim_px = {20.0f, 30.0f};
    geometry.adopt(observed, false);
    desired.adopt_geometry(geometry.snapshot(), false, false, had_geometry);
    require(std::fabs(desired.snapshot().normalized.x - corrected) < 0.001f,
            "fresh R geometry erased user-owned D");
}

}  // namespace

void register_target_state_reducers_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "geometry_and_desired_point_have_separate_state", test_geometry_and_desired_point_have_separate_state);
    registry.add_case("BaseBodyLock", "geometry_refresh_preserves_user_corrected_d", test_geometry_refresh_preserves_user_corrected_d);
}
