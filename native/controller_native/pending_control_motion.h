#pragma once

#include "causal_mix_evaluator.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace controller_native {

struct DeliveredPreRecoilSample {
    double delivered_at_seconds = 0.0;
    pipeline_contract::Vec2f pre_recoil_stick{};
    std::uint64_t target_id = 0;
    bool delivered = false;
    bool output_enabled = true;
};

struct PendingControlMotionEstimate {
    std::array<pipeline_contract::Vec2f, kCausalMixHorizonCount>
        camera_displacement_px{};
    bool valid = false;
};

pipeline_contract::Vec2f delivered_camera_work_px(
    pipeline_contract::Vec2f control_displacement_px) noexcept;

pipeline_contract::Vec2f remaining_work_after_delivery(
    pipeline_contract::Vec2f error_px,
    pipeline_contract::Vec2f delivered_work_px) noexcept;

class PendingControlMotion {
public:
    bool observe(const DeliveredPreRecoilSample& sample) noexcept;
    PendingControlMotionEstimate estimate(
        double now_seconds,
        float observation_age_ms,
        float response_scale_px_per_stick_second,
        std::uint64_t target_id) const noexcept;
    PendingControlMotionEstimate estimate_between(
        double begin_seconds,
        double end_seconds,
        float response_scale_px_per_stick_second,
        std::uint64_t target_id) const noexcept;
    void reset() noexcept;

private:
    static constexpr std::size_t kCapacity = 256;
    const DeliveredPreRecoilSample& at(std::size_t index) const noexcept;

    std::array<DeliveredPreRecoilSample, kCapacity> samples_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

// W5 keeps the exact delivered final command and its contemporaneous camera
// response.  It deliberately lives beside the legacy Remaining ledger while
// shadow validation is in progress; the two models must not both own output.
struct DeliveredFinalCommandSample {
    double delivered_at_seconds = 0.0;
    pipeline_contract::Vec2f final_stick{};
    // Screen-space camera velocity: +X moves the reticle right, +Y moves it
    // down.  The response curve and sensitivity are resolved at delivery time
    // so later learning/config changes cannot rewrite history.
    pipeline_contract::Vec2f camera_velocity_px_per_second{};
    std::uint64_t target_id = 0;
    std::uint64_t ads_epoch = 0;
    bool delivered = false;
    bool output_enabled = true;
    // Physical actuator/backend identity.  Target and ADS values above are
    // attribution metadata only; this epoch is the only lifecycle key that
    // can invalidate the physical history.
    std::uint64_t physical_actuator_epoch = 1;
    float response_confidence = 1.0f;
    bool response_model_valid = true;
};

enum class CausalMotionLedgerStatus : unsigned char {
    Valid = 0,
    Empty = 1,
    InvalidRequest = 2,
    HorizonExceeded = 3,
    IncompleteHistory = 4,
    LifecycleMismatch = 5,
    InvalidSample = 6,
    BackendStateUnknown = 7,
    NonMonotonicClock = 8,
    DeviceEpochChanged = 9,
    CapturePairIncompatible = 10,
    InvalidResponseModel = 11,
};

struct CausalMotionPhaseRequest {
    double previous_capture_seconds = 0.0;
    double current_capture_seconds = 0.0;
    double decision_seconds = 0.0;
    float response_delay_ms = 20.0f;
    float memory_horizon_ms = 200.0f;
    std::uint64_t target_id = 0;
    std::uint64_t ads_epoch = 0;
    // These are logical capture compatibility and physical history join
    // hints.  They are deliberately not target filters.
    bool capture_pair_compatible = true;
    std::uint64_t physical_actuator_epoch = 0;
    // Provenance is carried only for a safe shadow join; it does not alter
    // the causal integration or controller output.
    std::uint64_t previous_source_frame_id = 0;
    std::uint64_t previous_source_observation_id = 0;
    std::uint64_t previous_present_steady_ns = 0;
    std::uint64_t previous_present_calibration_id = 0;
    std::uint64_t previous_present_qpc_frequency = 0;
    std::uint64_t previous_target_id = 0;
    std::uint64_t previous_ads_epoch = 0;
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t current_target_id = 0;
    std::uint64_t current_ads_epoch = 0;
    std::uint64_t current_present_steady_ns = 0;
    std::uint64_t present_calibration_id = 0;
    std::uint64_t present_qpc_frequency = 0;
    bool present_time_valid = false;
};

struct CausalMotionCaptureProvenance {
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t present_steady_ns = 0;
    std::uint64_t present_calibration_id = 0;
    std::uint64_t present_qpc_frequency = 0;
    std::uint64_t target_id = 0;
    std::uint64_t ads_epoch = 0;

    bool valid() const noexcept {
        return source_frame_id != 0 && source_observation_id != 0 &&
            present_steady_ns != 0 && present_calibration_id != 0 &&
            present_qpc_frequency != 0 && target_id != 0 && ads_epoch != 0;
    }
};

struct CausalMotionPhaseEstimate {
    // Motion already expected to be visible between the two observations.
    pipeline_contract::Vec2f realized_px{};
    // Commands delivered before capture whose delayed effect is not visible in
    // the current observation yet.
    pipeline_contract::Vec2f in_flight_px{};
    // Commands delivered after capture and before this control decision.
    pipeline_contract::Vec2f scheduled_px{};
    // P for the current observation: in_flight + scheduled only.  The
    // horizon bounds query validity; it is not an additional integration
    // window and must not re-count already-realized motion.
    pipeline_contract::Vec2f pending_total_px{};
    CausalMotionLedgerStatus status = CausalMotionLedgerStatus::Empty;
    CausalMotionLedgerStatus realized_status = CausalMotionLedgerStatus::Empty;
    // Minimum response confidence over nonzero-duration modeled segments.
    // These are diagnostic only and never alter final output.
    float realized_response_confidence = 0.0f;
    float pending_response_confidence = 0.0f;
    bool realized_response_confidence_valid = false;
    bool pending_response_confidence_valid = false;
    bool realized_valid = false;
    bool pending_valid = false;
    bool valid = false;
    std::uint64_t physical_actuator_epoch = 0;
    std::uint64_t previous_source_frame_id = 0;
    std::uint64_t previous_source_observation_id = 0;
    std::uint64_t previous_present_steady_ns = 0;
    std::uint64_t previous_present_calibration_id = 0;
    std::uint64_t previous_present_qpc_frequency = 0;
    std::uint64_t previous_target_id = 0;
    std::uint64_t previous_ads_epoch = 0;
    std::uint64_t source_frame_id = 0;
    std::uint64_t source_observation_id = 0;
    std::uint64_t current_target_id = 0;
    std::uint64_t current_ads_epoch = 0;
    std::uint64_t current_present_steady_ns = 0;
    std::uint64_t present_calibration_id = 0;
    std::uint64_t present_qpc_frequency = 0;
    bool present_time_valid = false;
};

// A realized interval is a previous->current pair. This diagnostic join must
// reject a matching current endpoint when the controller used a different
// previous endpoint; current-only joins are not sufficient evidence.
inline bool causal_motion_estimate_matches_capture_pair(
    const CausalMotionPhaseEstimate& estimate,
    const CausalMotionCaptureProvenance& previous,
    const CausalMotionCaptureProvenance& current,
    std::uint64_t physical_actuator_epoch) noexcept {
    return estimate.realized_valid && previous.valid() && current.valid() &&
        physical_actuator_epoch != 0 &&
        estimate.physical_actuator_epoch == physical_actuator_epoch &&
        estimate.previous_source_frame_id == previous.source_frame_id &&
        estimate.previous_source_observation_id ==
            previous.source_observation_id &&
        estimate.previous_present_steady_ns == previous.present_steady_ns &&
        estimate.previous_present_calibration_id ==
            previous.present_calibration_id &&
        estimate.previous_present_qpc_frequency ==
            previous.present_qpc_frequency &&
        estimate.previous_target_id == previous.target_id &&
        estimate.previous_ads_epoch == previous.ads_epoch &&
        estimate.source_frame_id == current.source_frame_id &&
        estimate.source_observation_id == current.source_observation_id &&
        estimate.current_present_steady_ns == current.present_steady_ns &&
        estimate.present_calibration_id == current.present_calibration_id &&
        estimate.present_qpc_frequency == current.present_qpc_frequency &&
        estimate.current_target_id == current.target_id &&
        estimate.current_ads_epoch == current.ads_epoch &&
        estimate.present_time_valid;
}

class CausalMotionLedger {
public:
    bool observe(const DeliveredFinalCommandSample& sample) noexcept;
    void invalidate(
        CausalMotionLedgerStatus reason,
        std::uint64_t physical_actuator_epoch = 0) noexcept;
    CausalMotionPhaseEstimate estimate(
        const CausalMotionPhaseRequest& request) const noexcept;
    void reset() noexcept;
    std::size_t size() const noexcept { return size_; }
    CausalMotionLedgerStatus last_invalidation_status() const noexcept {
        return invalidation_status_;
    }
    std::uint64_t physical_actuator_epoch() const noexcept {
        return physical_actuator_epoch_;
    }

private:
    static constexpr std::size_t kCapacity = 512;
    static constexpr std::size_t kConfidenceBlockSize = 16;
    static constexpr std::size_t kConfidenceBlockCount =
        (kCapacity + kConfidenceBlockSize - 1) / kConfidenceBlockSize;
    struct IntegralResult {
        pipeline_contract::Vec2f displacement_px{};
        float response_confidence = 0.0f;
        bool response_confidence_valid = false;
        CausalMotionLedgerStatus status =
            CausalMotionLedgerStatus::IncompleteHistory;
        bool valid = false;
    };

    const DeliveredFinalCommandSample& at(std::size_t index) const noexcept;
    IntegralResult integrate(
        double begin_seconds,
        double end_seconds) const noexcept;
    std::size_t upper_bound_index(double seconds) const noexcept;
    std::size_t lower_bound_index(double seconds) const noexcept;
    bool primitive_at(
        double seconds,
        pipeline_contract::Vec2f* area) const noexcept;
    void rebuild_confidence_block(std::size_t physical_index) noexcept;
    bool physical_index_is_active(std::size_t physical_index) const noexcept;
    void include_confidence_range(
        std::size_t first_physical,
        std::size_t last_physical,
        float* minimum_confidence,
        bool* confidence_valid,
        bool* invalid_response_model) const noexcept;
    void include_confidence_logical_range(
        std::size_t first_logical,
        std::size_t last_logical,
        float* minimum_confidence,
        bool* confidence_valid,
        bool* invalid_response_model) const noexcept;
    void clear_samples() noexcept;

    std::array<DeliveredFinalCommandSample, kCapacity> samples_{};
    // Area from the first known sample timestamp to each delivery timestamp.
    // This makes interval integration a prefix subtraction rather than a
    // scan over every held-output segment.
    std::array<pipeline_contract::Vec2f, kCapacity> cumulative_area_{};
    std::array<float, kConfidenceBlockCount> confidence_block_min_{};
    std::array<std::uint16_t, kConfidenceBlockCount>
        confidence_block_invalid_count_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    double last_delivery_seconds_ = 0.0;
    // No state is inferred before this timestamp.  A successful neutral
    // report may serve as an explicit known-zero anchor, while a first
    // non-neutral report only establishes coverage from its own delivery
    // timestamp onward.
    double history_coverage_start_seconds_ = 0.0;
    std::uint64_t physical_actuator_epoch_ = 0;
    CausalMotionLedgerStatus invalidation_status_ =
        CausalMotionLedgerStatus::Empty;
};

}  // namespace controller_native
