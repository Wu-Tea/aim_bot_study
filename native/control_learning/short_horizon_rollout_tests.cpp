#include "control_learning/short_horizon_rollout.h"

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
        using namespace control_learning;
        RolloutSnapshot snapshot;
        snapshot.decision_at_ns = 1'000;
        snapshot.latest_evidence_at_ns = 900;
        snapshot.mode = pipeline_contract::ControlMode::BodyLockFollow;
        snapshot.has_target = true;
        snapshot.single_strong_target = true;
        snapshot.error_px = {24.0, -8.0};
        snapshot.predicted_terminal_error_px = {18.0, -6.0};
        snapshot.target_velocity_px_per_sec = {90.0, 0.0};
        snapshot.shaped_ai = {0.20, -0.05};
        snapshot.manual = {0.02, 0.0};
        snapshot.scheduled_pending_px = {3.0, -1.0};
        snapshot.right_response.values = {{{900.0, 40.0}, {-20.0, 760.0}}};
        snapshot.response_confidence = 0.8f;
        snapshot.delay_confidence = 0.7f;
        const auto before = snapshot;
        const auto result = ShortHorizonRollout::evaluate(snapshot);
        require(snapshot == before, "rollout must be pure over immutable snapshot");
        require(result.valid, "confident target must produce candidate ranking");
        require(result.used_latest_timestamp_ns <= snapshot.decision_at_ns,
                "rollout must not consume future evidence");
        require(result.candidate_count == 5,
                "single strong target must evaluate bounded 1.15 hypothesis");
        require(result.best_scale >= 0.0f && result.best_scale <= 1.15f,
                "best scale outside fixed lattice");
        require(ShortHorizonRollout::evaluate(snapshot) == result,
                "rollout and tie breaking must be deterministic");

        auto ambiguous = snapshot;
        ambiguous.single_strong_target = false;
        const auto ambiguous_result = ShortHorizonRollout::evaluate(ambiguous);
        require(ambiguous_result.candidate_count == 4,
                "multi-target ambiguity must not receive aggressive candidate");

        auto low_confidence = snapshot;
        low_confidence.response_confidence = 0.05f;
        const auto fallback = ShortHorizonRollout::evaluate(low_confidence);
        require(!fallback.valid && std::fabs(fallback.best_scale - 1.0f) < 1.0e-6f,
                "low confidence must retain actual controller scale");

        auto escape = snapshot;
        escape.manual = {-0.8, 0.0};
        const auto escaped = ShortHorizonRollout::evaluate(escape);
        require(escaped.manual_escape && escaped.best_scale == 0.0f,
                "deliberate manual escape must suppress AI candidates");

        auto future = snapshot;
        future.latest_evidence_at_ns = future.decision_at_ns + 1;
        require(!ShortHorizonRollout::evaluate(future).valid,
                "future evidence must reject causal ranking");
        auto no_target = snapshot;
        no_target.has_target = false;
        require(!ShortHorizonRollout::evaluate(no_target).valid,
                "no-target snapshot must not rank actions");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[ShortHorizonRolloutTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
