#include "sustained_aimlab_counterfactual.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace controller_native::sustained_aimlab;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ScenarioScript replay_fixture() {
    ScenarioScript script;
    script.seed = 77;
    script.hash = 770077;
    script.config.duration_ms = 240;
    script.config.inter_target_gap_ms = 0;
    TargetScript target;
    target.id = 1;
    target.initial_error_px = {40.0, 0.0};
    target.acquire_deadline_ms = 220;
    target.visible_radius_px = 10.0;
    for (int ms = 0; ms <= 240; ++ms) {
        target.observation_at_ms.push_back(ms);
        target.observation_noise_px.push_back({});
    }
    script.targets.push_back(target);
    return script;
}

Vec2d mixed_for(BranchPolicy policy, Vec2d manual, Vec2d ai) {
    switch (policy) {
    case BranchPolicy::ActualMix: return {manual.x + ai.x, manual.y + ai.y};
    case BranchPolicy::ManualOnly: return manual;
    case BranchPolicy::AiOnly: return ai;
    case BranchPolicy::ManualPlusAi25:
        return {manual.x + ai.x * 0.25, manual.y + ai.y * 0.25};
    case BranchPolicy::ManualPlusAi50:
        return {manual.x + ai.x * 0.50, manual.y + ai.y * 0.50};
    case BranchPolicy::ManualPlusAi75:
        return {manual.x + ai.x * 0.75, manual.y + ai.y * 0.75};
    }
    return {};
}

ReplayControllerFactory opposing_factory() {
    return [](const BranchSchedule& schedule) {
        return [schedule](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            output.bodylock_mode = input.target_present;
            output.shaped_assist_stick = {-0.35, 0.0};
            output.requested_assist_stick = output.shaped_assist_stick;
            const bool branch_active = input.now_ms >= schedule.start_ms &&
                input.now_ms < schedule.start_ms + schedule.duration_ms;
            const BranchPolicy policy = branch_active
                ? schedule.policy : BranchPolicy::ActualMix;
            output.final_stick = mixed_for(
                policy, input.manual_stick, output.shaped_assist_stick);
            return output;
        };
    };
}

ReplayRequest request_for(BranchPolicy policy) {
    ReplayRequest request;
    request.branch_at_ms = 20;
    request.substitution_ms = 80;
    request.horizon_ms = 120;
    request.policy = policy;
    return request;
}

void test_replay_matches_reference_before_branch() {
    const ReplayReference reference = record_reference(
        replay_fixture(), ManualProfile::Mixed,
        BenchmarkCohort::AdsAcquire, opposing_factory());

    const BranchResult branch = replay_branch(
        reference, request_for(BranchPolicy::ActualMix), opposing_factory());

    require(branch.prebranch_verified,
            "actual replay must match every prebranch frame");
    require(branch.first_divergence_ms == -1,
            "actual mix must not diverge from reference");
}

void test_manual_only_branch_can_beat_harmful_ai() {
    const ReplayReference reference = record_reference(
        replay_fixture(), ManualProfile::Mixed,
        BenchmarkCohort::AdsAcquire, opposing_factory());

    const BranchResult actual = replay_branch(
        reference, request_for(BranchPolicy::ActualMix), opposing_factory());
    const BranchResult manual = replay_branch(
        reference, request_for(BranchPolicy::ManualOnly), opposing_factory());

    require(manual.error_area_px_ms < actual.error_area_px_ms,
            "helpful manual-only branch must beat harmful mix");
    require(manual.first_divergence_ms == 20,
            "manual branch must first diverge at the scheduled tick");
}

void test_invalid_replay_window_is_rejected() {
    const ReplayReference reference = record_reference(
        replay_fixture(), ManualProfile::Mixed,
        BenchmarkCohort::AdsAcquire, opposing_factory());
    ReplayRequest invalid = request_for(BranchPolicy::ManualOnly);
    invalid.branch_at_ms = -1;
    bool threw = false;
    try {
        (void)replay_branch(reference, invalid, opposing_factory());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "negative branch time must be rejected");
}

}  // namespace

int main() {
    try {
        test_replay_matches_reference_before_branch();
        test_manual_only_branch_can_beat_harmful_ai();
        test_invalid_replay_window_is_rejected();
        std::cout << "cod_native_sustained_aimlab_counterfactual_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_counterfactual_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
