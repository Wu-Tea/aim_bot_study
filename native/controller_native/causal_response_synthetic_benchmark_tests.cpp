#include "causal_response_synthetic_benchmark.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        using namespace controller_native::causal_response;
        PlantConfig config;
        config.right_response.values = {{{900.0, 40.0}, {-20.0, 760.0}}};
        config.left_response.values = {{{-180.0, 0.0}, {0.0, -80.0}}};
        config.delay_ms = 45;
        const auto fixture = run_feedback_fixture(config, 1337);
        require(fixture.controls_depend_on_prior_error,
                "plant control must close the feedback loop");
        require(std::fabs(fixture.ground_truth_delay_ms - 45.0) < 1.0e-9,
                "plant must retain ground-truth delay");
        require(fixture.cohorts.size() >= 13,
                "all retained causal cohorts must be present");
        require(fixture.hindsight_headroom_px_ms > 0.0,
                "action lattice must expose non-trivial hindsight headroom");
        for (double headroom : fixture.hindsight_headroom_by_horizon_px_ms) {
            require(headroom > 0.0,
                    "40/80/160/250 ms hindsight windows must each expose headroom");
        }
        require(fixture.single_target_wrong_input_headroom_px_ms > 0.0,
                "single-target wrong-input cohort must expose AI counter-correction headroom");
        require(fixture.single_target_aggressive_gain_px_ms > 0.0,
                "single-target strong-AI hypothesis must have its own measurable gain");
        require(fixture.multi_target_conservative_regret_px_ms >= 0.0,
                "multi-target ambiguity must retain a separate regret measure");
        require(fixture.multi_target_aggressive_regret_px_ms > 0.0,
                "multi-target ambiguity must expose the cost of unconditional strong AI");
        require(fixture.rollout_decisions > 0,
                "causal rollout must rank retained decisions");
        require(fixture.rollout_top1_agreement > 0.0,
                "causal ranking must correlate with delayed oracle outcomes");
        require(fixture.rollout_causal_gain_px_ms > 0.0,
                "causal ranking must beat unchanged scale on retained plant");
        require(fixture.rollout_harmful_release_count == 0,
                "causal ranking must not increase harmful release");
        for (const auto& mutation : fixture.mutations) {
            require(mutation.detected, mutation.name.c_str());
        }
        require(fixture.maneuver_absolute_degradation_pp >= 1.0,
                "maneuver mutation must degrade by at least one percentage point");
        require(fixture.maneuver_relative_degradation_percent >= 5.0,
                "maneuver mutation must degrade by at least five percent");

        const auto repeated = run_feedback_fixture(config, 1337);
        require(to_json(fixture) == to_json(repeated),
                "fixed-seed benchmark must be byte deterministic");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[CausalResponseSyntheticBenchmarkTests] FAIL "
                  << error.what() << '\n';
        return 1;
    }
}
