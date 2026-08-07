#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vision_native {

// Pure, single-threaded policy for choosing the phase at which a completed
// source-present frame may submit CUDA work. The owner calls recommend() once
// for the current frame and observe() later for the completed measurement of a
// prior frame. No clock, CUDA, runtime-config or file-I/O dependency belongs in
// this component.
class AdaptiveCudaSubmitPhaseController {
public:
    static constexpr std::size_t kMaxCandidateCount = 10;
    static constexpr std::size_t kCadenceWindowSize = 8;
    static constexpr std::size_t kMaxBlockSamples = 16;

    enum class Mode : std::uint8_t {
        Fallback,
        Exploring,
        Held,
    };

    enum class DecisionReason : std::uint8_t {
        NaturalFallback,
        MissingSourceTimestamp,
        InvalidSourceTimestamp,
        InsufficientCadence,
        UnstableCadence,
        CadenceChanged,
        IdleFrozen,
        IdleTimeout,
        NoPhaseRoom,
        Exploring,
        HeldCandidate,
    };

    enum class ResetReason : std::uint8_t {
        Explicit,
        InvalidObservation,
        ContextChanged,
        RegimeChanged,
        CadenceChanged,
        PeriodicReprobe,
    };

    struct Config {
        std::array<std::uint32_t, kMaxCandidateCount> candidate_phases_us{};
        std::uint8_t candidate_count = 0;
        std::uint8_t block_samples = 0;
        std::uint8_t min_cadence_samples = 0;
        std::uint32_t min_source_period_us = 0;
        std::uint32_t max_source_period_us = 0;
        std::uint32_t next_frame_guard_us = 0;
        // Inactive source time below this threshold freezes learning; a
        // sustained idle interval clears confidence before active work resumes.
        std::uint32_t idle_reset_after_us = 0;
        // Held observations between bounded full candidate re-probes.
        std::uint8_t held_reprobe_after_blocks = 0;
        std::uint32_t cadence_tolerance_percent = 0;
        std::uint32_t cadence_change_percent = 0;
        float residual_weight = 0.0f;
        float tail_weight = 0.0f;
        // Milliseconds per accumulated frame beyond the first, capped below.
        float backlog_weight = 0.0f;
        float residual_cap_ms = 0.0f;
        float tail_cap_ms = 0.0f;
        float backlog_cap_ms = 0.0f;
        float promotion_absolute_improvement_ms = 0.0f;
        float promotion_relative_improvement = 0.0f;
    };

    struct FrameContext {
        std::uint64_t source_present_steady_ns = 0;
        bool source_present_steady_available = false;
        bool active = false;
        std::uint64_t context_id = 0;
        bool context_id_available = false;
        std::uint64_t regime_id = 0;
        bool regime_id_available = false;
    };

    struct CompletedObservation {
        std::uint64_t frame_id = 0;
        std::uint64_t source_present_steady_ns = 0;
        bool source_present_steady_available = false;
        std::uint32_t applied_phase_us = 0;
        float cuda_submit_wait_ms = 0.0f;
        float output_wait_ms = 0.0f;
        float sync_queue_residual_ms = 0.0f;
        float tail_latency_ms = 0.0f;
        // Direct DXGI accumulated-frame evidence; never inferred from frame ids.
        std::uint32_t accumulated_frames = 0;
        bool completed = false;
        bool timing_confident = false;
        bool active = false;
        std::uint64_t context_id = 0;
        bool context_id_available = false;
        std::uint64_t regime_id = 0;
        bool regime_id_available = false;
    };

    struct Decision {
        std::uint32_t phase_us = 0;
        std::uint32_t estimated_period_us = 0;
        std::uint8_t candidate_index = 0;
        std::uint8_t block_sample_count = 0;
        std::uint64_t adaptation_epoch = 0;
        Mode mode = Mode::Fallback;
        DecisionReason reason = DecisionReason::NaturalFallback;
        bool cadence_stable = false;
    };

    struct Snapshot {
        Mode mode = Mode::Fallback;
        std::uint32_t held_phase_us = 0;
        std::uint32_t estimated_period_us = 0;
        std::uint8_t active_candidate_index = 0;
        std::uint8_t active_candidate_samples = 0;
        std::uint64_t adaptation_epoch = 0;
        bool cadence_stable = false;
    };

    AdaptiveCudaSubmitPhaseController();
    explicit AdaptiveCudaSubmitPhaseController(const Config& config);

    AdaptiveCudaSubmitPhaseController(
        const AdaptiveCudaSubmitPhaseController&) = delete;
    AdaptiveCudaSubmitPhaseController& operator=(
        const AdaptiveCudaSubmitPhaseController&) = delete;

    // Returns a phase relative to source_present_steady_ns. A zero phase is
    // always a legal natural-submit fallback.
    Decision recommend(const FrameContext& frame) noexcept;

    // Consumes only a completed prior-frame observation. Invalid or low-
    // confidence measurements reset adaptation and cannot promote a phase.
    void observe(const CompletedObservation& observation) noexcept;

    // Clears learned candidate confidence. Cadence is retained for identity /
    // regime resets and cleared for timestamp/cadence resets.
    void reset(ResetReason reason = ResetReason::Explicit) noexcept;

    Snapshot snapshot() const noexcept;
    const Config& config() const noexcept { return config_; }

    static Config default_config() noexcept;
    static float objective_cost(const CompletedObservation& observation,
        const Config& config) noexcept;

private:
    struct CandidateStats {
        std::array<float, kMaxBlockSamples> costs{};
        std::uint8_t count = 0;

        void clear() noexcept;
        void add(float cost, std::uint8_t capacity) noexcept;
        float robust_cost() const noexcept;
    };

    struct CadenceState {
        std::array<std::uint32_t, kCadenceWindowSize> intervals_us{};
        std::uint8_t count = 0;
        std::uint8_t next = 0;
        std::uint64_t last_source_present_ns = 0;
        std::uint32_t estimated_period_us = 0;
        bool stable = false;

        void clear() noexcept;
    };

    Config config_{};
    std::array<CandidateStats, kMaxCandidateCount> candidate_stats_{};
    CadenceState cadence_{};
    Mode mode_ = Mode::Fallback;
    ResetReason last_reset_reason_ = ResetReason::Explicit;
    std::uint8_t active_candidate_index_ = 0;
    std::uint8_t active_candidate_samples_ = 0;
    std::uint32_t held_phase_us_ = 0;
    std::uint32_t held_observation_count_ = 0;
    std::uint64_t adaptation_epoch_ = 0;
    std::uint64_t last_observed_source_present_ns_ = 0;
    bool have_context_ = false;
    std::uint64_t context_id_ = 0;
    bool have_regime_ = false;
    std::uint64_t regime_id_ = 0;
    bool exploration_complete_ = false;
    std::uint64_t idle_since_source_present_ns_ = 0;
    bool idle_reset_applied_ = false;

    void sanitize_config() noexcept;
    void clear_adaptation(ResetReason reason) noexcept;
    bool update_identity(const FrameContext& frame) noexcept;
    bool update_observation_identity(const CompletedObservation& observation) noexcept;
    Decision fallback(DecisionReason reason) const noexcept;
    bool update_cadence(std::uint64_t source_present_ns,
        DecisionReason* failure_reason) noexcept;
    std::uint32_t maximum_safe_phase_us() const noexcept;
    std::uint32_t effective_candidate_phase_us(std::uint8_t index) const noexcept;
    int candidate_index_for_phase(std::uint32_t phase_us) const noexcept;
    void start_exploration_if_ready() noexcept;
    void advance_candidate() noexcept;
    void finish_exploration() noexcept;
};

const char* adaptive_phase_mode_name(
    AdaptiveCudaSubmitPhaseController::Mode mode) noexcept;
const char* adaptive_phase_reason_name(
    AdaptiveCudaSubmitPhaseController::DecisionReason reason) noexcept;

}  // namespace vision_native
