#pragma once

#include "sustained_aimlab_simulator.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace controller_native::sustained_aimlab {

enum class AnchorKind : std::uint8_t {
    TargetReverse,
    TargetStop,
    JumpApex,
    FallTransition,
    ObservationLoss,
    ObservationRecovery,
    AdsSettled,
    AdsBodylockHandoff,
};

enum class ConflictKind : std::uint8_t {
    ManualAiOpposition,
    NearZeroCancellation,
    StallRing,
    DirectionReversal,
    CircleExit,
    FalseInterruption,
    WrongWayCommit,
    DestructiveStack,
};

struct EvaluationPoint {
    int absolute_ms = -1;
    int target_elapsed_ms = 0;
    std::uint64_t target_id = 0;
    AnchorKind kind = AnchorKind::TargetReverse;
};

struct ConflictEpisode {
    ConflictKind kind = ConflictKind::ManualAiOpposition;
    int start_ms = 0;
    int end_ms = 0;
    double severity = 0.0;
    int peak_ms = -1;
};

struct ConflictConfig {
    double minimum_manual = 0.03;
    double minimum_ai = 0.03;
    double opposing_cosine = -0.15;
    double cancellation_output = 0.04;
    int merge_gap_ms = 4;
};

std::vector<EvaluationPoint> generate_fixed_anchors(
    const ScenarioScript& script,
    const std::vector<SimulationTraceFrame>& trace = {});

std::vector<ConflictEpisode> detect_conflict_episodes(
    const std::vector<SimulationTraceFrame>& trace,
    const ConflictConfig& config);

std::vector<ConflictEpisode> select_episode_budget(
    std::vector<ConflictEpisode> episodes,
    std::size_t per_kind_limit);

}  // namespace controller_native::sustained_aimlab
