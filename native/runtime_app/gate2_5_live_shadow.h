#pragma once

#include "control_learning/control_history.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace runtime_app {

// Gate 2.5A is a passive, diagnostic-only observer.  It consumes the
// already-committed target endpoint and the existing fixed delivery history;
// it never produces a controller command.
enum class Gate25Reason : std::uint8_t {
    None = 0,
    MissingPresentClock,
    NoTargetIdentity,
    DuplicateObservation,
    SamePresentEndpoint,
    StaleObservation,
    BackwardPresent,
    CaptureGap,
    IdentityBoundary,
    SelectorBoundary,
    AdsBoundary,
    ModeBoundary,
    ViewportBoundary,
    BackendBoundary,
    TargetAcquisitionBoundary,
    ResponseDelayBoundary,
    PresentCalibrationBoundary,
    MissingDeliveryHistory,
    IncompleteDeliveryHistory,
    MissingResponseDelay,
    MissingNeutralBaseline,
    BelowNoiseFloor,
    CancellationOrReversal,
    ExogenousMotion,
    TargetMotionContamination,
    ResponseModelUnavailable,
    ProfileIdentityUnavailable,
    InvalidGeometry,
};

enum class Gate25ResponseDelaySource : std::uint8_t {
    Unavailable = 0,
    ConfiguredHypothesis,
    Measured,
};

enum class Gate25EvidenceStatus : std::uint8_t {
    InsufficientEvidence = 0,
    EffectEvidenceAvailable,
};

const char* gate25_reason_name(Gate25Reason reason) noexcept;
const char* gate25_status_name(Gate25EvidenceStatus status) noexcept;

struct Gate25ObservationInput {
    // Observation key. These values are never replaced with controller tick
    // ids and source_observation_id is never interpreted as persistent target
    // identity.
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t selector_target_generation = 0;
    std::uint64_t physical_ads_epoch = 0;
    std::uint64_t target_acquisition_id = 0;
    std::uint64_t viewport_sequence = 0;
    std::uint64_t viewport_source_frame_id = 0;
    std::uint32_t accumulated_frames = 0;

    // Present endpoint provenance. result_at_ns is processing time and is
    // deliberately not used as a present/effect timestamp.
    std::uint64_t source_present_qpc = 0;
    std::uint64_t source_present_qpc_frequency = 0;
    std::uint64_t source_present_steady_ns = 0;
    std::uint64_t source_present_calibration_id = 0;
    std::uint64_t source_present_calibration_uncertainty_ns = 0;
    bool source_present_available = false;
    bool source_present_steady_available = false;

    std::uint64_t captured_at_ns = 0;
    std::uint64_t result_at_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    std::uint64_t decision_ns = 0;
    std::uint64_t controller_tick_id = 0;
    std::uint64_t response_delay_ns = 0;
    bool response_delay_valid = false;
    Gate25ResponseDelaySource response_delay_source =
        Gate25ResponseDelaySource::Unavailable;

    // Absolute screen anchor. Runtime fills this from the committed stable
    // error plus the source screen center, so the pair uses the same stable
    // production geometry that was admitted to the controller.
    float target_anchor_screen_x = 0.0f;
    float target_anchor_screen_y = 0.0f;
    float stable_body_width = 0.0f;
    float stable_body_height = 0.0f;
    float reliability = 0.0f;
    float target_confidence = 0.0f;
    float motion_anchor_score = 0.0f;
    int viewport_width = 0;
    int viewport_height = 0;

    std::uint8_t lifecycle = 0;
    std::uint8_t motion = 0;
    std::uint8_t mode = 0;
    std::uint16_t response_label_ms = 0; // 0 means unavailable, not 500.
    bool response_profile_provenance_valid = false;
    bool ads_release_neutral_before_rise = false;
    bool ads_rising_edge = false;

    // Runtime state used only for strict boundary/eligibility decisions.
    std::uint64_t backend_epoch = 0;
    bool backend_known = false;
    bool output_enabled = true;
    bool selector_identity_protocol = false;
    bool fresh_observed = false;
    bool strong_observation = false;
    bool stable_coordinates_valid = false;
    bool has_motion_anchor = false;
    bool reused_or_projected = false;
    bool response_model_available = false;
    bool game_profile_identity_available = false;

    // Optional comparison supplied by an existing shadow ledger. It is a
    // diagnostic cross-check only; it is never used to manufacture observed
    // effect or to alter output. The full previous/current provenance travels
    // with the candidate so the observer can compare against its own accepted
    // prior, rather than a second RuntimeLoop-side anchor.
    bool ledger_realized_available = false;
    float ledger_realized_x = 0.0f;
    float ledger_realized_y = 0.0f;
    std::uint64_t ledger_physical_actuator_epoch = 0;
    std::uint64_t ledger_previous_source_frame_id = 0;
    std::uint64_t ledger_previous_source_observation_id = 0;
    std::uint64_t ledger_previous_present_steady_ns = 0;
    std::uint64_t ledger_previous_present_calibration_id = 0;
    std::uint64_t ledger_previous_present_qpc_frequency = 0;
    std::uint64_t ledger_previous_target_id = 0;
    std::uint64_t ledger_previous_ads_epoch = 0;
    std::uint64_t ledger_current_source_frame_id = 0;
    std::uint64_t ledger_current_source_observation_id = 0;
    std::uint64_t ledger_current_present_steady_ns = 0;
    std::uint64_t ledger_current_present_calibration_id = 0;
    std::uint64_t ledger_current_present_qpc_frequency = 0;
    std::uint64_t ledger_current_target_id = 0;
    std::uint64_t ledger_current_ads_epoch = 0;
};

struct Gate25EffectSample {
    bool valid = false;
    bool ledger_comparison_valid = false;
    Gate25Reason reason = Gate25Reason::None;
    std::uint64_t previous_frame_id = 0;
    std::uint64_t current_frame_id = 0;
    std::uint64_t previous_present_steady_ns = 0;
    std::uint64_t current_present_steady_ns = 0;
    std::uint64_t controller_tick_id = 0;
    std::uint64_t delivery_first_seq = 0;
    std::uint64_t delivery_last_seq = 0;
    Gate25ResponseDelaySource response_delay_source =
        Gate25ResponseDelaySource::Unavailable;
    float interval_ms = 0.0f;
    float target_delta_x = 0.0f;
    float target_delta_y = 0.0f;
    float neutral_baseline_x = 0.0f;
    float neutral_baseline_y = 0.0f;
    float observed_camera_work_x = 0.0f;
    float observed_camera_work_y = 0.0f;
    float delivered_final_work_x = 0.0f;
    float delivered_final_work_y = 0.0f;
    float delivered_final_exposure_seconds = 0.0f;
    float ledger_realized_x = 0.0f;
    float ledger_realized_y = 0.0f;
    float residual_x = 0.0f;
    float residual_y = 0.0f;
    float snr = 0.0f;
    bool saturated = false;
};

// The production ControlMode contract currently has exactly three values:
// Manual, AdsAcquire and BodyLockFollow. Keep the persisted grid aligned with
// that contract so transport capacity can cover every real mode/axis/bin cell
// without carrying unreachable mode rows.
inline constexpr std::size_t kGate25ModeCapacity = 3;
inline constexpr std::size_t kGate25AxisCapacity = 4;
inline constexpr std::size_t kGate25CommandBinCapacity = 6;
inline constexpr std::size_t kGate25CohortCellCount =
    kGate25ModeCapacity * kGate25AxisCapacity * kGate25CommandBinCapacity;

struct Gate25CohortCell {
    std::uint32_t pair_count = 0;
    std::uint32_t valid_count = 0;
    std::uint32_t invalid_count = 0;
    std::uint32_t snr_pass_count = 0;
    std::uint32_t below_noise_count = 0;
    float delivered_final_sum_x = 0.0f;
    float delivered_final_sum_y = 0.0f;
    float delivered_final_abs_sum = 0.0f;
    float delivered_final_energy_sum = 0.0f;
    float attempted_delivered_final_sum_x = 0.0f;
    float attempted_delivered_final_sum_y = 0.0f;
    float attempted_delivered_final_abs_sum = 0.0f;
    std::uint32_t attempted_delivered_count = 0;
    float observed_camera_sum_x = 0.0f;
    float observed_camera_sum_y = 0.0f;
    float observed_camera_abs_sum = 0.0f;
    float observed_camera_energy_sum = 0.0f;
    float delivered_observed_dot_sum = 0.0f;
    float delivered_observed_cross_sum = 0.0f;
    std::uint32_t component_sign_agree_count = 0;
    std::uint32_t component_sign_disagree_count = 0;
    float ledger_residual_sum = 0.0f;
    float ledger_residual_max = 0.0f;
    std::uint32_t ledger_residual_count = 0;
};

enum class Gate25WindowClockDomain : std::uint8_t {
    SourcePresentSteady,
    CollectorMonotonicDiagnostic,
};

// Scalar aggregate fields are separated from the dense internal cohort grid
  // so the writer can transport only populated cells without copying all 72
// fixed cells across the ordinary telemetry queue boundary.
struct Gate25AggregateScalars {
    std::uint64_t window_begin_ns = 0;
    std::uint64_t window_end_ns = 0;
    std::uint64_t last_controller_tick_id = 0;
    std::uint64_t delivery_first_seq = 0;
    std::uint64_t delivery_last_seq = 0;
    std::uint64_t observations = 0;
    std::uint64_t compatible_pairs = 0;
    std::uint64_t effect_valid = 0;
    std::uint64_t neutral_baseline_pairs = 0;
    std::uint64_t ledger_comparisons = 0;
    std::uint64_t snr_pass = 0;
    std::uint64_t below_noise_floor = 0;
    std::uint64_t duplicate = 0;
    std::uint64_t same_present_endpoint = 0;
    std::uint64_t stale = 0;
    std::uint64_t backward_present = 0;
    std::uint64_t capture_gap = 0;
    std::uint64_t identity_boundary = 0;
    std::uint64_t selector_boundary = 0;
    std::uint64_t ads_boundary = 0;
    std::uint64_t mode_boundary = 0;
    std::uint64_t viewport_boundary = 0;
    std::uint64_t backend_boundary = 0;
    std::uint64_t target_acquisition_boundary = 0;
    std::uint64_t response_delay_boundary = 0;
    std::uint64_t present_calibration_boundary = 0;
    std::uint64_t missing_present_clock = 0;
    std::uint64_t missing_delivery_history = 0;
    std::uint64_t incomplete_delivery_history = 0;
    std::uint64_t missing_response_delay = 0;
    std::uint64_t cancellation_or_reversal = 0;
    std::uint64_t invalid_geometry = 0;
    std::uint64_t exogenous_rejected = 0;
    std::uint64_t target_motion_contamination = 0;
    std::uint64_t missing_neutral_baseline = 0;
    std::uint64_t no_target_identity = 0;
    std::uint64_t response_model_unavailable = 0;
    std::uint64_t profile_identity_unavailable = 0;
    std::uint64_t saturation_rows = 0;
    std::uint64_t model_insufficient = 0;
    // These are cadence/coverage counters, not model scores. A model score
    // is only emitted when a valid prediction residual exists.
    std::uint64_t continuous_zoh_coverage_rows = 0;
    std::uint64_t fixed_180hz_coverage_rows = 0;
    std::uint64_t fixed_240hz_coverage_rows = 0;
    std::uint64_t observed_present_cadence_coverage_rows = 0;
    std::uint64_t manual_only_rows = 0;
    std::uint64_t ai_only_rows = 0;
    std::uint64_t mixed_rows = 0;
    std::array<std::uint64_t, 6> output_magnitude_bins{};
    std::array<std::uint64_t, 4> output_axis_bins{}; // x, y, diagonal, zero
    bool cohort_profile_incomplete = false;
    std::uint64_t response_260ms_rows = 0;
    std::uint64_t response_400ms_rows = 0;
    Gate25ResponseDelaySource response_delay_source =
        Gate25ResponseDelaySource::Unavailable;
    float baseline_median_x = 0.0f;
    float baseline_median_y = 0.0f;
    float baseline_mad_x = 0.0f;
    float baseline_mad_y = 0.0f;
    float observed_sum_x = 0.0f;
    float observed_sum_y = 0.0f;
    float residual_abs_sum = 0.0f;
    float residual_abs_max = 0.0f;
    std::uint32_t anomaly_count = 0;
    std::uint32_t anomaly_dropped = 0;
    Gate25EvidenceStatus status = Gate25EvidenceStatus::InsufficientEvidence;
    Gate25WindowClockDomain window_clock_domain =
        Gate25WindowClockDomain::SourcePresentSteady;
};

struct Gate25AggregateSnapshot : Gate25AggregateScalars {
    // Fixed 3-mode x axis x command-magnitude cohort grid. Profile identity is
    // required before interpreting cells across sessions. This remains dense
    // inside the observer; the RuntimeTelemetry transport compacts it.
    std::array<Gate25CohortCell, kGate25CohortCellCount> cohort_grid{};
};

// Every persisted anomaly is fixed-size and bounded. It intentionally omits
// strings, vectors and raw frame data.
struct Gate25Anomaly {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t persistent_target_id = 0;
    std::uint64_t present_steady_ns = 0;
    std::uint64_t decision_ns = 0;
    std::uint64_t controller_tick_id = 0;
    std::uint64_t backend_epoch = 0;
    std::uint64_t delivery_first_seq = 0;
    std::uint64_t delivery_last_seq = 0;
    Gate25Reason reason = Gate25Reason::None;
    std::uint8_t reserved[7]{};
    float target_delta_x = 0.0f;
    float target_delta_y = 0.0f;
    float observed_camera_work_x = 0.0f;
    float observed_camera_work_y = 0.0f;
    float snr = 0.0f;
    float reliability = 0.0f;
    float target_confidence = 0.0f;
};

static_assert(sizeof(Gate25Anomaly) <= 256,
              "Gate 2.5A anomaly entries must remain bounded");

// Gate-only delivery history. This is deliberately a small, named physical
// delivery view rather than a second ControlHistory<1024>. It records the
// final actuator proposal and the bounded diagnostics needed by Gate2.5; it
// has no control authority and does not carry target ownership.
struct Gate25DeliverySample {
    std::uint64_t sample_seq = 0;
    std::uint64_t applied_at_ns = 0;
    std::uint64_t backend_epoch = 0;
    pipeline_contract::Vec2f final_right{};
    pipeline_contract::Vec2f physical_left{};
    pipeline_contract::Vec2f manual{};
    pipeline_contract::Vec2f ai{};
    bool output_delivered = false;
    bool output_enabled = true;
    bool firing = false;
    bool recoil_active = false;
    bool saturated = false;
};

class Gate25DeliveryView {
public:
    static constexpr std::size_t kCapacity = 256;

    bool push(const Gate25DeliverySample& sample) noexcept;
    control_learning::ControlIntegral integrate(
        std::uint64_t begin_ns,
        std::uint64_t end_ns) const noexcept;
    void reset() noexcept;
    std::size_t size() const noexcept { return size_; }
    std::uint64_t push_count() const noexcept { return push_count_; }
    std::uint64_t overwritten_count() const noexcept { return overwritten_; }

private:
    const Gate25DeliverySample& entry(std::size_t index) const noexcept {
        return entries_[(head_ + index) % kCapacity];
    }
    std::size_t floor_index(std::uint64_t time_ns) const noexcept;

    std::array<Gate25DeliverySample, kCapacity> entries_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t overwritten_ = 0;
    std::uint64_t push_count_ = 0;
};

class Gate25LiveShadow {
public:
    static constexpr std::size_t kBaselineCapacity = 32;
    static constexpr std::size_t kAnomalyCapacity = 256;
    static constexpr std::uint64_t kAggregateWindowNs = 5'000'000'000ull;
    static constexpr float kMinimumEffectPx = 0.50f;
    static constexpr float kMinimumEffectSnr = 3.0f;
    // Only an actual delivered neutral/quantization residue may seed the
    // baseline. 1-2% commands remain observable cohorts for deadzone tests.
    static constexpr float kNeutralAverageStick = 0.001f;
    static constexpr float kExogenousAverageLeftStick = 0.02f;
    static constexpr std::uint64_t kMaxPresentCalibrationUncertaintyNs =
        2'000'000ull;

    Gate25LiveShadow() noexcept : construction_count_(1) {}

    void observe(
        const Gate25ObservationInput& input,
        const control_learning::ControlHistory<1024>* delivery_history) noexcept;
    void observe(
        const Gate25ObservationInput& input,
        const Gate25DeliveryView& delivery_view) noexcept;
    bool take_due_summary(Gate25AggregateSnapshot& out) noexcept;
    bool flush_summary(Gate25AggregateSnapshot& out) noexcept;
    bool pop_anomaly(Gate25Anomaly& out) noexcept;
    std::size_t pending_anomaly_count() const noexcept { return anomaly_count_; }
    const Gate25EffectSample& last_effect() const noexcept { return last_effect_; }
    std::uint64_t invocation_count() const noexcept { return invocation_count_; }
    // This is a construction diagnostic only. It is not an allocator probe;
    // allocation behavior is verified by an external contract test.
    std::uint64_t construction_count() const noexcept { return construction_count_; }
    std::uint64_t writer_invocation_count() const noexcept {
        return writer_invocation_count_;
    }
    void note_writer_invocation() noexcept { ++writer_invocation_count_; }
    bool has_observer_state() const noexcept { return true; }
    void reset() noexcept;

private:
    using DeliveryIntegrateFn = control_learning::ControlIntegral (*) (
        const void*, std::uint64_t, std::uint64_t) noexcept;
    struct DeliveryIntegrator {
        const void* context = nullptr;
        DeliveryIntegrateFn integrate = nullptr;
    };

    void observe_impl(
        const Gate25ObservationInput& input,
        const DeliveryIntegrator* delivery_source) noexcept;

    struct LastObservation {
        Gate25ObservationInput value{};
        bool valid = false;
    };

    void reset_aggregate(std::uint64_t window_begin_ns) noexcept;
    void record_observation_window(
        std::uint64_t present_ns,
        std::uint64_t controller_tick_id,
        Gate25WindowClockDomain clock_domain =
            Gate25WindowClockDomain::SourcePresentSteady) noexcept;
    void reset_pair_state() noexcept;
    void queue_anomaly(
        const Gate25ObservationInput& input,
        Gate25Reason reason,
        float target_delta_x = 0.0f,
        float target_delta_y = 0.0f,
        float observed_x = 0.0f,
        float observed_y = 0.0f,
        float snr = 0.0f) noexcept;
    bool should_queue_anomaly(Gate25Reason reason) const noexcept;
    void count_reason(Gate25Reason reason) noexcept;
    void add_neutral_baseline(float x, float y) noexcept;
    float baseline_median(const std::array<float, kBaselineCapacity>& values) const noexcept;
    float baseline_mad(
        const std::array<float, kBaselineCapacity>& values,
        float median) const noexcept;
    void update_baseline_summary() noexcept;

    Gate25AggregateSnapshot aggregate_{};
    Gate25AggregateSnapshot pending_summary_{};
    bool has_pending_summary_ = false;
    LastObservation previous_{};
    Gate25EffectSample last_effect_{};
    std::array<float, kBaselineCapacity> baseline_x_{};
    std::array<float, kBaselineCapacity> baseline_y_{};
    std::size_t baseline_count_ = 0;
    std::size_t baseline_head_ = 0;
    std::array<Gate25Anomaly, kAnomalyCapacity> anomalies_{};
    std::size_t anomaly_head_ = 0;
    std::size_t anomaly_count_ = 0;
    std::uint32_t anomaly_dropped_ = 0;
    bool missing_clock_anomaly_emitted_ = false;
    std::uint64_t invocation_count_ = 0;
    std::uint64_t construction_count_ = 0;
    std::uint64_t writer_invocation_count_ = 0;
};

}  // namespace runtime_app

static_assert(sizeof(runtime_app::Gate25LiveShadow) <= 128u * 1024u,
              "Gate 2.5A observer state must remain <=128 KiB");
