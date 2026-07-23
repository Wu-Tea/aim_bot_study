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
    case BranchPolicy::Neutral: return {};
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

ScenarioScript stationary_fixture(
    std::uint64_t target_id, Vec2d error, double visible_radius = 10.0) {
    ScenarioScript script = replay_fixture();
    script.targets.front().id = target_id;
    script.targets.front().initial_error_px = error;
    script.targets.front().visible_radius_px = visible_radius;
    return script;
}

ReplayControllerFactory constant_ai_factory(Vec2d ai) {
    return [ai](const BranchSchedule& schedule) {
        return [ai, schedule](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            output.bodylock_mode = input.target_present;
            output.shaped_assist_stick = ai;
            output.requested_assist_stick = ai;
            const bool active = input.now_ms >= schedule.start_ms &&
                input.now_ms < schedule.start_ms + schedule.duration_ms;
            output.final_stick = mixed_for(
                active ? schedule.policy : BranchPolicy::ActualMix,
                input.manual_stick, ai);
            return output;
        };
    };
}

ReplayControllerFactory constant_components_factory(Vec2d manual, Vec2d ai) {
    return [manual, ai](const BranchSchedule& schedule) {
        return [manual, ai, schedule](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            output.bodylock_mode = input.target_present;
            output.shaped_assist_stick = ai;
            output.requested_assist_stick = ai;
            const bool active = input.now_ms >= schedule.start_ms &&
                input.now_ms < schedule.start_ms + schedule.duration_ms;
            output.final_stick = mixed_for(
                active ? schedule.policy : BranchPolicy::ActualMix,
                manual, ai);
            return output;
        };
    };
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

ScenarioScript jump_then_fall_fixture() {
    ScenarioScript script;
    script.seed = 88;
    script.hash = 880088;
    script.config.duration_ms = 700;
    TargetScript target;
    target.id = 8;
    target.motion = MotionProfile::JumpFall;
    target.initial_error_px = {0.0, -10.0};
    target.initial_velocity_px_per_second = {0.0, -100.0};
    target.acceleration_px_per_second_squared = {0.0, 500.0};
    target.acquire_deadline_ms = 650;
    target.visible_radius_px = 8.0;
    for (int ms = 0; ms <= 700; ++ms) {
        target.observation_at_ms.push_back(ms);
        target.observation_noise_px.push_back({});
    }
    script.targets.push_back(target);
    return script;
}

ReplayControllerFactory jump_factory() {
    return [](const BranchSchedule& schedule) {
        return [schedule](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            output.bodylock_mode = input.target_present;
            const Vec2d ai = input.now_ms >= 190 && input.now_ms < 310
                ? Vec2d{0.0, 1.0} : Vec2d{};
            output.shaped_assist_stick = ai;
            output.requested_assist_stick = ai;
            const bool branch_active = input.now_ms >= schedule.start_ms &&
                input.now_ms < schedule.start_ms + schedule.duration_ms;
            const BranchPolicy policy = branch_active
                ? schedule.policy : BranchPolicy::ActualMix;
            output.final_stick = mixed_for(policy, input.manual_stick, ai);
            return output;
        };
    };
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
    require(branch.trace.size() == 140,
            "branch replay must stop at branch plus horizon");
}

void test_replay_preserves_full_speed_left_strafe_before_branch() {
    ScenarioScript script = replay_fixture();
    auto& strafe = script.targets.front().player_strafe;
    strafe.initial_direction = 1;
    strafe.onset_ms = 0;
    strafe.reverse_ms = 100;
    strafe.release_ms = 200;
    strafe.top_speed_px_per_second = 180.0;
    strafe.time_constant_ms = 120.0;
    const ReplayReference reference = record_reference(
        script, ManualProfile::Pure, BenchmarkCohort::AdsAcquire,
        constant_ai_factory({0.0, 0.0}), PlayerStrafeMode::FullReversal);
    ReplayRequest request = request_for(BranchPolicy::ActualMix);
    request.branch_at_ms = 150;
    const BranchResult branch = replay_branch(
        reference, request, constant_ai_factory({0.0, 0.0}));

    require(reference.player_strafe_mode == PlayerStrafeMode::FullReversal,
            "reference must retain player strafe mode");
    require(reference.trace[20].input.left_x == 1.0 &&
                reference.trace[120].input.left_x == -1.0,
            "reference trace must contain full-speed reversal");
    require(branch.prebranch_verified,
            "replay must preserve left input and player plant before branch");
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

void test_analyzer_selects_manual_and_reports_regret() {
    const ReplayReference reference = record_reference(
        replay_fixture(), ManualProfile::Mixed,
        BenchmarkCohort::AdsAcquire, opposing_factory());
    AnalysisBudget budget;
    budget.substitution_ms = 80;
    budget.stable_horizon_ms = 160;

    const CounterfactualEpisode result = analyze_episode(
        reference, 20, opposing_factory(), budget);

    require(result.hindsight_oracle.policy == BranchPolicy::ManualOnly,
            "manual-only must be the realized lower-cost branch");
    require(result.causal_oracle.policy == BranchPolicy::ManualOnly,
            "causal oracle must recognize current manual closing direction");
    require(result.actual.regret_80_px_ms > 0.0,
            "harmful production mix must accumulate short-horizon regret");
    require(result.actual.manual_helped_but_suppressed_ms > 0,
            "manual suppression must be attributed explicitly");
}

void test_jump_up_action_can_help_locally_but_hurt_globally() {
    const ReplayReference reference = record_reference(
        jump_then_fall_fixture(), ManualProfile::Pure,
        BenchmarkCohort::AdsAcquire, jump_factory());
    AnalysisBudget budget;
    budget.substitution_ms = 120;
    budget.stable_horizon_ms = 500;

    const CounterfactualEpisode result = analyze_episode(
        reference, 190, jump_factory(), budget);

    require(result.actual.instant_progress_px > 0.0,
            "upward action must initially close jump error");
    require(result.actual.future_burden_px_ms > 0.0,
            "continued upward action must add fall correction burden");
    require(result.actual.classification ==
                OutcomeClass::LocalHelpfulGlobalHarmful,
            "fixture must expose local/global disagreement");
    require(result.hindsight_oracle.error_area_px_ms <=
                result.causal_oracle.error_area_px_ms,
            "hindsight must remain a lower bound");
}

void test_ai_only_can_be_the_correct_conflict_branch() {
    const ReplayReference reference = record_reference(
        stationary_fixture(2, {40.0, 0.0}), ManualProfile::Mixed,
        BenchmarkCohort::AdsAcquire, constant_ai_factory({0.35, 0.0}));
    AnalysisBudget budget;
    budget.substitution_ms = 60;
    budget.stable_horizon_ms = 160;

    const CounterfactualEpisode result = analyze_episode(
        reference, 90, constant_ai_factory({0.35, 0.0}), budget);

    require(result.hindsight_oracle.policy == BranchPolicy::AiOnly,
            "correct AI must beat temporarily wrong manual input");
    require(result.actual.ai_helped_but_suppressed_ms > 0,
            "AI suppression must be attributed explicitly");
}

void test_neutral_branch_exposes_when_manual_and_ai_are_both_harmful() {
    const ReplayReference reference = record_reference(
        stationary_fixture(2, {40.0, 0.0}), ManualProfile::Mixed,
        BenchmarkCohort::AdsAcquire, constant_ai_factory({-0.20, 0.0}));
    AnalysisBudget budget;
    budget.substitution_ms = 60;
    budget.stable_horizon_ms = 160;

    const CounterfactualEpisode result = analyze_episode(
        reference, 90, constant_ai_factory({-0.20, 0.0}), budget);

    require(result.hindsight_oracle.policy == BranchPolicy::Neutral,
            "no input must beat two wrong-way isolated components");
    require(result.actual.both_harmful_ms > 0,
            "both-harmful duration must be recorded");
}

void test_same_direction_stack_can_be_destructive() {
    const auto factory = constant_components_factory(
        {0.25, 0.0}, {0.75, 0.0});
    const ReplayReference reference = record_reference(
        stationary_fixture(1, {10.0, 0.0}, 1.0), ManualProfile::Pure,
        BenchmarkCohort::AdsAcquire, factory);
    AnalysisBudget budget;
    budget.substitution_ms = 80;
    budget.stable_horizon_ms = 160;

    const CounterfactualEpisode result = analyze_episode(
        reference, 0, factory, budget);

    require(result.actual.destructive_stack_ms > 0,
            "same-direction overshoot must be attributed as destructive stacking");
}

}  // namespace

int main() {
    try {
        test_replay_matches_reference_before_branch();
        test_replay_preserves_full_speed_left_strafe_before_branch();
        test_manual_only_branch_can_beat_harmful_ai();
        test_invalid_replay_window_is_rejected();
        test_analyzer_selects_manual_and_reports_regret();
        test_jump_up_action_can_help_locally_but_hurt_globally();
        test_ai_only_can_be_the_correct_conflict_branch();
        test_neutral_branch_exposes_when_manual_and_ai_are_both_harmful();
        test_same_direction_stack_can_be_destructive();
        std::cout << "cod_native_sustained_aimlab_counterfactual_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_counterfactual_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
