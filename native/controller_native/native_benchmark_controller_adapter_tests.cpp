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

void test_same_generation_cue_has_no_observed_candidate() {
    ControllerObservation input;
    input.target_present = true;
    input.primary_candidate_visible = false;
    input.target_id = 19;
    input.selector_target_generation = 73;
    input.frame_id = 31;
    input.cue_continuation = true;
    input.observed_error_px = {4.0, -6.0};

    const auto snapshot = snapshot_from(input, 2.0, 0.30);
    require(snapshot.selected_observation_id == 0,
            "cue must not impersonate a direct selected observation");
    require(snapshot.selector_target_generation == 73,
            "cue must preserve selector-owned generation identity");
    require(snapshot.candidates.empty(),
            "cue must not manufacture an observed person candidate");
    require(snapshot.enemy_cue_current &&
                snapshot.enemy_identity_confirmed &&
                snapshot.state.has_target &&
                snapshot.state.aim_authority,
            "cue must publish bounded same-generation aim continuity");
    require_near(snapshot.state.dx, 4.0f,
                 "cue changed the held horizontal position");
    require_near(snapshot.state.dy, -6.0f,
                 "cue changed the held vertical position");
}
}  // namespace

int main() {
    test_snapshot_uses_the_same_anatomical_ratio_as_the_simulator();
    test_flat_geometry_keeps_the_shared_head_body_fallback();
    test_same_generation_cue_has_no_observed_candidate();
    return 0;
}
