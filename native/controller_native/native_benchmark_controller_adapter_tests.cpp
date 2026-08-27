#include "native_benchmark_controller_adapter.h"

#include <cmath>
#include <stdexcept>

namespace {
using controller_native::benchmark_adapter::snapshot_from;
using controller_native::sustained_aimlab::ControllerObservation;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 1.0e-5f) {
        throw std::runtime_error(message);
    }
}

void test_snapshot_uses_the_same_anatomical_ratio_as_the_simulator() {
    ControllerObservation input;
    input.target_present = true;
    input.primary_candidate_visible = true;
    input.target_id = 7;
    input.frame_id = 11;
    input.has_body_box = true;
    input.body_box_x = 100.0;
    input.body_box_y = 200.0;
    input.body_box_width = 80.0;
    input.body_box_height = 200.0;

    const auto snapshot = snapshot_from(input, 1.0, 0.30);
    require(snapshot.candidates.size() == 1,
            "benchmark snapshot must retain the target candidate");
    require_near(snapshot.candidates.front().aim_point_px.y, 260.0f,
                 "adapter changed the configured anatomical aim ratio");
}

void test_flat_geometry_keeps_the_shared_head_body_fallback() {
    ControllerObservation input;
    input.target_present = true;
    input.primary_candidate_visible = true;
    input.target_id = 8;
    input.frame_id = 12;
    input.has_body_box = true;
    input.body_box_x = 100.0;
    input.body_box_y = 200.0;
    input.body_box_width = 200.0;
    input.body_box_height = 100.0;

    const auto snapshot = snapshot_from(input, 1.0, 0.30);
    require_near(snapshot.candidates.front().aim_point_px.y, 240.0f,
                 "flat geometry must retain the shared 0.40 fallback");
}
}  // namespace

int main() {
    test_snapshot_uses_the_same_anatomical_ratio_as_the_simulator();
    test_flat_geometry_keeps_the_shared_head_body_fallback();
    return 0;
}
