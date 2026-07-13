#include "ads_completion_gate.h"

#include <cstdlib>

namespace {

void require(bool condition) {
    if (!condition) std::abort();
}

controller_native::AdsCompletionGateInput sample(
    std::uint64_t sequence,
    float dx,
    double now,
    bool fresh = true) {
    controller_native::AdsCompletionGateInput input;
    input.aiming = true;
    input.has_target_authority = true;
    input.has_strong_target = true;
    input.fresh_observation = fresh;
    input.vision_sequence = sequence;
    input.dx = dx;
    input.now_seconds = now;
    return input;
}

void test_requires_distinct_fresh_centered_frames() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    require(gate.update(sample(1, 4.0f, 1.000)).active);
    require(gate.state().centered_fresh_frames == 1);
    require(gate.update(sample(1, 4.0f, 1.001)).active);
    require(gate.state().centered_fresh_frames == 1);
    require(gate.update(sample(2, 4.0f, 1.010, false)).active);
    require(gate.state().centered_fresh_frames == 1);
    require(gate.update(sample(3, 4.0f, 1.020)).active);
    require(!gate.update(sample(4, 4.0f, 1.030)).active);
    require(gate.state().reason == controller_native::AdsCompletionReason::Centered);
}

void test_outside_radius_resets_streak() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    gate.update(sample(1, 4.0f, 1.000));
    gate.update(sample(2, 9.0f, 1.010));
    require(gate.state().centered_fresh_frames == 0);
}

void test_timeout_releases_acquisition() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    require(gate.update(sample(1, 20.0f, 1.000)).active);
    require(!gate.update(sample(2, 20.0f, 1.221)).active);
    require(gate.state().reason == controller_native::AdsCompletionReason::Timeout);
}

void test_projected_samples_hold_active_without_advancing() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    require(gate.update(sample(1, 20.0f, 1.000)).active);
    auto projected = sample(2, 2.0f, 1.020, false);
    projected.has_strong_target = false;
    require(gate.update(projected).active);
    require(gate.state().centered_fresh_frames == 0);
}

void test_crossing_brake_blocks_centered_completion_until_released() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    auto carrying = sample(1, 4.0f, 1.000);
    carrying.crossing_brake_active = true;
    require(gate.update(carrying).active);
    carrying.vision_sequence = 2;
    carrying.now_seconds = 1.010;
    require(gate.update(carrying).active);
    carrying.vision_sequence = 3;
    carrying.now_seconds = 1.020;
    require(gate.update(carrying).active);
    require(gate.state().centered_fresh_frames == 0);

    auto released = sample(4, 4.0f, 1.030);
    require(gate.update(released).active);
    released.vision_sequence = 5;
    released.now_seconds = 1.040;
    require(gate.update(released).active);
    released.vision_sequence = 6;
    released.now_seconds = 1.050;
    require(!gate.update(released).active);
    require(gate.state().reason == controller_native::AdsCompletionReason::Centered);
}

void test_release_and_target_loss_reset() {
    controller_native::AdsCompletionGate gate(8.0f, 3, 220.0f);
    gate.update(sample(1, 4.0f, 1.000));
    auto released = sample(2, 4.0f, 1.010);
    released.aiming = false;
    require(!gate.update(released).active);
    require(gate.state().reason == controller_native::AdsCompletionReason::Released);

    gate.update(sample(3, 4.0f, 2.000));
    auto lost = sample(4, 4.0f, 2.010);
    lost.has_target_authority = false;
    lost.has_strong_target = false;
    require(!gate.update(lost).active);
    require(gate.state().reason == controller_native::AdsCompletionReason::TargetLost);
}

}  // namespace

int main() {
    test_requires_distinct_fresh_centered_frames();
    test_outside_radius_resets_streak();
    test_timeout_releases_acquisition();
    test_projected_samples_hold_active_without_advancing();
    test_crossing_brake_blocks_centered_completion_until_released();
    test_release_and_target_loss_reset();
    return 0;
}
