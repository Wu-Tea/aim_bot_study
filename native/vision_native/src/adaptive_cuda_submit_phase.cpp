#include "vision_native/adaptive_cuda_submit_phase.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vision_native {
namespace {

constexpr std::array<std::uint32_t,
    AdaptiveCudaSubmitPhaseController::kMaxCandidateCount> kDefaultPhases{
    0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500};
constexpr std::uint32_t kDefaultBlockSamples = 6;
constexpr std::uint32_t kDefaultMinCadenceSamples = 4;
constexpr std::uint32_t kDefaultMinPeriodUs = 1000;
constexpr std::uint32_t kDefaultMaxPeriodUs = 20'000;
constexpr std::uint32_t kDefaultNextFrameGuardUs = 500;
// A short inactive pulse freezes the learned phase.  At 200 Hz this default
// is 50 source periods, so a real active/idle pulse cannot erase a partially
// completed candidate block.  A sustained idle interval re-enters exploration
// when active work resumes.
constexpr std::uint32_t kDefaultIdleResetAfterUs = 250'000;
// At 200 Hz, 100 blocks x 6 samples is about 3 seconds of held operation;
// the bounded candidate scan then occupies only one short adaptation epoch.
constexpr std::uint32_t kDefaultHeldReprobeAfterBlocks = 100;
constexpr std::uint32_t kDefaultCadenceTolerancePercent = 10;
constexpr std::uint32_t kDefaultCadenceChangePercent = 15;
constexpr float kDefaultResidualWeight = 0.50f;
constexpr float kDefaultTailWeight = 0.25f;
// Milliseconds charged per frame accumulated beyond the first.  The cap
// prevents a pathological DXGI backlog from dominating phase selection.
constexpr float kDefaultBacklogWeight = 0.35f;
constexpr float kDefaultResidualCapMs = 4.0f;
constexpr float kDefaultTailCapMs = 8.0f;
constexpr float kDefaultBacklogCapMs = 2.0f;
constexpr float kDefaultPromotionAbsoluteImprovementMs = 0.20f;
constexpr float kDefaultPromotionRelativeImprovement = 0.05f;
constexpr std::uint32_t kMinCadenceToleranceUs = 250;
constexpr std::uint32_t kMinCadenceChangeUs = 500;
constexpr float kEpsilon = 1.0e-5f;

float clamp_nonnegative(float value, float upper) noexcept {
    if (!std::isfinite(value) || value <= 0.0f) return 0.0f;
    return std::min(value, std::max(0.0f, upper));
}

std::uint32_t median_period_us(
    const std::array<std::uint32_t,
        AdaptiveCudaSubmitPhaseController::kCadenceWindowSize>& values,
    std::uint8_t count) noexcept {
    if (count == 0) return 0;
    std::array<std::uint32_t,
        AdaptiveCudaSubmitPhaseController::kCadenceWindowSize> sorted{};
    for (std::uint8_t i = 0; i < count; ++i) sorted[i] = values[i];
    std::sort(sorted.begin(), sorted.begin() + count);
    const std::uint8_t middle = static_cast<std::uint8_t>(count / 2);
    if ((count & 1u) != 0u) return sorted[middle];
    return (sorted[middle - 1] + sorted[middle]) / 2u;
}

}  // namespace

AdaptiveCudaSubmitPhaseController::Config
AdaptiveCudaSubmitPhaseController::default_config() noexcept {
    Config config{};
    config.candidate_phases_us = kDefaultPhases;
    config.candidate_count = static_cast<std::uint8_t>(kDefaultPhases.size());
    config.block_samples = static_cast<std::uint8_t>(kDefaultBlockSamples);
    config.min_cadence_samples = static_cast<std::uint8_t>(kDefaultMinCadenceSamples);
    config.min_source_period_us = kDefaultMinPeriodUs;
    config.max_source_period_us = kDefaultMaxPeriodUs;
    config.next_frame_guard_us = kDefaultNextFrameGuardUs;
    config.idle_reset_after_us = kDefaultIdleResetAfterUs;
    config.held_reprobe_after_blocks =
        static_cast<std::uint8_t>(kDefaultHeldReprobeAfterBlocks);
    config.cadence_tolerance_percent = kDefaultCadenceTolerancePercent;
    config.cadence_change_percent = kDefaultCadenceChangePercent;
    config.residual_weight = kDefaultResidualWeight;
    config.tail_weight = kDefaultTailWeight;
    config.backlog_weight = kDefaultBacklogWeight;
    config.residual_cap_ms = kDefaultResidualCapMs;
    config.tail_cap_ms = kDefaultTailCapMs;
    config.backlog_cap_ms = kDefaultBacklogCapMs;
    config.promotion_absolute_improvement_ms =
        kDefaultPromotionAbsoluteImprovementMs;
    config.promotion_relative_improvement = kDefaultPromotionRelativeImprovement;
    return config;
}

AdaptiveCudaSubmitPhaseController::AdaptiveCudaSubmitPhaseController()
    : AdaptiveCudaSubmitPhaseController(default_config()) {}

AdaptiveCudaSubmitPhaseController::AdaptiveCudaSubmitPhaseController(
    const Config& config)
    : config_(config) {
    sanitize_config();
    clear_adaptation(ResetReason::Explicit);
    cadence_.clear();
}

void AdaptiveCudaSubmitPhaseController::CandidateStats::clear() noexcept {
    costs.fill(0.0f);
    count = 0;
}

void AdaptiveCudaSubmitPhaseController::CandidateStats::add(
    float cost, std::uint8_t capacity) noexcept {
    if (count >= capacity || count >= kMaxBlockSamples ||
        !std::isfinite(cost)) {
        return;
    }
    costs[count++] = cost;
}

float AdaptiveCudaSubmitPhaseController::CandidateStats::robust_cost() const noexcept {
    if (count == 0) return std::numeric_limits<float>::infinity();
    std::array<float, kMaxBlockSamples> sorted = costs;
    std::sort(sorted.begin(), sorted.begin() + count);
    const std::uint8_t middle = static_cast<std::uint8_t>(count / 2);
    if ((count & 1u) != 0u) return sorted[middle];
    return (sorted[middle - 1] + sorted[middle]) * 0.5f;
}

void AdaptiveCudaSubmitPhaseController::CadenceState::clear() noexcept {
    intervals_us.fill(0);
    count = 0;
    next = 0;
    last_source_present_ns = 0;
    estimated_period_us = 0;
    stable = false;
}

void AdaptiveCudaSubmitPhaseController::sanitize_config() noexcept {
    if (config_.candidate_count == 0 ||
        config_.candidate_count > kMaxCandidateCount) {
        config_.candidate_count = static_cast<std::uint8_t>(kMaxCandidateCount);
    }
    if (config_.block_samples == 0 || config_.block_samples > kMaxBlockSamples) {
        config_.block_samples = static_cast<std::uint8_t>(kDefaultBlockSamples);
    }
    if (config_.min_cadence_samples == 0 ||
        config_.min_cadence_samples > kCadenceWindowSize) {
        config_.min_cadence_samples = static_cast<std::uint8_t>(kDefaultMinCadenceSamples);
    }
    if (config_.max_source_period_us == 0) {
        config_.max_source_period_us = kDefaultMaxPeriodUs;
    }
    if (config_.min_source_period_us == 0) {
        config_.min_source_period_us = kDefaultMinPeriodUs;
    }
    config_.min_source_period_us = std::clamp(
        config_.min_source_period_us, 1u, config_.max_source_period_us);
    if (config_.max_source_period_us < config_.min_source_period_us) {
        config_.max_source_period_us = kDefaultMaxPeriodUs;
        config_.min_source_period_us = std::min(
            config_.min_source_period_us, config_.max_source_period_us);
    }
    config_.next_frame_guard_us = std::min(config_.next_frame_guard_us, 5000u);
    if (config_.idle_reset_after_us == 0) {
        config_.idle_reset_after_us = kDefaultIdleResetAfterUs;
    }
    if (config_.held_reprobe_after_blocks == 0) {
        config_.held_reprobe_after_blocks =
            static_cast<std::uint8_t>(kDefaultHeldReprobeAfterBlocks);
    }
    config_.cadence_tolerance_percent = std::clamp(
        config_.cadence_tolerance_percent, 1u, 50u);
    config_.cadence_change_percent = std::clamp(
        config_.cadence_change_percent, 1u, 75u);
    if (!std::isfinite(config_.residual_weight) || config_.residual_weight < 0.0f) {
        config_.residual_weight = kDefaultResidualWeight;
    }
    if (!std::isfinite(config_.tail_weight) || config_.tail_weight < 0.0f) {
        config_.tail_weight = kDefaultTailWeight;
    }
    if (!std::isfinite(config_.backlog_weight) || config_.backlog_weight < 0.0f) {
        config_.backlog_weight = kDefaultBacklogWeight;
    }
    if (!std::isfinite(config_.residual_cap_ms) || config_.residual_cap_ms <= 0.0f) {
        config_.residual_cap_ms = kDefaultResidualCapMs;
    }
    if (!std::isfinite(config_.tail_cap_ms) || config_.tail_cap_ms <= 0.0f) {
        config_.tail_cap_ms = kDefaultTailCapMs;
    }
    if (!std::isfinite(config_.backlog_cap_ms) || config_.backlog_cap_ms <= 0.0f) {
        config_.backlog_cap_ms = kDefaultBacklogCapMs;
    }
    if (!std::isfinite(config_.promotion_absolute_improvement_ms) ||
        config_.promotion_absolute_improvement_ms < 0.0f) {
        config_.promotion_absolute_improvement_ms =
            kDefaultPromotionAbsoluteImprovementMs;
    }
    if (!std::isfinite(config_.promotion_relative_improvement) ||
        config_.promotion_relative_improvement < 0.0f ||
        config_.promotion_relative_improvement >= 1.0f) {
        config_.promotion_relative_improvement = kDefaultPromotionRelativeImprovement;
    }

    for (std::uint8_t i = 0; i < config_.candidate_count; ++i) {
        config_.candidate_phases_us[i] = std::min(
            config_.candidate_phases_us[i], 5000u);
    }
    std::sort(
        config_.candidate_phases_us.begin(),
        config_.candidate_phases_us.begin() + config_.candidate_count);
    if (config_.candidate_phases_us[0] != 0) {
        config_.candidate_phases_us[0] = 0;
        std::sort(
            config_.candidate_phases_us.begin(),
            config_.candidate_phases_us.begin() + config_.candidate_count);
    }
}

void AdaptiveCudaSubmitPhaseController::clear_adaptation(
    ResetReason reason) noexcept {
    for (auto& stats : candidate_stats_) stats.clear();
    mode_ = Mode::Fallback;
    last_reset_reason_ = reason;
    active_candidate_index_ = 0;
    active_candidate_samples_ = 0;
    held_phase_us_ = 0;
    held_observation_count_ = 0;
    exploration_complete_ = false;
    ++adaptation_epoch_;
}

void AdaptiveCudaSubmitPhaseController::reset(ResetReason reason) noexcept {
    clear_adaptation(reason);
    if (reason == ResetReason::InvalidObservation ||
        reason == ResetReason::CadenceChanged) {
        cadence_.clear();
        last_observed_source_present_ns_ = 0;
    }
}

bool AdaptiveCudaSubmitPhaseController::update_identity(
    const FrameContext& frame) noexcept {
    const bool context_lost = have_context_ && !frame.context_id_available;
    const bool context_changed = have_context_ && frame.context_id_available &&
        context_id_ != frame.context_id;
    const bool regime_lost = have_regime_ && !frame.regime_id_available;
    const bool regime_changed = have_regime_ && frame.regime_id_available &&
        regime_id_ != frame.regime_id;
    const bool changed = context_lost || context_changed || regime_lost || regime_changed;

    have_context_ = frame.context_id_available;
    if (have_context_) context_id_ = frame.context_id;
    have_regime_ = frame.regime_id_available;
    if (have_regime_) regime_id_ = frame.regime_id;
    // Active is a workload sample, not an identity or regime key.  Short
    // active/idle edges are handled by the bounded idle freeze in recommend().

    if (changed) {
        clear_adaptation(regime_lost || regime_changed ?
            ResetReason::RegimeChanged : ResetReason::ContextChanged);
    }
    return changed;
}

bool AdaptiveCudaSubmitPhaseController::update_observation_identity(
    const CompletedObservation& observation) noexcept {
    FrameContext frame{};
    frame.active = observation.active;
    frame.context_id = observation.context_id;
    frame.context_id_available = observation.context_id_available;
    frame.regime_id = observation.regime_id;
    frame.regime_id_available = observation.regime_id_available;
    return update_identity(frame);
}

bool AdaptiveCudaSubmitPhaseController::update_cadence(
    std::uint64_t source_present_ns,
    std::uint32_t accumulated_frames,
    DecisionReason* failure_reason) noexcept {
    if (source_present_ns == 0) {
        if (failure_reason != nullptr) *failure_reason = DecisionReason::InvalidSourceTimestamp;
        cadence_.clear();
        clear_adaptation(ResetReason::InvalidObservation);
        return false;
    }
    if (cadence_.last_source_present_ns != 0) {
        if (source_present_ns <= cadence_.last_source_present_ns) {
            if (failure_reason != nullptr) *failure_reason = DecisionReason::InvalidSourceTimestamp;
            cadence_.clear();
            cadence_.last_source_present_ns = source_present_ns;
            clear_adaptation(ResetReason::InvalidObservation);
            return false;
        }
        const std::uint64_t delta_ns = source_present_ns - cadence_.last_source_present_ns;
        const std::uint64_t source_frames = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(accumulated_frames));
        const std::uint64_t divisor = source_frames * 1000ull;
        // LastPresentTime advances to the newest source present while DXGI's
        // AccumulatedFrames reports how many presents contributed to that
        // delta.  Measure the underlying game cadence, not the slower cadence
        // at which this consumer happened to acquire frames.
        const std::uint64_t delta_us = (delta_ns + divisor / 2ull) / divisor;
        if (delta_us < config_.min_source_period_us ||
            delta_us > config_.max_source_period_us) {
            if (failure_reason != nullptr) *failure_reason = DecisionReason::UnstableCadence;
            cadence_.clear();
            cadence_.last_source_present_ns = source_present_ns;
            clear_adaptation(ResetReason::CadenceChanged);
            return false;
        }
        cadence_.intervals_us[cadence_.next] = static_cast<std::uint32_t>(delta_us);
        cadence_.next = static_cast<std::uint8_t>(
            (cadence_.next + 1) % kCadenceWindowSize);
        cadence_.count = std::min<std::uint8_t>(
            static_cast<std::uint8_t>(cadence_.count + 1),
            static_cast<std::uint8_t>(kCadenceWindowSize));
    }
    cadence_.last_source_present_ns = source_present_ns;
    if (cadence_.count < config_.min_cadence_samples) {
        cadence_.stable = false;
        cadence_.estimated_period_us = 0;
        if (failure_reason != nullptr) *failure_reason = DecisionReason::InsufficientCadence;
        return false;
    }

    const std::uint32_t old_period = cadence_.estimated_period_us;
    const std::uint32_t period = median_period_us(cadence_.intervals_us, cadence_.count);
    const std::uint32_t tolerance = std::max(
        kMinCadenceToleranceUs,
        period * config_.cadence_tolerance_percent / 100u);
    std::uint32_t min_interval = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t max_interval = 0;
    for (std::uint8_t i = 0; i < cadence_.count; ++i) {
        min_interval = std::min(min_interval, cadence_.intervals_us[i]);
        max_interval = std::max(max_interval, cadence_.intervals_us[i]);
    }
    if (old_period != 0) {
        const std::uint32_t change_limit = std::max(
            kMinCadenceChangeUs,
            old_period * config_.cadence_change_percent / 100u);
        if (period > old_period + change_limit ||
            old_period > period + change_limit) {
            cadence_.clear();
            cadence_.last_source_present_ns = source_present_ns;
            clear_adaptation(ResetReason::CadenceChanged);
            if (failure_reason != nullptr) *failure_reason = DecisionReason::CadenceChanged;
            return false;
        }
    }
    cadence_.estimated_period_us = period;
    cadence_.stable = max_interval - min_interval <= tolerance;
    if (!cadence_.stable) {
        clear_adaptation(ResetReason::CadenceChanged);
        if (failure_reason != nullptr) *failure_reason = DecisionReason::UnstableCadence;
        return false;
    }
    return true;
}

std::uint32_t AdaptiveCudaSubmitPhaseController::maximum_safe_phase_us() const noexcept {
    if (!cadence_.stable || cadence_.estimated_period_us <= config_.next_frame_guard_us) {
        return 0;
    }
    return std::min<std::uint32_t>(
        5000u, cadence_.estimated_period_us - config_.next_frame_guard_us);
}

std::uint32_t AdaptiveCudaSubmitPhaseController::effective_candidate_phase_us(
    std::uint8_t index) const noexcept {
    if (index >= config_.candidate_count) return 0;
    return std::min(config_.candidate_phases_us[index], maximum_safe_phase_us());
}

int AdaptiveCudaSubmitPhaseController::candidate_index_for_phase(
    std::uint32_t phase_us) const noexcept {
    for (std::uint8_t i = 0; i < config_.candidate_count; ++i) {
        if (effective_candidate_phase_us(i) == phase_us) return i;
    }
    return -1;
}

AdaptiveCudaSubmitPhaseController::Decision
AdaptiveCudaSubmitPhaseController::fallback(DecisionReason reason) const noexcept {
    Decision decision{};
    decision.estimated_period_us = cadence_.estimated_period_us;
    decision.adaptation_epoch = adaptation_epoch_;
    decision.mode = Mode::Fallback;
    decision.reason = reason;
    decision.cadence_stable = cadence_.stable;
    decision.candidate_index = 0;
    decision.block_sample_count = active_candidate_samples_;
    return decision;
}

void AdaptiveCudaSubmitPhaseController::start_exploration_if_ready() noexcept {
    if (!cadence_.stable || mode_ == Mode::Held || exploration_complete_) return;
    if (maximum_safe_phase_us() == 0) {
        mode_ = Mode::Fallback;
        return;
    }
    mode_ = Mode::Exploring;
    while (active_candidate_index_ > 0 &&
           effective_candidate_phase_us(active_candidate_index_) ==
               effective_candidate_phase_us(active_candidate_index_ - 1)) {
        ++active_candidate_index_;
    }
    if (active_candidate_index_ >= config_.candidate_count) {
        finish_exploration();
    }
}

void AdaptiveCudaSubmitPhaseController::advance_candidate() noexcept {
    ++active_candidate_index_;
    active_candidate_samples_ = 0;
    while (active_candidate_index_ < config_.candidate_count &&
           effective_candidate_phase_us(active_candidate_index_) ==
               effective_candidate_phase_us(active_candidate_index_ - 1)) {
        ++active_candidate_index_;
    }
    if (active_candidate_index_ >= config_.candidate_count) finish_exploration();
}

void AdaptiveCudaSubmitPhaseController::finish_exploration() noexcept {
    const float natural_cost = candidate_stats_[0].robust_cost();
    std::uint8_t best_index = 0;
    float best_cost = natural_cost;
    for (std::uint8_t i = 1; i < config_.candidate_count; ++i) {
        if (effective_candidate_phase_us(i) ==
                effective_candidate_phase_us(i - 1) ||
            candidate_stats_[i].count < config_.block_samples) {
            continue;
        }
        const float cost = candidate_stats_[i].robust_cost();
        if (cost + kEpsilon < best_cost ||
            (std::fabs(cost - best_cost) <= kEpsilon &&
             config_.candidate_phases_us[i] < config_.candidate_phases_us[best_index])) {
            best_index = i;
            best_cost = cost;
        }
    }
    const bool natural_valid = std::isfinite(natural_cost) &&
        candidate_stats_[0].count >= config_.block_samples;
    const bool material_absolute = natural_valid &&
        best_cost + config_.promotion_absolute_improvement_ms < natural_cost;
    const bool material_relative = natural_valid && natural_cost > 0.0f &&
        best_cost <= natural_cost *
            (1.0f - config_.promotion_relative_improvement);
    if (best_index != 0 && material_absolute && material_relative) {
        held_phase_us_ = effective_candidate_phase_us(best_index);
        mode_ = held_phase_us_ == 0 ? Mode::Fallback : Mode::Held;
    } else {
        held_phase_us_ = 0;
        mode_ = Mode::Fallback;
    }
    active_candidate_index_ = best_index;
    active_candidate_samples_ = config_.block_samples;
    exploration_complete_ = true;
}

AdaptiveCudaSubmitPhaseController::Decision
AdaptiveCudaSubmitPhaseController::recommend(const FrameContext& frame) noexcept {
    (void)update_identity(frame);
    // Inactive keep-warm capture is intentionally not a workload sample.  It
    // may run much slower than the game, so feeding those intervals into the
    // cadence window would erase a partially learned phase.  Rebase the last
    // source timestamp so the first active interval is still local.
    if (!frame.active) {
        if (frame.source_present_steady_available &&
            frame.source_present_steady_ns != 0) {
            cadence_.last_source_present_ns = frame.source_present_steady_ns;
            if (idle_since_source_present_ns_ == 0 ||
                frame.source_present_steady_ns < idle_since_source_present_ns_) {
                idle_since_source_present_ns_ = frame.source_present_steady_ns;
            }
            const std::uint64_t idle_elapsed_ns =
                frame.source_present_steady_ns - idle_since_source_present_ns_;
            const std::uint64_t idle_limit_ns =
                static_cast<std::uint64_t>(config_.idle_reset_after_us) * 1000ull;
            if (!idle_reset_applied_ && idle_elapsed_ns >= idle_limit_ns) {
                clear_adaptation(ResetReason::RegimeChanged);
                idle_reset_applied_ = true;
                return fallback(DecisionReason::IdleTimeout);
            }
        }
        return fallback(DecisionReason::IdleFrozen);
    }
    idle_since_source_present_ns_ = 0;
    idle_reset_applied_ = false;
    if (!frame.source_present_steady_available ||
        frame.source_present_steady_ns == 0) {
        cadence_.clear();
        clear_adaptation(ResetReason::InvalidObservation);
        return fallback(DecisionReason::MissingSourceTimestamp);
    }
    DecisionReason failure_reason = DecisionReason::NaturalFallback;
    if (!update_cadence(
            frame.source_present_steady_ns,
            frame.accumulated_frames,
            &failure_reason)) {
        return fallback(failure_reason);
    }
    if (maximum_safe_phase_us() == 0) return fallback(DecisionReason::NoPhaseRoom);
    start_exploration_if_ready();
    Decision decision{};
    decision.estimated_period_us = cadence_.estimated_period_us;
    decision.adaptation_epoch = adaptation_epoch_;
    decision.cadence_stable = cadence_.stable;
    decision.candidate_index = active_candidate_index_;
    decision.block_sample_count = active_candidate_samples_;
    if (mode_ == Mode::Held) {
        decision.phase_us = held_phase_us_;
        decision.mode = Mode::Held;
        decision.reason = DecisionReason::HeldCandidate;
    } else if (mode_ == Mode::Exploring) {
        decision.phase_us = effective_candidate_phase_us(active_candidate_index_);
        decision.mode = Mode::Exploring;
        decision.reason = DecisionReason::Exploring;
    } else {
        decision.mode = Mode::Fallback;
        decision.reason = DecisionReason::NaturalFallback;
    }
    return decision;
}

float AdaptiveCudaSubmitPhaseController::objective_cost(
    const CompletedObservation& observation,
    const Config& config) noexcept {
    if (!std::isfinite(observation.cuda_submit_wait_ms) ||
        !std::isfinite(observation.output_wait_ms) ||
        !std::isfinite(observation.sync_queue_residual_ms) ||
        !std::isfinite(observation.tail_latency_ms)) {
        return std::numeric_limits<float>::infinity();
    }
    const float explicit_wait = std::max(0.0f, observation.cuda_submit_wait_ms);
    const float output_wait = std::max(0.0f, observation.output_wait_ms);
    const float residual = clamp_nonnegative(
        observation.sync_queue_residual_ms, config.residual_cap_ms);
    const float tail = clamp_nonnegative(
        observation.tail_latency_ms, config.tail_cap_ms);
    const std::uint32_t backlog_frames = observation.accumulated_frames > 1
        ? observation.accumulated_frames - 1
        : 0;
    const float backlog = std::min(
        config.backlog_cap_ms,
        static_cast<float>(backlog_frames) * config.backlog_weight);
    return explicit_wait + output_wait +
        config.residual_weight * residual + config.tail_weight * tail + backlog;
}

void AdaptiveCudaSubmitPhaseController::observe(
    const CompletedObservation& observation) noexcept {
    if (!observation.source_present_steady_available ||
        observation.source_present_steady_ns == 0 ||
        !observation.completed || !observation.timing_confident) {
        clear_adaptation(ResetReason::InvalidObservation);
        return;
    }
    if (last_observed_source_present_ns_ != 0 &&
        observation.source_present_steady_ns <= last_observed_source_present_ns_) {
        reset(ResetReason::InvalidObservation);
        return;
    }
    last_observed_source_present_ns_ = observation.source_present_steady_ns;
    if (update_observation_identity(observation)) return;
    // Inactive frames intentionally do not contribute timing samples.  This
    // preserves a partially completed block across short idle pulses.
    if (!observation.active) return;
    const float cost = objective_cost(observation, config_);
    if (!std::isfinite(cost)) {
        clear_adaptation(ResetReason::InvalidObservation);
        return;
    }
    if (mode_ == Mode::Held ||
        (mode_ == Mode::Fallback && exploration_complete_)) {
        ++held_observation_count_;
        const std::uint32_t reprobe_samples =
            static_cast<std::uint32_t>(config_.held_reprobe_after_blocks) *
            static_cast<std::uint32_t>(config_.block_samples);
        if (held_observation_count_ >= reprobe_samples) {
            clear_adaptation(ResetReason::PeriodicReprobe);
        }
        return;
    }
    if (mode_ != Mode::Exploring) return;
    const int candidate = candidate_index_for_phase(observation.applied_phase_us);
    if (candidate < 0 || static_cast<std::uint8_t>(candidate) != active_candidate_index_) {
        return;
    }
    candidate_stats_[candidate].add(cost, config_.block_samples);
    active_candidate_samples_ = candidate_stats_[candidate].count;
    if (active_candidate_samples_ >= config_.block_samples) advance_candidate();
}

AdaptiveCudaSubmitPhaseController::Snapshot
AdaptiveCudaSubmitPhaseController::snapshot() const noexcept {
    Snapshot value{};
    value.mode = mode_;
    value.held_phase_us = held_phase_us_;
    value.estimated_period_us = cadence_.estimated_period_us;
    value.active_candidate_index = active_candidate_index_;
    value.active_candidate_samples = active_candidate_samples_;
    value.adaptation_epoch = adaptation_epoch_;
    value.cadence_stable = cadence_.stable;
    return value;
}

const char* adaptive_phase_mode_name(
    AdaptiveCudaSubmitPhaseController::Mode mode) noexcept {
    switch (mode) {
    case AdaptiveCudaSubmitPhaseController::Mode::Exploring: return "exploring";
    case AdaptiveCudaSubmitPhaseController::Mode::Held: return "held";
    case AdaptiveCudaSubmitPhaseController::Mode::Fallback:
    default: return "fallback";
    }
}

const char* adaptive_phase_reason_name(
    AdaptiveCudaSubmitPhaseController::DecisionReason reason) noexcept {
    switch (reason) {
    case AdaptiveCudaSubmitPhaseController::DecisionReason::MissingSourceTimestamp:
        return "missing_source_timestamp";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::InvalidSourceTimestamp:
        return "invalid_source_timestamp";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::InsufficientCadence:
        return "insufficient_cadence";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::UnstableCadence:
        return "unstable_cadence";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::CadenceChanged:
        return "cadence_changed";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::IdleFrozen:
        return "idle_frozen";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::IdleTimeout:
        return "idle_timeout";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::NoPhaseRoom:
        return "no_phase_room";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::Exploring:
        return "exploring";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::HeldCandidate:
        return "held_candidate";
    case AdaptiveCudaSubmitPhaseController::DecisionReason::NaturalFallback:
    default: return "natural_fallback";
    }
}

}  // namespace vision_native
