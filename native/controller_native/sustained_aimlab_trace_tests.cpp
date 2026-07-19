#include "sustained_aimlab_trace.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace controller_native::sustained_aimlab;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_fixed_reverse_anchor_comes_from_script() {
    ScenarioScript script;
    TargetScript target;
    target.id = 9;
    target.motion = MotionProfile::Reverse;
    target.maneuver_at_ms = 120;
    script.targets.push_back(target);

    const auto anchors = generate_fixed_anchors(script);

    require(anchors.size() == 1, "reverse script must create one anchor");
    require(anchors.front().kind == AnchorKind::TargetReverse,
            "anchor must preserve maneuver kind");
    require(anchors.front().target_id == 9,
            "anchor must preserve target identity");
    require(anchors.front().target_elapsed_ms == 120,
            "anchor must preserve maneuver time");
}

void test_opposition_frames_merge_into_one_episode() {
    std::vector<SimulationTraceFrame> trace(12);
    for (int ms = 0; ms < 12; ++ms) {
        trace[ms].absolute_ms = ms;
        trace[ms].target_active = true;
        trace[ms].input.manual_stick = {0.20, 0.0};
        trace[ms].output.shaped_assist_stick = {-0.18, 0.0};
        trace[ms].output.final_stick = {0.10, 0.0};
    }

    const auto episodes = detect_conflict_episodes(trace, ConflictConfig{});

    require(episodes.size() == 1, "adjacent conflict frames must merge");
    require(episodes.front().kind == ConflictKind::ManualAiOpposition,
            "opposing vectors must retain their defect class");
    require(episodes.front().start_ms == 0 && episodes.front().end_ms == 11,
            "merged episode must preserve inclusive bounds");
}

void test_short_gap_is_merged_but_long_gap_is_not() {
    std::vector<SimulationTraceFrame> trace(20);
    for (int ms : {0, 1, 2, 6, 7, 8, 16, 17}) {
        trace[ms].absolute_ms = ms;
        trace[ms].target_active = true;
        trace[ms].input.manual_stick = {0.20, 0.0};
        trace[ms].output.shaped_assist_stick = {-0.20, 0.0};
        trace[ms].output.final_stick = {0.10, 0.0};
    }
    for (int ms = 0; ms < 20; ++ms) trace[ms].absolute_ms = ms;

    const auto episodes = detect_conflict_episodes(trace, ConflictConfig{});

    require(episodes.size() == 2,
            "four millisecond gap must merge while seven millisecond gap splits");
    require(episodes.front().start_ms == 0 && episodes.front().end_ms == 8,
            "first merged episode bounds");
    require(episodes.back().start_ms == 16 && episodes.back().end_ms == 17,
            "second episode bounds");
}

void test_merged_episode_preserves_peak_severity_time() {
    std::vector<SimulationTraceFrame> trace(8);
    for (int ms = 0; ms < 8; ++ms) {
        trace[ms].absolute_ms = ms;
        trace[ms].target_active = true;
        trace[ms].input.manual_stick = {0.10 + ms * 0.02, 0.0};
        trace[ms].output.shaped_assist_stick = {-0.10 - ms * 0.02, 0.0};
        trace[ms].output.final_stick = {0.10, 0.0};
    }

    const auto episodes = detect_conflict_episodes(trace, ConflictConfig{});

    require(episodes.size() == 1, "fixture must merge into one episode");
    require(episodes.front().peak_ms == 7,
            "merged episode must retain maximum-severity timestamp");
}

void test_budget_selection_is_severity_stable() {
    std::vector<ConflictEpisode> episodes{
        {ConflictKind::ManualAiOpposition, 20, 25, 1.0},
        {ConflictKind::ManualAiOpposition, 10, 15, 3.0},
        {ConflictKind::ManualAiOpposition, 30, 35, 2.0},
        {ConflictKind::StallRing, 5, 9, 0.5},
    };

    const auto selected = select_episode_budget(std::move(episodes), 2);

    require(selected.size() == 3, "budget applies independently per class");
    require(selected[0].start_ms == 10 && selected[1].start_ms == 30,
            "higher severity opposition episodes must win deterministically");
    require(selected[2].kind == ConflictKind::StallRing,
            "other classes retain their own budget");
}

void test_normal_vision_cadence_is_not_observation_loss() {
    ScenarioScript script;
    std::vector<SimulationTraceFrame> trace(20);
    for (int ms = 0; ms < 20; ++ms) {
        trace[ms].absolute_ms = ms;
        trace[ms].target_elapsed_ms = ms;
        trace[ms].target_active = true;
        trace[ms].target_id = 1;
        trace[ms].input.target_present = true;
        trace[ms].input.fresh_vision = ms == 0 || ms == 10;
        trace[ms].output.target_observed = trace[ms].input.fresh_vision;
    }

    const auto anchors = generate_fixed_anchors(script, trace);
    const auto observation_anchors = std::count_if(
        anchors.begin(), anchors.end(), [](const EvaluationPoint& point) {
            return point.kind == AnchorKind::ObservationLoss ||
                point.kind == AnchorKind::ObservationRecovery;
        });
    require(observation_anchors == 0,
            "normal frame cadence must not look like observation loss");
}

}  // namespace

int main() {
    try {
        test_fixed_reverse_anchor_comes_from_script();
        test_opposition_frames_merge_into_one_episode();
        test_short_gap_is_merged_but_long_gap_is_not();
        test_merged_episode_preserves_peak_severity_time();
        test_budget_selection_is_severity_stable();
        test_normal_vision_cadence_is_not_observation_loss();
        std::cout << "cod_native_sustained_aimlab_trace_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_trace_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
