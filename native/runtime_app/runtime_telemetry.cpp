#include "runtime_telemetry.h"

#include "../pipeline_contract/target_plan.h"
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
#include "vision_native/ego_motion_observer.h"
#endif

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <string>

namespace runtime_app {
namespace {

const char* kind_name(TelemetryRecordKind kind) {
    switch (kind) {
    case TelemetryRecordKind::VisionFrame: return "VisionFrame";
    case TelemetryRecordKind::RuntimeEvent: return "RuntimeEvent";
    case TelemetryRecordKind::ManualControllerTick:
    default: return "ManualControllerTick";
    }
}

const char* record_type_name(TelemetryRecordType type) {
    switch (type) {
    case TelemetryRecordType::SessionMetadata: return "session_metadata";
    case TelemetryRecordType::InputEvent: return "input_event";
    case TelemetryRecordType::TargetEvent: return "target_event";
    case TelemetryRecordType::AdsTransitionSample: return "ads_transition_sample";
    case TelemetryRecordType::AdsTransition: return "ads_transition";
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    case TelemetryRecordType::ControlResponseWindow: return "control_response_window";
#endif
    case TelemetryRecordType::CommittedCaptureObservation:
        return "committed_capture_observation";
    case TelemetryRecordType::DeliveredControlSample: return "delivered_control_sample";
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    case TelemetryRecordType::CausalResponseShadow: return "causal_response_shadow";
#endif
    case TelemetryRecordType::AdsAcquisitionTrace: return "ads_acquisition_trace";
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    case TelemetryRecordType::EgoMotionShadow: return "ego_motion_shadow";
#endif
    case TelemetryRecordType::Gate25LiveShadow: return "w5_gate2_5a_live_shadow";
    case TelemetryRecordType::ControllerSample:
    default: return "controller_sample";
    }
}

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
const char* gate25_response_delay_source_name(
    Gate25ResponseDelaySource source) noexcept {
    switch (source) {
    case Gate25ResponseDelaySource::ConfiguredHypothesis:
        return "configured_hypothesis";
    case Gate25ResponseDelaySource::Measured:
        return "measured";
    case Gate25ResponseDelaySource::Unavailable:
    default:
        return "unavailable";
    }
}

const char* gate25_window_clock_domain_name(
    Gate25WindowClockDomain domain) noexcept {
    return domain == Gate25WindowClockDomain::CollectorMonotonicDiagnostic
        ? "collector_monotonic_diagnostic" : "source_present_steady";
}
#endif

const char* ads_acquisition_state_name(std::uint8_t value) {
    using State = pipeline_contract::AdsAcquisitionState;
    switch (static_cast<State>(value)) {
    case State::ArmedWaitingForTarget: return "armed_waiting_for_target";
    case State::AcquiringNominal: return "acquiring_nominal";
    case State::AcquiringExtended: return "acquiring_extended";
    case State::Completed: return "completed";
    case State::Consumed: return "consumed";
    case State::Idle:
    default: return "idle";
    }
}

const char* ads_decision_reason_name(std::uint8_t value) {
    using Reason = pipeline_contract::AdsDecisionReason;
    switch (static_cast<Reason>(value)) {
    case Reason::Admitted: return "admitted";
    case Reason::SelectorNoSelection: return "selector_no_selection";
    case Reason::OutsideAdsActivationRadius: return "outside_ads_activation_radius";
    case Reason::OutsideAssociationRadius: return "outside_association_radius";
    case Reason::StaleCapture: return "stale_capture";
    case Reason::DuplicateFrame: return "duplicate_frame";
    case Reason::OldControlEpoch: return "old_control_epoch";
    case Reason::LowReliability: return "low_reliability";
    case Reason::FriendlyOrCueReject: return "friendly_or_cue_reject";
    case Reason::BodylockOutsideContinuation: return "bodylock_outside_continuation";
    case Reason::AdsAlreadyConsumed: return "ads_already_consumed";
    case Reason::AcquisitionCeiling: return "acquisition_ceiling";
    case Reason::Settled: return "settled";
    case Reason::CenterCross: return "center_cross";
    case Reason::MovingAway: return "moving_away";
    case Reason::NonHelpfulOutput: return "non_helpful_output";
    case Reason::ManualEscape: return "manual_escape";
    case Reason::TargetLost: return "target_lost";
    case Reason::TargetSwitch: return "target_switch";
    case Reason::NoTarget: return "no_target";
    case Reason::None:
    default: return "none";
    }
}

const char* source_decision_outcome_name(std::uint8_t value) {
    using Outcome = pipeline_contract::SourceDecisionOutcome;
    switch (static_cast<Outcome>(value)) {
    case Outcome::Admitted: return "admitted";
    case Outcome::AcceptedContinuation: return "accepted_continuation";
    case Outcome::Rejected: return "rejected";
    case Outcome::NoDecision:
    default: return "no_decision";
    }
}

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
const char* ego_motion_invalid_reason_name(std::uint8_t value) {
    using Reason = vision_native::EgoMotionInvalidReason;
    switch (static_cast<Reason>(value)) {
    case Reason::None: return "none";
    case Reason::NoPreviousFrame: return "no_previous_frame";
    case Reason::InvalidInput: return "invalid_input";
    case Reason::DuplicateOrOutOfOrder: return "duplicate_or_out_of_order";
    case Reason::LowBackgroundCoverage: return "low_background_coverage";
    case Reason::LowConfidence: return "low_confidence";
    case Reason::SearchBoundaryLimited: return "search_boundary_limited";
    case Reason::WorkerStopped: return "worker_stopped";
    default: return "unknown";
    }
}
#endif

const char* vision_sample_quality_name(VisionSampleQuality value) {
    switch (value) {
    case VisionSampleQuality::SoftWeight: return "soft_weight";
    case VisionSampleQuality::HardReject: return "hard_reject";
    case VisionSampleQuality::Normal:
    default: return "normal";
    }
}

const char* identification_update_outcome_name(
    IdentificationUpdateOutcome value) {
    switch (value) {
    case IdentificationUpdateOutcome::AcceptedByAtLeastOneDelay:
        return "accepted_by_at_least_one_delay";
    case IdentificationUpdateOutcome::InsufficientExcitation:
        return "insufficient_excitation";
    case IdentificationUpdateOutcome::DeliveryGap: return "delivery_gap";
    case IdentificationUpdateOutcome::FiringOrRecoil: return "firing_or_recoil";
    case IdentificationUpdateOutcome::Saturated: return "saturated";
    case IdentificationUpdateOutcome::TimingInvalid: return "timing_invalid";
    case IdentificationUpdateOutcome::CoordinateInvalid: return "coordinate_invalid";
    case IdentificationUpdateOutcome::IdentityBoundary: return "identity_boundary";
    case IdentificationUpdateOutcome::NoUsableDelay: return "no_usable_delay";
    case IdentificationUpdateOutcome::NotEvaluated:
    default: return "not_evaluated";
    }
}

const char* readiness_name(TelemetryReadiness readiness) {
    switch (readiness) {
    case TelemetryReadiness::ProfileEligible: return "profile_eligible";
    case TelemetryReadiness::ModelEligible: return "model_eligible";
    case TelemetryReadiness::Diagnostic:
    default: return "diagnostic";
    }
}

const char* target_event_name(TargetEventKind event) {
    switch (event) {
    case TargetEventKind::Created: return "created";
    case TargetEventKind::Switched: return "switched";
    case TargetEventKind::Lost: return "lost";
    case TargetEventKind::Reacquired: return "reacquired";
    case TargetEventKind::Released: return "released";
    case TargetEventKind::None:
    default: return "none";
    }
}

const char* identity_quality_name(TargetIdentityQuality quality) {
    switch (quality) {
    case TargetIdentityQuality::ProductionAssociated: return "production_associated";
    case TargetIdentityQuality::StrongGeometricMatch: return "strong_geometric_match";
    case TargetIdentityQuality::WeakGeometricMatch: return "weak_geometric_match";
    case TargetIdentityQuality::ProjectedContinuity: return "projected_continuity";
    case TargetIdentityQuality::Ambiguous: return "ambiguous";
    case TargetIdentityQuality::None:
    default: return "none";
    }
}

const char* calibration_class_name(AdsCalibrationClass value) {
    switch (value) {
    case AdsCalibrationClass::CalibrationClean: return "calibration_clean";
    case AdsCalibrationClass::ConditionalModel: return "conditional_model";
    case AdsCalibrationClass::DiagnosticOnly:
    default: return "diagnostic_only";
    }
}

const char* input_event_name(InputEventKind value) {
    switch (value) {
    case InputEventKind::AdsPressed: return "ads_pressed";
    case InputEventKind::AdsReleased: return "ads_released";
    case InputEventKind::TargetCreated: return "target_created";
    case InputEventKind::TargetSwitched: return "target_switched";
    case InputEventKind::TargetLost: return "target_lost";
    case InputEventKind::TargetReacquired: return "target_reacquired";
    case InputEventKind::ManualAiConflict: return "manual_ai_conflict";
    case InputEventKind::TargetCrossed: return "target_crossed";
    case InputEventKind::BodylockEntered: return "bodylock_entered";
    case InputEventKind::BodylockExited: return "bodylock_exited";
    case InputEventKind::AuthorityChanged: return "authority_changed";
    case InputEventKind::InputStarted: return "input_started";
    case InputEventKind::InputPeak: return "input_peak";
    case InputEventKind::DirectionReversed: return "direction_reversed";
    case InputEventKind::InputSettled: return "input_settled";
    case InputEventKind::InputEnded: return "input_ended";
    default: return "none";
    }
}

const char* ads_invalid_reason_name(AdsInvalidReason value) {
    switch (value) {
    case AdsInvalidReason::NoHipfireTarget: return "no_hipfire_target";
    case AdsInvalidReason::TargetSwitched: return "target_switched";
    case AdsInvalidReason::TargetLost: return "target_lost";
    case AdsInvalidReason::ProjectedOnlyAnchor: return "projected_only_anchor";
    case AdsInvalidReason::AdsNotSettled: return "ads_not_settled";
    case AdsInvalidReason::LargeManualTurn: return "large_manual_turn";
    case AdsInvalidReason::GeometryChanged: return "geometry_changed";
    case AdsInvalidReason::InsufficientFrames: return "insufficient_frames";
    case AdsInvalidReason::IdentityAmbiguous: return "identity_ambiguous";
    case AdsInvalidReason::VisualSettleUnproven: return "visual_settle_unproven";
    case AdsInvalidReason::MotionResidualHigh: return "motion_residual_high";
    case AdsInvalidReason::SampleGap: return "sample_gap";
    case AdsInvalidReason::RuntimeShutdown: return "runtime_shutdown";
    case AdsInvalidReason::QueueOverflow: return "queue_overflow";
    default: return "none";
    }
}

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
const char* response_reason_name(ResponseWindowReason value) {
    switch (value) {
    case ResponseWindowReason::TargetChanged: return "target_changed";
    case ResponseWindowReason::IdentityWeak: return "identity_weak";
    case ResponseWindowReason::GeometryChanged: return "geometry_changed";
    case ResponseWindowReason::SampleGap: return "sample_gap";
    case ResponseWindowReason::TimingInvalid: return "timing_invalid";
    default: return "none";
    }
}

#endif

} // namespace

RuntimeTelemetry::RuntimeTelemetry(RuntimeTelemetryOptions options)
    : options_(std::move(options)) {
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    if (options_.gate_only) {
        options_.queue_capacity = std::min(
            options_.queue_capacity, kGate25GateOnlyOrdinaryQueueCapacity);
    }
#endif
    // Ordinary telemetry must be fully initialized in the production-shaped
    // build too; the research seam only changes the optional Gate-only cap.
    options_.queue_capacity = std::max<std::size_t>(1, options_.queue_capacity);
    options_.max_files = std::max<std::size_t>(1, options_.max_files);
    if (options_.enabled) queue_.resize(options_.queue_capacity);
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    // Keep all Gate transport storage out of the RuntimeTelemetry object and
    // out of ordinary/detailed sessions unless the independent Gate switch is
    // on.  gate_only controls the ordinary collector policy only.
    if (options_.enabled && options_.gate_enabled) {
        gate25_transport_ = std::make_unique<Gate25TransportState>();
    }
#endif
}

RuntimeTelemetry::~RuntimeTelemetry() {
    stop();
}

void RuntimeTelemetry::start() {
    if (!options_.enabled || !options_.start_writer) return;
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    writer_ = std::thread(&RuntimeTelemetry::writer_loop, this);
    ++writers_started_;
}

void RuntimeTelemetry::stop() {
    if (!writer_.joinable()) return;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(options_.shutdown_timeout_ms);
    shutdown_deadline_ns_.store(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count()));
    running_.store(false);
    condition_.notify_all();
    writer_.join();
    if (output_.is_open()) output_.close();
}

bool RuntimeTelemetry::enqueue(const TelemetryRecord& record) noexcept {
    if (!options_.enabled || writer_failed_.load()) return false;
    // Gate2.5 payloads use the dedicated bounded channel below. Keeping the
    // ordinary API unable to accept this type prevents a future caller from
    // reintroducing the large aggregate into every queue slot.
    if (record.type == TelemetryRecordType::Gate25LiveShadow) return false;
    std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
    if (record.critical) {
        // Critical transition records are rare. The writer only holds this
        // mutex while moving an item out of the queue, never during file I/O,
        // so waiting here is bounded and prevents target/ADS evidence from
        // disappearing because of a transient queue-pop collision.
        lock.lock();
    } else if (!lock.try_lock()) {
        record.critical ? ++dropped_critical_ : ++dropped_normal_;
        return false;
    }
    if (queue_count_ >= options_.queue_capacity) {
        record.critical ? ++dropped_critical_ : ++dropped_normal_;
        return false;
    }
    if (record.kind == TelemetryRecordKind::VisionFrame && record.frame_id != 0) {
        if (record.frame_id <= last_vision_frame_id_) {
            ++duplicate_vision_;
            return false;
        }
        last_vision_frame_id_ = record.frame_id;
    }
    queue_[queue_tail_] = record;
    queue_tail_ = (queue_tail_ + 1) % queue_.size();
    ++queue_count_;
    ++accepted_;
    const auto size = static_cast<std::uint64_t>(queue_count_);
    auto previous = high_watermark_.load();
    while (size > previous && !high_watermark_.compare_exchange_weak(previous, size)) {}
    lock.unlock();
    condition_.notify_one();
    return true;
}

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
Gate25AggregateTransport RuntimeTelemetry::compact_gate25_aggregate(
    const Gate25AggregateSnapshot& summary) noexcept {
    Gate25AggregateTransport compact;
    compact.scalar = static_cast<const Gate25AggregateScalars&>(summary);
    for (std::size_t index = 0; index < summary.cohort_grid.size(); ++index) {
        const auto& cell = summary.cohort_grid[index];
        if (cell.pair_count == 0) continue;
        if (compact.active_cell_count >= kGate25TransportCellCapacity) {
            compact.active_cell_overflow = true;
            continue;
        }
        compact.cell_indices[compact.active_cell_count] =
            static_cast<std::uint16_t>(index);
        compact.cells[compact.active_cell_count] = cell;
        ++compact.active_cell_count;
    }
    return compact;
}

bool RuntimeTelemetry::enqueue_gate25_aggregate(
    const Gate25AggregateSnapshot& summary) noexcept {
    if (!options_.enabled || !gate25_transport_ || writer_failed_.load()) return false;
    const auto compact = compact_gate25_aggregate(summary);
    std::unique_lock<std::mutex> lock(mutex_);
    auto& transport = *gate25_transport_;
    if (transport.aggregate_count >= kGate25AggregateQueueCapacity) {
        ++gate25_dropped_;
        return false;
    }
    transport.aggregate_queue[transport.aggregate_tail] = compact;
    transport.aggregate_tail =
        (transport.aggregate_tail + 1) % kGate25AggregateQueueCapacity;
    ++transport.aggregate_count;
    ++gate25_accepted_;
    auto previous = gate25_high_watermark_.load();
    const auto size = static_cast<std::uint64_t>(
        transport.aggregate_count + transport.anomaly_count);
    while (size > previous &&
           !gate25_high_watermark_.compare_exchange_weak(previous, size)) {}
    lock.unlock();
    condition_.notify_one();
    return true;
}

bool RuntimeTelemetry::enqueue_gate25_anomaly(
    const Gate25Anomaly& anomaly) noexcept {
    if (!options_.enabled || !gate25_transport_ || writer_failed_.load()) return false;
    std::unique_lock<std::mutex> lock(mutex_);
    auto& transport = *gate25_transport_;
    if (transport.anomaly_count >= kGate25AnomalyQueueCapacity) {
        ++gate25_dropped_;
        return false;
    }
    transport.anomaly_queue[transport.anomaly_tail] = anomaly;
    transport.anomaly_tail =
        (transport.anomaly_tail + 1) % kGate25AnomalyQueueCapacity;
    ++transport.anomaly_count;
    ++gate25_accepted_;
    auto previous = gate25_high_watermark_.load();
    const auto size = static_cast<std::uint64_t>(
        transport.aggregate_count + transport.anomaly_count);
    while (size > previous &&
           !gate25_high_watermark_.compare_exchange_weak(previous, size)) {}
    lock.unlock();
    condition_.notify_one();
    return true;
}
#endif

RuntimeTelemetryCounters RuntimeTelemetry::counters() const noexcept {
    return RuntimeTelemetryCounters{
        accepted_.load(), dropped_normal_.load(), dropped_critical_.load(),
        duplicate_vision_.load(), serialized_.load(), writers_started_.load(),
        writer_failures_.load(), high_watermark_.load()
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
        ,
        gate25_accepted_.load(), gate25_dropped_.load(),
        gate25_serialized_.load(), gate25_high_watermark_.load()};
#else
        };
#endif
}

std::size_t RuntimeTelemetry::ordinary_queue_capacity() const noexcept {
    return queue_.capacity();
}

std::size_t RuntimeTelemetry::ordinary_queue_size() const noexcept {
    return queue_.size();
}

std::size_t RuntimeTelemetry::ordinary_queue_bytes() const noexcept {
    return queue_.capacity() * sizeof(TelemetryRecord);
}

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
std::size_t RuntimeTelemetry::gate25_transport_queue_bytes() const noexcept {
    if (!gate25_transport_) return 0;
    return kGate25AggregateQueueCapacity * sizeof(Gate25AggregateTransport) +
        kGate25AnomalyQueueCapacity * sizeof(Gate25Anomaly);
}
#endif

const std::filesystem::path& RuntimeTelemetry::log_path() const noexcept {
    return log_path_;
}

bool RuntimeTelemetry::open_next_file() {
    try {
        std::filesystem::create_directories(options_.directory);
        if (output_.is_open()) output_.close();
        const std::size_t slot = file_index_++;
        const char* session_id = has_session_metadata_
            ? session_metadata_.session_metadata.session_id.data() : "unknown-session";
        log_path_ = options_.directory / ("native_runtime_telemetry_" +
            std::string(session_id) + "_" + std::to_string(slot) + ".jsonl");
        output_.open(log_path_, std::ios::out | std::ios::trunc);
        current_size_ = 0;
        if (!output_.is_open()) {
            if (!writer_failed_.exchange(true)) ++writer_failures_;
            return false;
        }
        if (has_session_metadata_) {
            const auto& metadata = session_metadata_.session_metadata;
            output_ << '{'
                << "\"schema_version\":" << session_metadata_.schema_version
                << ",\"type\":\"session_metadata\""
                << ",\"session_id\":\"" << metadata.session_id.data() << '\"'
                << ",\"build_commit\":\"" << metadata.build_commit.data() << '\"'
                << ",\"config_hash\":\"" << metadata.config_hash.data() << '\"'
                << ",\"engine_hash\":\"" << metadata.engine_hash.data() << '\"'
                << ",\"executable_sha256\":\"" << metadata.executable_sha256.data() << '\"'
                << ",\"tracker_backend\":\"" << metadata.tracker_backend.data() << '\"'
                << ",\"capture_width\":" << metadata.capture_width
                << ",\"capture_height\":" << metadata.capture_height
                << ",\"active_capture_fps\":" << metadata.active_capture_fps
                << ",\"idle_capture_fps\":" << metadata.idle_capture_fps
                << ",\"controller_tick_hz\":" << metadata.controller_tick_hz
                << ",\"telemetry_hz\":" << metadata.telemetry_hz
                << "}\n";
            ++serialized_;
            current_size_ = static_cast<std::size_t>(output_.tellp());
        }
        return true;
    } catch (...) {
        if (!writer_failed_.exchange(true)) ++writer_failures_;
        return false;
    }
}

void RuntimeTelemetry::serialize(const TelemetryRecord& record) {
    if (record.type == TelemetryRecordType::SessionMetadata) {
        session_metadata_ = record;
        has_session_metadata_ = true;
        if (!output_.is_open()) {
            open_next_file();
            return;
        }
    }
    if (!output_.is_open() && !open_next_file()) return;
    const bool has_versioned_controller =
        record.controller.physical_x != 0.0f || record.controller.physical_y != 0.0f ||
        record.controller.manual_x != 0.0f || record.controller.manual_y != 0.0f ||
        record.controller.ai_x != 0.0f || record.controller.ai_y != 0.0f ||
        record.controller.target_final_x != 0.0f ||
        record.controller.target_final_y != 0.0f ||
        record.controller.ai_correction_x != 0.0f ||
        record.controller.ai_correction_y != 0.0f ||
        record.controller.pre_recoil_x != 0.0f || record.controller.pre_recoil_y != 0.0f ||
        record.controller.recoil_x != 0.0f || record.controller.recoil_y != 0.0f ||
        record.controller.final_x != 0.0f || record.controller.final_y != 0.0f;
    const float serialized_manual_x = has_versioned_controller
        ? record.controller.manual_x : record.manual_x;
    const float serialized_manual_y = has_versioned_controller
        ? record.controller.manual_y : record.manual_y;
    const float serialized_ai_x = has_versioned_controller
        ? record.controller.ai_x : record.ai_x;
    const float serialized_ai_y = has_versioned_controller
        ? record.controller.ai_y : record.ai_y;
    const float serialized_final_x = has_versioned_controller
        ? record.controller.final_x : record.final_x;
    const float serialized_final_y = has_versioned_controller
        ? record.controller.final_y : record.final_y;
    output_ << '{'
        << "\"schema_version\":" << record.schema_version
        << ",\"type\":\"" << record_type_name(record.type) << '\"'
        << ",\"legacy_kind\":\"" << kind_name(record.kind) << '\"'
        << ",\"readiness\":\"" << readiness_name(record.readiness) << '\"'
        << ",\"event_id\":" << record.event_id
        << ",\"tick_id\":" << record.tick_id
        << ",\"frame_id\":" << record.frame_id
        << ",\"intent_id\":" << record.intent_id
        << ",\"timestamp_ns\":" << record.timestamp_ns
        << ",\"physical_read_ns\":" << record.timestamps.physical_read_ns
        << ",\"vision_capture_ns\":" << record.timestamps.vision_capture_ns
        << ",\"inference_ready_ns\":" << record.timestamps.inference_ready_ns
        << ",\"controller_consume_ns\":" << record.timestamps.controller_consume_ns
        << ",\"output_sent_ns\":" << record.timestamps.output_sent_ns
        << ",\"sample_ns\":" << record.timestamps.sample_ns
        << ",\"sample_seq\":" << record.sample_seq
        << ",\"target_track_id\":" << record.target_track_id;
    switch (record.type) {
    case TelemetryRecordType::SessionMetadata:
        output_ << ",\"session_id\":\"" << record.session_metadata.session_id.data() << '\"'
            << ",\"build_commit\":\"" << record.session_metadata.build_commit.data() << '\"'
            << ",\"config_hash\":\"" << record.session_metadata.config_hash.data() << '\"'
            << ",\"engine_hash\":\"" << record.session_metadata.engine_hash.data() << '\"'
            << ",\"executable_sha256\":\"" << record.session_metadata.executable_sha256.data() << '\"'
            << ",\"tracker_backend\":\"" << record.session_metadata.tracker_backend.data() << '\"';
        break;
    case TelemetryRecordType::ControllerSample:
        output_ << ",\"physical_connected\":"
            << (record.controller.physical_connected ? "true" : "false")
            << ",\"current_observed_target_present\":"
            << (record.controller.current_observed_target_present ? "true" : "false")
            << ",\"output_delivered\":"
            << (record.controller.output_delivered ? "true" : "false")
            << ",\"output_backend_connected\":"
            << (record.controller.output_backend_connected ? "true" : "false")
            << ",\"output_error_code\":" << record.controller.output_error_code
            << ",\"input_reconnect_count\":" << record.controller.input_reconnect_count
            << ",\"output_reconnect_count\":" << record.controller.output_reconnect_count
            << ",\"physical_x\":" << record.controller.physical_x
            << ",\"physical_y\":" << record.controller.physical_y
            << ",\"manual_x\":" << serialized_manual_x << ",\"manual_y\":" << serialized_manual_y
            << ",\"filtered_manual_x\":" << record.controller.filtered_manual_x
            << ",\"filtered_manual_y\":" << record.controller.filtered_manual_y
            << ",\"manual_confidence\":" << record.controller.manual_confidence
            << ",\"ai_x\":" << serialized_ai_x << ",\"ai_y\":" << serialized_ai_y
            << ",\"target_final\":[" << record.controller.target_final_x
            << ',' << record.controller.target_final_y << ']'
            << ",\"ai_correction\":[" << record.controller.ai_correction_x
            << ',' << record.controller.ai_correction_y << ']'
            << ",\"manual_authority_mode\":\""
            << record.controller.manual_authority_mode.data() << '"'
            << ",\"fresh_vision_validated_manual_proposal_x\":"
            << record.controller.fresh_vision_validated_manual_proposal_x
            << ",\"fresh_vision_validated_manual_proposal_y\":"
            << record.controller.fresh_vision_validated_manual_proposal_y
            << ",\"fresh_vision_validated_ai_proposal_x\":"
            << record.controller.fresh_vision_validated_ai_proposal_x
            << ",\"fresh_vision_validated_ai_proposal_y\":"
            << record.controller.fresh_vision_validated_ai_proposal_y
            // Compatibility aliases: these retain the historical keys but
            // carry validated manual proposal semantics, never output shares.
            << ",\"fresh_vision_effective_manual_x\":"
            << record.controller.fresh_vision_validated_manual_proposal_x
            << ",\"fresh_vision_effective_manual_y\":"
            << record.controller.fresh_vision_validated_manual_proposal_y
            << ",\"fresh_vision_manual_radial_scale\":"
            << record.controller.fresh_vision_manual_radial_scale
            << ",\"fresh_vision_wrong_way_policy_applied\":"
            << (record.controller.fresh_vision_wrong_way_policy_applied
                    ? "true" : "false")
            << ",\"fresh_vision_ai_radial_bound_applied\":"
            << (record.controller.fresh_vision_ai_radial_bound_applied
                    ? "true" : "false")
            << ",\"fresh_vision_ai_radial_scale\":"
            << record.controller.fresh_vision_ai_radial_scale
            << ",\"fresh_vision_predictive_envelope_applied\":"
            << (record.controller.fresh_vision_predictive_envelope_applied
                    ? "true" : "false")
            << ",\"fresh_vision_escape_latched\":"
            << (record.controller.fresh_vision_escape_latched
                    ? "true" : "false")
            << ",\"fresh_vision_authoritative_error_x\":"
            << record.controller.fresh_vision_authoritative_error_x
            << ",\"fresh_vision_authoritative_error_y\":"
            << record.controller.fresh_vision_authoritative_error_y
            << ",\"fresh_vision_predicted_error_x\":"
            << record.controller.fresh_vision_predicted_error_x
            << ",\"fresh_vision_predicted_error_y\":"
            << record.controller.fresh_vision_predicted_error_y
            << ",\"fresh_vision_raw_manual_radial\":"
            << record.controller.fresh_vision_raw_manual_radial
            << ",\"fresh_vision_raw_ai_radial\":"
            << record.controller.fresh_vision_raw_ai_radial
            << ",\"fresh_vision_strongest_valid_radial\":"
            << record.controller.fresh_vision_strongest_valid_radial
            << ",\"fresh_vision_stopping_radial\":"
            << record.controller.fresh_vision_stopping_radial
            << ",\"fresh_vision_permitted_radial\":"
            << record.controller.fresh_vision_permitted_radial
            << ",\"fresh_vision_pre_slew_radial\":"
            << record.controller.fresh_vision_pre_slew_radial
            << ",\"fresh_vision_final_radial\":"
            << record.controller.fresh_vision_final_radial
            << ",\"fresh_vision_horizon_seconds\":"
            << record.controller.fresh_vision_horizon_seconds
            << ",\"fresh_vision_horizon_y_seconds\":"
            << record.controller.fresh_vision_horizon_y_seconds
            << ",\"fresh_vision_max_force_x\":"
            << record.controller.fresh_vision_max_force_x
            << ",\"fresh_vision_max_force_y\":"
            << record.controller.fresh_vision_max_force_y
            << ",\"fresh_vision_envelope_target_x\":"
            << record.controller.fresh_vision_envelope_target_x
            << ",\"fresh_vision_envelope_target_y\":"
            << record.controller.fresh_vision_envelope_target_y
            << ",\"fresh_vision_envelope_reason\":\""
            << record.controller.fresh_vision_envelope_reason.data() << '"'
            << ",\"fresh_vision_envelope_source\":\""
            << record.controller.fresh_vision_envelope_source.data() << '"'
            << ",\"bodylock_error_rate_x\":"
            << record.controller.bodylock_error_rate_x
            << ",\"bodylock_error_rate_y\":"
            << record.controller.bodylock_error_rate_y
            << ",\"bodylock_position_stick_x\":"
            << record.controller.bodylock_position_stick_x
            << ",\"bodylock_position_stick_y\":"
            << record.controller.bodylock_position_stick_y
            << ",\"bodylock_motion_stick_x\":"
            << record.controller.bodylock_motion_stick_x
            << ",\"bodylock_motion_stick_y\":"
            << record.controller.bodylock_motion_stick_y
            << ",\"bodylock_effective_motion_stick_x\":"
            << record.controller.bodylock_effective_motion_stick_x
            << ",\"bodylock_effective_motion_stick_y\":"
            << record.controller.bodylock_effective_motion_stick_y
            << ",\"bodylock_radial_motion_bound\":"
            << (record.controller.bodylock_radial_motion_bound
                    ? "true" : "false")
            << ",\"bodylock_constraint_reason\":\""
            << record.controller.bodylock_constraint_reason.data() << '"'
            << ",\"post_ai_x\":" << record.controller.post_ai_x << ",\"post_ai_y\":" << record.controller.post_ai_y
            << ",\"dynamic_adjustment_x\":" << record.controller.dynamic_adjustment_x << ",\"dynamic_adjustment_y\":" << record.controller.dynamic_adjustment_y
            << ",\"post_dynamic_x\":" << record.controller.post_dynamic_x << ",\"post_dynamic_y\":" << record.controller.post_dynamic_y
            << ",\"ads_brake_x\":" << record.controller.ads_brake_x << ",\"ads_brake_y\":" << record.controller.ads_brake_y
            << ",\"post_ads_brake_x\":" << record.controller.post_ads_brake_x << ",\"post_ads_brake_y\":" << record.controller.post_ads_brake_y
            << ",\"ads_carry_brake_x\":" << record.controller.ads_carry_brake_x << ",\"ads_carry_brake_y\":" << record.controller.ads_carry_brake_y
            << ",\"post_ads_carry_brake_x\":" << record.controller.post_ads_carry_brake_x << ",\"post_ads_carry_brake_y\":" << record.controller.post_ads_carry_brake_y
            << ",\"pre_recoil_x\":" << record.controller.pre_recoil_x
            << ",\"pre_recoil_y\":" << record.controller.pre_recoil_y
            << ",\"recoil_x\":" << record.controller.recoil_x
             << ",\"recoil_y\":" << record.controller.recoil_y
             << ",\"final_x\":" << serialized_final_x << ",\"final_y\":" << serialized_final_y
             << ",\"observed_error_x\":" << record.controller.observed_error_x
             << ",\"observed_error_y\":" << record.controller.observed_error_y
             << ",\"pending_motion_x\":" << record.controller.pending_motion_x
             << ",\"pending_motion_y\":" << record.controller.pending_motion_y
             << ",\"control_error_x\":" << record.controller.control_error_x
             << ",\"control_error_y\":" << record.controller.control_error_y
             << ",\"pending_motion_confidence\":"
             << record.controller.pending_motion_confidence
             << ",\"pending_motion_valid\":"
             << (record.controller.pending_motion_valid ? "true" : "false")
             << ",\"memory_applied\":"
             << (record.controller.memory_applied ? "true" : "false")
             << ",\"memory_status\":\""
             << record.controller.memory_status.data() << '"'
             << ",\"requested_assist_x\":" << record.controller.requested_assist_x
             << ",\"requested_assist_y\":" << record.controller.requested_assist_y
             << ",\"shaped_assist_x\":" << record.controller.shaped_assist_x
             << ",\"shaped_assist_y\":" << record.controller.shaped_assist_y
             << ",\"left_trigger\":" << record.controller.left_trigger
             << ",\"right_trigger\":" << record.controller.right_trigger
             << ",\"controller_target_track_id\":" << record.target_track_id
             << ",\"telemetry_identity_track_id\":" << record.target_track_id
             << ",\"selected_track_id\":" << record.controller.selected_track_id
             << ",\"selected_observation_id\":" << record.controller.selected_observation_id
             << ",\"track_backing_frame_id\":" << record.controller.backing_frame_id
             << ",\"track_observation_age_ms\":" << record.controller.track_observation_age_ms
             << ",\"track_position_sigma\":" << record.controller.track_position_sigma
             << ",\"track_ambiguity\":" << record.controller.track_ambiguity
             << ",\"assist_authority\":\"" << record.controller.assist_authority.data() << '"'
             << ",\"assist_authority_reason\":\"" << record.controller.assist_authority_reason.data() << '"'
             << ",\"bodylock_lifecycle\":\"" << record.controller.bodylock_lifecycle.data() << '"'
             << ",\"bodylock_transition_reason\":\"" << record.controller.bodylock_transition_reason.data() << '"'
             << ",\"assist_limit_reason\":\"" << record.controller.assist_limit_reason.data() << '"'
             << ",\"has_target\":" << (record.controller.has_target ? "true" : "false")
            << ",\"aim_authority\":" << (record.controller.aim_authority ? "true" : "false")
            << ",\"fire_authority\":" << (record.controller.fire_authority ? "true" : "false")
            << ",\"ads_brake_active\":" << (record.controller.ads_brake_active ? "true" : "false")
            << ",\"ads_carry_brake_active\":" << (record.controller.ads_carry_brake_active ? "true" : "false")
            << ",\"ads_completion_active\":" << (record.controller.ads_completion_active ? "true" : "false")
            << ",\"ads_completion_stable_frames\":" << record.controller.ads_completion_stable_frames
            << ",\"ads_completion_radius_px\":" << record.controller.ads_completion_radius_px
            << ",\"ads_completion_required_frames\":" << record.controller.ads_completion_required_frames
            << ",\"ads_completion_max_ms\":" << record.controller.ads_completion_max_ms
            << ",\"ads_completion_reason\":\"" << record.controller.ads_completion_reason.data() << '"'
            << ",\"manual_takeover_active\":" << (record.controller.manual_takeover_active ? "true" : "false")
            << ",\"auto_fire_requested\":" << (record.controller.auto_fire_requested ? "true" : "false")
            << ",\"auto_fire_aim_ready\":" << (record.controller.auto_fire_aim_ready ? "true" : "false")
            << ",\"auto_fire_allowed\":" << (record.controller.auto_fire_allowed ? "true" : "false")
            << ",\"auto_fire_active\":" << (record.controller.auto_fire_active ? "true" : "false")
            << ",\"auto_fire_pulse_starts\":" << record.controller.auto_fire_pulse_starts
            << ",\"auto_fire_pulse_pressed\":" << (record.controller.auto_fire_pulse_pressed ? "true" : "false")
            << ",\"auto_fire_cadence_wait\":" << (record.controller.auto_fire_cadence_wait ? "true" : "false")
            << ",\"final_fire_button\":" << (record.controller.final_fire_button ? "true" : "false")
            << ",\"auto_fire_block_reason\":\"" << record.controller.auto_fire_block_reason.data() << '"'
            << ",\"detector_box_count\":" << record.controller.detector_box_count
            << ",\"production_target_source\":\"" << record.controller.production_target_source.data() << '"'
            << ",\"production_target_tier\":\"" << record.controller.production_target_tier.data() << '"'
            << ",\"production_target_confidence\":" << record.controller.production_target_confidence
            << ",\"target_dx\":" << record.controller.target_dx
            << ",\"target_dy\":" << record.controller.target_dy
            << ",\"target_error_px\":" << record.controller.target_error_px
            << ",\"target_identity_quality\":\"" << identity_quality_name(record.controller.target_identity_quality) << '\"'
            << ",\"aim_mode\":\"" << record.controller.aim_mode.data() << '\"';
        break;
    case TelemetryRecordType::InputEvent:
        output_ << ",\"input_event_kind\":\"" << input_event_name(record.input_event.kind) << '\"'
            << ",\"input_episode_id\":" << record.input_event.input_episode_id
            << ",\"magnitude\":" << record.input_event.magnitude;
        break;
    case TelemetryRecordType::TargetEvent:
        output_ << ",\"target_event\":\"" << target_event_name(record.target_event.event) << '\"'
            << ",\"target_identity_quality\":\"" << identity_quality_name(record.target_event.quality) << '\"'
            << ",\"previous_track_id\":" << record.target_event.previous_track_id;
        break;
    case TelemetryRecordType::AdsTransition:
        output_ << ",\"valid\":" << (record.ads_transition.valid ? "true" : "false")
            << ",\"calibration_class\":\"" << calibration_class_name(record.ads_transition.calibration_class) << '\"'
            << ",\"invalid_reason\":\"" << ads_invalid_reason_name(record.ads_transition.invalid_reason) << '\"'
            << ",\"hipfire_frame_id\":" << record.ads_transition.hipfire_frame_id
            << ",\"settled_frame_id\":" << record.ads_transition.settled_frame_id
            << ",\"hipfire_dx\":" << record.ads_transition.hipfire_dx << ",\"hipfire_dy\":" << record.ads_transition.hipfire_dy
            << ",\"ads_dx\":" << record.ads_transition.ads_dx << ",\"ads_dy\":" << record.ads_transition.ads_dy
            << ",\"delta_dx\":" << record.ads_transition.delta_dx << ",\"delta_dy\":" << record.ads_transition.delta_dy
            << ",\"scale_x\":" << record.ads_transition.scale_x << ",\"scale_y\":" << record.ads_transition.scale_y
            << ",\"offset_x\":" << record.ads_transition.offset_x << ",\"offset_y\":" << record.ads_transition.offset_y
            << ",\"settle_confidence\":" << record.ads_transition.settle_confidence
            << ",\"cumulative_manual\":" << record.ads_transition.cumulative_manual
            << ",\"cumulative_ai\":" << record.ads_transition.cumulative_ai
            << ",\"cumulative_recoil\":" << record.ads_transition.cumulative_recoil;
        break;
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    case TelemetryRecordType::ControlResponseWindow:
        output_ << ",\"response_reason\":\"" << response_reason_name(record.control_response.reason) << '\"'
            << ",\"response_frame_before\":" << record.control_response.frame_id_before
            << ",\"response_frame_after\":" << record.control_response.frame_id_after
            << ",\"delta_error_x\":" << record.control_response.delta_error_x
            << ",\"delta_error_y\":" << record.control_response.delta_error_y
            << ",\"residual_x\":" << record.control_response.residual_x
            << ",\"residual_y\":" << record.control_response.residual_y
            << ",\"manual_x_integral\":" << record.control_response.manual_x_integral
            << ",\"manual_y_integral\":" << record.control_response.manual_y_integral
            << ",\"ai_x_integral\":" << record.control_response.ai_x_integral
            << ",\"ai_y_integral\":" << record.control_response.ai_y_integral
            << ",\"pre_recoil_x_integral\":" << record.control_response.pre_recoil_x_integral
            << ",\"pre_recoil_y_integral\":" << record.control_response.pre_recoil_y_integral
            << ",\"recoil_x_integral\":" << record.control_response.recoil_x_integral
            << ",\"recoil_y_integral\":" << record.control_response.recoil_y_integral
            << ",\"final_x_integral\":" << record.control_response.final_x_integral
             << ",\"final_y_integral\":" << record.control_response.final_y_integral;
        break;
#endif
    case TelemetryRecordType::CommittedCaptureObservation: {
        const auto& value = record.committed_observation;
        const auto& provenance = has_session_metadata_
            ? session_metadata_.session_metadata : record.session_metadata;
        output_ << ",\"schema\":\"causal_response_journal_v1\""
            << ",\"observation\":{"
            << "\"source_frame_id\":" << value.source_frame_id
            << ",\"source_observation_id\":" << value.source_observation_id
            << ",\"persistent_target_id\":" << value.persistent_target_id
            << ",\"viewport_sequence\":" << value.viewport_sequence
            << ",\"viewport_source_frame_id\":" << value.viewport_source_frame_id
            << ",\"captured_at_ns\":" << value.captured_at_ns
            << ",\"result_at_ns\":" << value.result_at_ns
            << ",\"controller_consume_ns\":" << value.controller_consume_ns
            << ",\"stable_error\":[" << value.stable_error_x << ',' << value.stable_error_y << ']'
            << ",\"stable_body_size\":[" << value.stable_body_width << ',' << value.stable_body_height << ']'
            << ",\"raw_body_box\":[" << value.raw_body_x << ',' << value.raw_body_y
            << ',' << value.raw_body_width << ',' << value.raw_body_height << ']'
            << ",\"motion_anchor\":[" << value.motion_anchor_x << ','
            << value.motion_anchor_y << ']'
            << ",\"motion_anchor_score\":" << value.motion_anchor_score
            << ",\"viewport_offset\":[" << value.viewport_offset_x << ',' << value.viewport_offset_y << ']'
            << ",\"target_acceleration\":[" << value.target_acceleration_x << ',' << value.target_acceleration_y << ']'
            << ",\"reliability\":" << value.reliability
            << ",\"normalized_size\":" << value.normalized_size
            << ",\"ads_epoch\":" << value.ads_epoch
            << ",\"eligible_candidate_count\":" << value.eligible_candidate_count
            << ",\"lifecycle\":" << static_cast<unsigned int>(value.lifecycle)
            << ",\"motion\":" << static_cast<unsigned int>(value.motion)
            << ",\"mode\":" << static_cast<unsigned int>(value.mode)
            << ",\"fresh_observed\":" << (value.fresh_observed ? "true" : "false")
            << ",\"strong_observation\":" << (value.strong_observation ? "true" : "false")
            << ",\"stable_coordinates_valid\":" << (value.stable_coordinates_valid ? "true" : "false")
            << ",\"has_motion_anchor\":" << (value.has_motion_anchor ? "true" : "false")
            << ",\"reused_or_projected\":" << (value.reused_or_projected ? "true" : "false")
            << '}'
            << ",\"vision_sample_quality\":\""
            << vision_sample_quality_name(record.vision_sample_quality) << '"'
            << ",\"identification_update_outcome\":\""
            << identification_update_outcome_name(record.identification_update_outcome) << '"'
            << ",\"provenance\":{"
            << "\"build_revision\":\"" << provenance.build_commit.data() << '"'
            << ",\"config_sha256\":\"" << provenance.config_hash.data() << '"'
            << ",\"engine_sha256\":\"" << provenance.engine_hash.data() << '"'
            << ",\"executable_sha256\":\"" << provenance.executable_sha256.data() << '"'
            << ",\"capture_width\":" << provenance.capture_width
            << ",\"capture_height\":" << provenance.capture_height << '}';
        break;
    }
    case TelemetryRecordType::AdsAcquisitionTrace: {
        const auto& value = record.ads_acquisition_trace;
        output_ << ",\"schema\":\"ads_acquisition_trace_v3\""
            << ",\"source_frame_id\":" << value.source_frame_id
            << ",\"source_observation_id\":" << value.source_observation_id
            << ",\"persistent_target_id\":" << value.persistent_target_id
            << ",\"physical_ads_epoch\":" << value.physical_ads_epoch
            << ",\"target_acquisition_id\":" << value.target_acquisition_id
            << ",\"controller_tick_id\":" << value.controller_tick_id
            << ",\"capture_acquire_begin_ns\":" << value.capture_acquire_begin_ns
            << ",\"capture_acquire_complete_ns\":" << value.capture_acquire_complete_ns
            << ",\"capture_copy_complete_ns\":" << value.capture_copy_complete_ns
            << ",\"accumulated_frames\":" << value.accumulated_frames
            << ",\"ads_acquisition_begin_ns\":" << value.ads_acquisition_begin_ns
            << ",\"ads_acquisition_complete_ns\":" << value.ads_acquisition_complete_ns
            << ",\"result_ready_ns\":" << value.result_ready_ns
            << ",\"vision_publish_ns\":" << value.vision_publish_ns
            << ",\"vision_publish_available\":"
            << (value.vision_publish_available ? "true" : "false")
            << ",\"controller_submit_complete_ns\":"
            << value.controller_submit_complete_ns
            << ",\"controller_consume_ns\":" << value.controller_consume_ns
            << ",\"plan_decision_ns\":" << value.plan_decision_ns
            << ",\"plan_decision_available\":"
            << (value.plan_decision_ns != 0 ? "true" : "false")
            << ",\"final_output_ready_ns\":" << value.final_output_ready_ns
            << ",\"final_output_ready_available\":"
            << (value.final_output_ready_ns != 0 ? "true" : "false")
            << ",\"first_requested_ai_ns\":" << value.first_requested_ai_ns
            << ",\"first_shaped_ai_ns\":" << value.first_shaped_ai_ns
            << ",\"first_fused_output_ns\":" << value.first_fused_output_ns
            << ",\"vigem_submit_complete_ns\":" << value.vigem_submit_complete_ns
            << ",\"first_effect_observed_ns\":" << value.first_effect_observed_ns
            << ",\"source_present_available\":"
            << (value.source_present_available ? "true" : "false")
            << ",\"source_present_qpc\":" << value.source_present_qpc
            << ",\"source_present_qpc_frequency\":"
            << value.source_present_qpc_frequency
            << ",\"source_present_clock_domain\":\""
            << (value.source_present_available ? "qpc" : "unavailable") << '\"'
            << ",\"source_present_steady_clock_domain\":\""
            << (value.source_present_steady_available
                ? "qpc_to_steady_calibrated" : "unavailable") << '\"'
            << ",\"source_present_steady_ns\":"
            << value.source_present_steady_ns
            << ",\"source_present_calibration_id\":"
            << value.source_present_calibration_id
            << ",\"source_present_calibration_uncertainty_ns\":"
            << value.source_present_calibration_uncertainty_ns
            << ",\"source_present_steady_available\":"
            << (value.source_present_steady_available ? "true" : "false")
            << ",\"plan_admitted\":"
            << (value.plan_admitted ? "true" : "false")
            << ",\"acquisition_active\":"
            << (value.acquisition_active ? "true" : "false")
            << ",\"acquisition_exists\":"
            << (value.acquisition_exists ? "true" : "false")
            << ",\"acquisition_state\":\""
            << ads_acquisition_state_name(value.acquisition_state) << '"'
            << ",\"decision_reason\":\""
            << ads_decision_reason_name(value.decision_reason) << '"'
            << ",\"source_decision_available\":"
            << (value.source_decision_available ? "true" : "false")
            << ",\"source_decision_outcome\":\""
            << source_decision_outcome_name(value.source_decision_outcome) << '"'
            << ",\"source_decision_reason\":\""
            << ads_decision_reason_name(value.source_decision_reason) << '"'
            << ",\"acquisition_terminal_reason\":\""
            << ads_decision_reason_name(value.acquisition_terminal_reason) << '"'
            << ",\"selector_target_generation\":"
            << value.selector_target_generation
            << ",\"selector_target_changed\":"
            << (value.selector_target_changed ? "true" : "false")
            << ",\"candidate_count\":" << value.candidate_count
            << ",\"preferred_source_id\":" << value.preferred_source_id
            << ",\"selected_source_id\":" << value.selected_source_id
            << ",\"effective_activation_radius_px\":"
            << value.effective_activation_radius_px
            << ",\"raw_error\":[" << value.raw_error_x << ',' << value.raw_error_y << ']'
            << ",\"target_size\":[" << value.target_size_x << ',' << value.target_size_y << ']'
            << ",\"requested_ai\":[" << value.requested_ai_x << ',' << value.requested_ai_y << ']'
            << ",\"shaped_ai\":[" << value.shaped_ai_x << ',' << value.shaped_ai_y << ']'
            << ",\"fused_output\":[" << value.fused_output_x << ',' << value.fused_output_y << ']'
            << ",\"post_output\":[" << value.post_output_x << ',' << value.post_output_y << ']'
            << ",\"has_first_requested_ai\":"
            << (value.has_first_requested_ai ? "true" : "false")
            << ",\"has_first_shaped_ai\":"
            << (value.has_first_shaped_ai ? "true" : "false")
            << ",\"has_first_fused_output\":"
            << (value.has_first_fused_output ? "true" : "false")
            << ",\"first_requested_ai\":["
            << value.first_requested_ai_x << ',' << value.first_requested_ai_y << ']'
            << ",\"first_shaped_ai\":["
            << value.first_shaped_ai_x << ',' << value.first_shaped_ai_y << ']'
            << ",\"first_fused_output\":["
            << value.first_fused_output_x << ',' << value.first_fused_output_y << ']';
        break;
    }
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    case TelemetryRecordType::EgoMotionShadow: {
        const auto& value = record.ego_motion_shadow;
        output_ << ",\"schema\":\"ego_motion_shadow_v2\""
            << ",\"available\":" << (value.available ? "true" : "false")
            << ",\"valid\":" << (value.valid ? "true" : "false")
            << ",\"invalid_reason_code\":"
            << static_cast<unsigned int>(value.invalid_reason)
            << ",\"invalid_reason\":\""
            << ego_motion_invalid_reason_name(value.invalid_reason) << '\"'
            << ",\"result_sequence\":" << value.result_sequence
            << ",\"previous_frame_id\":" << value.previous_frame_id
            << ",\"current_frame_id\":" << value.current_frame_id
            << ",\"previous_present_qpc\":" << value.previous_present_qpc
            << ",\"current_present_qpc\":" << value.current_present_qpc
            << ",\"previous_present_qpc_frequency\":"
            << value.previous_present_qpc_frequency
            << ",\"current_present_qpc_frequency\":"
            << value.current_present_qpc_frequency
            << ",\"present_qpc_frequency\":" << value.present_qpc_frequency
            << ",\"previous_present_steady_ns\":"
            << value.previous_present_steady_ns
            << ",\"current_present_steady_ns\":"
            << value.current_present_steady_ns
            << ",\"previous_present_calibration_id\":"
            << value.previous_present_calibration_id
            << ",\"current_present_calibration_id\":"
            << value.current_present_calibration_id
            << ",\"previous_present_calibration_uncertainty_ns\":"
            << value.previous_present_calibration_uncertainty_ns
            << ",\"current_present_calibration_uncertainty_ns\":"
            << value.current_present_calibration_uncertainty_ns
            << ",\"previous_present_steady_available\":"
            << (value.previous_present_steady_available ? "true" : "false")
            << ",\"current_present_steady_available\":"
            << (value.current_present_steady_available ? "true" : "false")
            << ",\"present_clock_domain\":\""
            << (value.present_clock_valid
                ? "qpc_to_steady_calibrated" : "unavailable") << '\"'
            << ",\"present_clock_valid\":"
            << (value.present_clock_valid ? "true" : "false")
            << ",\"previous_capture_copy_complete_ns\":"
            << value.previous_capture_copy_complete_ns
            << ",\"current_capture_copy_complete_ns\":"
            << value.current_capture_copy_complete_ns
            << ",\"previous_result_ns\":" << value.previous_result_ns
            << ",\"current_result_ns\":" << value.current_result_ns
            << ",\"observer_completed_at_ns\":"
            << value.observer_completed_at_ns
            << ",\"result_age_at_take_ns\":"
            << value.result_age_at_take_ns
            << ",\"background_displacement\":["
            << value.background_dx << ',' << value.background_dy << ']'
            << ",\"camera_displacement\":["
            << value.camera_dx << ',' << value.camera_dy << ']'
            << ",\"confidence\":" << value.confidence
            << ",\"valid_background_ratio\":" << value.valid_background_ratio
            << ",\"residual_px\":" << value.residual_px
            << ",\"compute_ms\":" << value.compute_ms
            << ",\"inlier_count\":" << value.inlier_count
            << ",\"sample_count\":" << value.sample_count
            << ",\"search_radius_px\":" << value.search_radius_px
            << ",\"boundary_hit_count\":" << value.boundary_hit_count
            << ",\"boundary_hit_rate\":" << value.boundary_hit_rate
            << ",\"boundary_consistent_hit_count\":"
            << value.boundary_consistent_hit_count
            << ",\"boundary_consistent_hit_rate\":"
            << value.boundary_consistent_hit_rate
            << ",\"observer_lifecycle_generation\":"
            << value.observer_lifecycle_generation
            << ",\"submitted_frame_count\":"
            << value.submitted_frame_count
            << ",\"pending_frame_replaced_count\":"
            << value.pending_frame_replaced_count
            << ",\"pairs_processed_count\":"
            << value.pairs_processed_count
            << ",\"unread_result_replaced_count\":"
            << value.unread_result_replaced_count
            << ",\"duplicate_or_out_of_order_rejected_count\":"
            << value.duplicate_or_out_of_order_rejected_count;
        break;
    }
#endif
    // Gate2.5 records use RuntimeTelemetry's dedicated bounded channel. The
    // old ordinary-record serializer is retained only in the source history
    // while the new serializer below owns this type.
#if 0
    case TelemetryRecordType::Gate25LiveShadow: {
        if (record.gate25_anomaly) {
            const auto& value = record.gate25_payload.anomaly;
            output_ << ",\"schema\":\"w5_gate2_5a_anomaly_v1\""
                << ",\"anomaly\":true"
                << ",\"source_frame_id\":" << value.source_frame_id
                << ",\"source_observation_id\":" << value.source_observation_id
                << ",\"persistent_target_id\":" << value.persistent_target_id
                << ",\"present_steady_ns\":" << value.present_steady_ns
                << ",\"decision_ns\":" << value.decision_ns
                << ",\"controller_tick_id\":" << value.controller_tick_id
                << ",\"backend_epoch\":" << value.backend_epoch
                << ",\"delivery_first_seq\":" << value.delivery_first_seq
                << ",\"delivery_last_seq\":" << value.delivery_last_seq
                << ",\"reason\":\"" << gate25_reason_name(value.reason) << '\"'
                << ",\"target_delta\":[" << value.target_delta_x << ','
                << value.target_delta_y << ']'
                << ",\"observed_camera_work\":["
                << value.observed_camera_work_x << ','
                << value.observed_camera_work_y << ']'
                << ",\"snr\":" << value.snr
                << ",\"reliability\":" << value.reliability
                << ",\"target_confidence\":" << value.target_confidence;
        } else {
            const auto& value = record.gate25_payload.aggregate;
            output_ << ",\"schema\":\"w5_gate2_5a_aggregate_v1\""
                << ",\"anomaly\":false"
                << ",\"window_begin_ns\":" << value.window_begin_ns
                << ",\"window_end_ns\":" << value.window_end_ns
                << ",\"last_controller_tick_id\":"
                << value.last_controller_tick_id
                << ",\"delivery_first_seq\":" << value.delivery_first_seq
                << ",\"delivery_last_seq\":" << value.delivery_last_seq
                << ",\"status\":\"" << gate25_status_name(value.status) << '\"'
                << ",\"observations\":" << value.observations
                << ",\"compatible_pairs\":" << value.compatible_pairs
                << ",\"effect_valid\":" << value.effect_valid
                << ",\"neutral_baseline_pairs\":" << value.neutral_baseline_pairs
                << ",\"ledger_comparisons\":" << value.ledger_comparisons
                << ",\"snr_pass\":" << value.snr_pass
                << ",\"below_noise_floor\":" << value.below_noise_floor
                << ",\"duplicate\":" << value.duplicate
                << ",\"same_present_endpoint\":"
                << value.same_present_endpoint
                << ",\"stale\":" << value.stale
                << ",\"backward_present\":" << value.backward_present
                << ",\"capture_gap\":" << value.capture_gap
                << ",\"identity_boundary\":" << value.identity_boundary
                << ",\"selector_boundary\":" << value.selector_boundary
                << ",\"ads_boundary\":" << value.ads_boundary
                << ",\"viewport_boundary\":" << value.viewport_boundary
                << ",\"backend_boundary\":" << value.backend_boundary
                << ",\"target_acquisition_boundary\":"
                << value.target_acquisition_boundary
                << ",\"response_delay_boundary\":"
                << value.response_delay_boundary
                << ",\"present_calibration_boundary\":"
                << value.present_calibration_boundary
                << ",\"response_delay_source\":\""
                << gate25_response_delay_source_name(value.response_delay_source)
                << '"'
                << ",\"missing_present_clock\":" << value.missing_present_clock
                << ",\"missing_delivery_history\":" << value.missing_delivery_history
                << ",\"incomplete_delivery_history\":" << value.incomplete_delivery_history
                << ",\"missing_response_delay\":" << value.missing_response_delay
                << ",\"cancellation_or_reversal\":"
                << value.cancellation_or_reversal
                << ",\"invalid_geometry\":" << value.invalid_geometry
                << ",\"exogenous_rejected\":" << value.exogenous_rejected
                << ",\"target_motion_contamination\":"
                << value.target_motion_contamination
                << ",\"missing_neutral_baseline\":"
                << value.missing_neutral_baseline
                << ",\"no_target_identity\":" << value.no_target_identity
                << ",\"response_model_unavailable\":"
                << value.response_model_unavailable
                << ",\"profile_identity_unavailable\":"
                << value.profile_identity_unavailable
                << ",\"saturation_rows\":" << value.saturation_rows
                << ",\"model_insufficient\":" << value.model_insufficient
                << ",\"continuous_zoh_coverage_rows\":"
                << value.continuous_zoh_coverage_rows
                << ",\"fixed_180hz_coverage_rows\":"
                << value.fixed_180hz_coverage_rows
                << ",\"fixed_240hz_coverage_rows\":"
                << value.fixed_240hz_coverage_rows
                << ",\"observed_present_cadence_coverage_rows\":"
                << value.observed_present_cadence_coverage_rows
                << ",\"manual_only_rows\":" << value.manual_only_rows
                << ",\"ai_only_rows\":" << value.ai_only_rows
                << ",\"mixed_rows\":" << value.mixed_rows
                << ",\"response_260ms_rows\":" << value.response_260ms_rows
                << ",\"response_400ms_rows\":" << value.response_400ms_rows
                << ",\"baseline_median\":[" << value.baseline_median_x << ','
                << value.baseline_median_y << ']'
                << ",\"baseline_mad\":[" << value.baseline_mad_x << ','
                << value.baseline_mad_y << ']'
                << ",\"observed_sum\":[" << value.observed_sum_x << ','
                << value.observed_sum_y << ']'
                << ",\"residual_abs_sum\":" << value.residual_abs_sum
                << ",\"residual_abs_max\":" << value.residual_abs_max
                << ",\"anomaly_count\":" << value.anomaly_count
                << ",\"anomaly_dropped\":" << value.anomaly_dropped
                << ",\"output_magnitude_bins\":[";
            for (std::size_t i = 0; i < value.output_magnitude_bins.size(); ++i) {
                if (i != 0) output_ << ',';
                output_ << value.output_magnitude_bins[i];
            }
            output_ << "],\"output_axis_bins\":[";
            for (std::size_t i = 0; i < value.output_axis_bins.size(); ++i) {
                if (i != 0) output_ << ',';
                output_ << value.output_axis_bins[i];
            }
            output_ << "],\"cohort_profile_incomplete\":"
                << (value.cohort_profile_incomplete ? "true" : "false")
                << ",\"command_bin_upper_bounds_percent\":[1.5,2.5,4.0,7.5,12.5]"
                << ",\"cohort_grid_columns\":[\"mode\",\"axis_bin\",\"command_bin\","
                   "\"pair_count\",\"valid_count\",\"invalid_count\","
                   "\"snr_pass_count\",\"below_noise_count\","
                   "\"attempted_delivered_x\",\"attempted_delivered_y\","
                   "\"attempted_delivered_abs\",\"attempted_delivered_count\","
                   "\"delivered_x\",\"delivered_y\","
                   "\"observed_x\",\"observed_y\",\"observed_abs\","
                   "\"residual_sum\",\"residual_max\",\"residual_count\"]"
                << ",\"cohort_grid\":[";
            bool emitted_cohort = false;
            for (std::size_t index = 0; index < value.cohort_grid.size(); ++index) {
                const auto& cell = value.cohort_grid[index];
                if (cell.pair_count == 0) continue;
                if (emitted_cohort) output_ << ',';
                emitted_cohort = true;
                const std::size_t mode = index /
                    (kGate25AxisCapacity * kGate25CommandBinCapacity);
                const std::size_t remainder = index %
                    (kGate25AxisCapacity * kGate25CommandBinCapacity);
                const std::size_t axis = remainder / kGate25CommandBinCapacity;
                const std::size_t command_bin = remainder % kGate25CommandBinCapacity;
                output_ << '[' << mode << ',' << axis << ',' << command_bin
                    << ',' << cell.pair_count
                    << ',' << cell.valid_count
                    << ',' << cell.invalid_count
                    << ',' << cell.snr_pass_count
                    << ',' << cell.below_noise_count
                    << ',' << cell.attempted_delivered_final_sum_x
                    << ',' << cell.attempted_delivered_final_sum_y
                    << ',' << cell.attempted_delivered_final_abs_sum
                    << ',' << cell.attempted_delivered_count
                    << ',' << cell.delivered_final_sum_x
                    << ',' << cell.delivered_final_sum_y
                    << ',' << cell.observed_camera_sum_x
                    << ',' << cell.observed_camera_sum_y
                    << ',' << cell.observed_camera_abs_sum
                    << ',' << cell.ledger_residual_sum
                    << ',' << cell.ledger_residual_max
                    << ',' << cell.ledger_residual_count << ']';
            }
            output_ << ']';
        }
        break;
    }
#endif
    case TelemetryRecordType::Gate25LiveShadow:
        break;
    case TelemetryRecordType::DeliveredControlSample: {
        const auto& value = record.delivered_control;
        const auto& provenance = has_session_metadata_
            ? session_metadata_.session_metadata : record.session_metadata;
        output_ << ",\"schema\":\"causal_response_journal_v1\""
            << ",\"control\":{"
            << "\"sample_seq\":" << value.sample_seq
            << ",\"applied_at_ns\":" << value.applied_at_ns
            << ",\"physical_right\":[" << value.physical_right_x << ',' << value.physical_right_y << ']'
            << ",\"physical_left\":[" << value.physical_left_x << ',' << value.physical_left_y << ']'
            << ",\"manual\":[" << value.manual_x << ',' << value.manual_y << ']'
            << ",\"ai\":[" << value.ai_x << ',' << value.ai_y << ']'
            << ",\"pre_recoil\":[" << value.pre_recoil_x << ',' << value.pre_recoil_y << ']'
            << ",\"recoil\":[" << value.recoil_x << ',' << value.recoil_y << ']'
            << ",\"final_right\":[" << value.final_right_x << ',' << value.final_right_y << ']'
            << ",\"final_left\":[" << value.final_left_x << ',' << value.final_left_y << ']'
            << ",\"ads_epoch\":" << value.ads_epoch
            << ",\"output_delivered\":" << (value.output_delivered ? "true" : "false")
            << ",\"output_disabled\":" << (value.output_disabled ? "true" : "false")
            << ",\"firing\":" << (value.firing ? "true" : "false")
            << ",\"recoil_active\":" << (value.recoil_active ? "true" : "false")
            << ",\"saturated\":" << (value.saturated ? "true" : "false") << '}'
            << ",\"provenance\":{"
            << "\"build_revision\":\"" << provenance.build_commit.data() << '"'
            << ",\"config_sha256\":\"" << provenance.config_hash.data() << '"'
            << ",\"engine_sha256\":\"" << provenance.engine_hash.data() << '"'
            << ",\"executable_sha256\":\"" << provenance.executable_sha256.data() << '"'
            << ",\"capture_width\":" << provenance.capture_width
            << ",\"capture_height\":" << provenance.capture_height << '}';
        break;
    }
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    case TelemetryRecordType::CausalResponseShadow:
        output_ << ",\"schema\":\"causal_response_shadow_v3\""
            << ",\"best_delay_ms\":" << record.causal_shadow.best_delay_ms
            << ",\"selected_delay_ms\":" << record.causal_shadow.selected_delay_ms
            << ",\"selected_delay_confidence\":"
            << record.causal_shadow.selected_delay_confidence
            << ",\"right_confidence\":" << record.causal_shadow.right_confidence
            << ",\"left_confidence\":" << record.causal_shadow.left_confidence
            << ",\"joint_confidence\":" << record.causal_shadow.joint_confidence
            << ",\"excitation\":" << record.causal_shadow.excitation
            << ",\"residual\":" << record.causal_shadow.residual
            << ",\"pending_realized\":[" << record.causal_shadow.pending_realized_x
            << ',' << record.causal_shadow.pending_realized_y << ']'
            << ",\"pending_in_flight\":[" << record.causal_shadow.pending_in_flight_x
            << ',' << record.causal_shadow.pending_in_flight_y << ']'
            << ",\"pending_scheduled\":[" << record.causal_shadow.pending_scheduled_x
            << ',' << record.causal_shadow.pending_scheduled_y << ']'
            << ",\"pending_total\":[" << record.causal_shadow.pending_total_x
            << ',' << record.causal_shadow.pending_total_y << ']'
            << ",\"pending_confidence\":" << record.causal_shadow.pending_confidence
            << ",\"reason_bits\":" << record.causal_shadow.reason_bits
            << ",\"accepted_delay_count\":"
            << static_cast<unsigned int>(record.causal_shadow.accepted_delay_count)
            << ",\"accepted_by_any_delay\":"
            << (record.causal_shadow.accepted_by_any_delay ? "true" : "false")
            << ",\"delay_switch_pending\":"
            << (record.causal_shadow.delay_switch_pending ? "true" : "false")
            << ",\"pending_valid\":"
            << (record.causal_shadow.pending_valid ? "true" : "false")
            << ",\"rollout_valid\":"
            << (record.causal_shadow.rollout_valid ? "true" : "false")
            << ",\"rollout_best_scale\":"
            << record.causal_shadow.rollout_best_scale
            << ",\"rollout_confidence\":"
            << record.causal_shadow.rollout_confidence
            << ",\"rollout_uses_final_output\":"
            << (record.causal_shadow.rollout_uses_final_output ? "true" : "false")
            << ",\"rollout_final_output\":["
            << record.causal_shadow.rollout_final_output_x << ','
            << record.causal_shadow.rollout_final_output_y << ']'
            << ",\"rollout_candidates\":[";
        for (std::size_t i = 0;
             i < record.causal_shadow.rollout_candidate_count && i < 5; ++i) {
            if (i) output_ << ',';
            output_ << "{\"scale\":" << record.causal_shadow.rollout_scales[i]
                << ",\"cost\":" << record.causal_shadow.rollout_costs[i] << '}';
        }
        output_ << ']'
            << ",\"vision_sample_quality\":\""
            << vision_sample_quality_name(record.vision_sample_quality) << '"'
            << ",\"identification_update_outcome\":\""
            << identification_update_outcome_name(record.identification_update_outcome) << '"';
        break;
#endif
    default: break;
    }
    output_ << ",\"first_seq\":" << record.completeness.first_seq
        << ",\"last_seq\":" << record.completeness.last_seq
        << ",\"expected\":" << record.completeness.expected
        << ",\"written\":" << record.completeness.written
        << ",\"dropped\":" << record.completeness.dropped
        << ",\"complete\":" << (record.completeness.complete ? "true" : "false")
        << "}\n";
    finish_serialized_line();
}

void RuntimeTelemetry::finish_serialized_line() {
    ++serialized_;
    current_size_ = static_cast<std::size_t>(output_.tellp());
    if (options_.rotate_size_bytes > 0 && current_size_ >= options_.rotate_size_bytes) {
        open_next_file();
    }
}

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
void RuntimeTelemetry::serialize_gate25_anomaly(const Gate25Anomaly& value) {
    if (!output_.is_open() && !open_next_file()) return;
    output_ << '{'
        << "\"schema_version\":" << kTelemetrySchemaVersion
        << ",\"type\":\"w5_gate2_5a_live_shadow\""
        << ",\"schema\":\"w5_gate2_5a_anomaly_v1\""
        << ",\"anomaly\":true"
        << ",\"source_frame_id\":" << value.source_frame_id
        << ",\"source_observation_id\":" << value.source_observation_id
        << ",\"persistent_target_id\":" << value.persistent_target_id
        << ",\"present_steady_ns\":" << value.present_steady_ns
        << ",\"decision_ns\":" << value.decision_ns
        << ",\"controller_tick_id\":" << value.controller_tick_id
        << ",\"backend_epoch\":" << value.backend_epoch
        << ",\"delivery_first_seq\":" << value.delivery_first_seq
        << ",\"delivery_last_seq\":" << value.delivery_last_seq
        << ",\"reason\":\"" << gate25_reason_name(value.reason) << '"'
        << ",\"target_delta\":[" << value.target_delta_x << ','
        << value.target_delta_y << ']'
        << ",\"observed_camera_work\":["
        << value.observed_camera_work_x << ','
        << value.observed_camera_work_y << ']'
        << ",\"snr\":" << value.snr
        << ",\"reliability\":" << value.reliability
        << ",\"target_confidence\":" << value.target_confidence
        << ",\"first_seq\":0,\"last_seq\":0,\"expected\":0"
        << ",\"written\":0,\"dropped\":0,\"complete\":true}\n";
    ++gate25_serialized_;
    finish_serialized_line();
}

void RuntimeTelemetry::serialize_gate25_aggregate(
    const Gate25AggregateTransport& summary) {
    if (!output_.is_open() && !open_next_file()) return;
    const auto& value = summary.scalar;
    output_ << '{'
        << "\"schema_version\":" << kTelemetrySchemaVersion
        << ",\"type\":\"w5_gate2_5a_live_shadow\""
        << ",\"schema\":\"w5_gate2_5a_aggregate_v1\""
        << ",\"anomaly\":false"
        << ",\"window_begin_ns\":" << value.window_begin_ns
        << ",\"window_end_ns\":" << value.window_end_ns
        << ",\"window_clock_domain\":\""
        << gate25_window_clock_domain_name(value.window_clock_domain) << '"'
        << ",\"last_controller_tick_id\":"
        << value.last_controller_tick_id
        << ",\"delivery_first_seq\":" << value.delivery_first_seq
        << ",\"delivery_last_seq\":" << value.delivery_last_seq
        << ",\"status\":\"" << gate25_status_name(value.status) << '"'
        << ",\"observations\":" << value.observations
        << ",\"compatible_pairs\":" << value.compatible_pairs
        << ",\"effect_valid\":" << value.effect_valid
        << ",\"neutral_baseline_pairs\":" << value.neutral_baseline_pairs
        << ",\"ledger_comparisons\":" << value.ledger_comparisons
        << ",\"snr_pass\":" << value.snr_pass
        << ",\"below_noise_floor\":" << value.below_noise_floor
        << ",\"duplicate\":" << value.duplicate
        << ",\"same_present_endpoint\":" << value.same_present_endpoint
        << ",\"stale\":" << value.stale
        << ",\"backward_present\":" << value.backward_present
        << ",\"capture_gap\":" << value.capture_gap
        << ",\"identity_boundary\":" << value.identity_boundary
        << ",\"selector_boundary\":" << value.selector_boundary
        << ",\"ads_boundary\":" << value.ads_boundary
        << ",\"mode_boundary\":" << value.mode_boundary
        << ",\"viewport_boundary\":" << value.viewport_boundary
        << ",\"backend_boundary\":" << value.backend_boundary
        << ",\"target_acquisition_boundary\":"
        << value.target_acquisition_boundary
        << ",\"response_delay_boundary\":"
        << value.response_delay_boundary
        << ",\"present_calibration_boundary\":"
        << value.present_calibration_boundary
        << ",\"response_delay_source\":\""
        << gate25_response_delay_source_name(value.response_delay_source)
        << '"'
        << ",\"missing_present_clock\":" << value.missing_present_clock
        << ",\"missing_delivery_history\":" << value.missing_delivery_history
        << ",\"incomplete_delivery_history\":"
        << value.incomplete_delivery_history
        << ",\"missing_response_delay\":" << value.missing_response_delay
        << ",\"cancellation_or_reversal\":"
        << value.cancellation_or_reversal
        << ",\"invalid_geometry\":" << value.invalid_geometry
        << ",\"exogenous_rejected\":" << value.exogenous_rejected
        << ",\"target_motion_contamination\":"
        << value.target_motion_contamination
        << ",\"missing_neutral_baseline\":"
        << value.missing_neutral_baseline
        << ",\"no_target_identity\":" << value.no_target_identity
        << ",\"response_model_unavailable\":"
        << value.response_model_unavailable
        << ",\"profile_identity_unavailable\":"
        << value.profile_identity_unavailable
        << ",\"saturation_rows\":" << value.saturation_rows
        << ",\"model_insufficient\":" << value.model_insufficient
        << ",\"continuous_zoh_coverage_rows\":"
        << value.continuous_zoh_coverage_rows
        << ",\"fixed_180hz_coverage_rows\":"
        << value.fixed_180hz_coverage_rows
        << ",\"fixed_240hz_coverage_rows\":"
        << value.fixed_240hz_coverage_rows
        << ",\"observed_present_cadence_coverage_rows\":"
        << value.observed_present_cadence_coverage_rows
        << ",\"manual_only_rows\":" << value.manual_only_rows
        << ",\"ai_only_rows\":" << value.ai_only_rows
        << ",\"mixed_rows\":" << value.mixed_rows
        << ",\"response_260ms_rows\":" << value.response_260ms_rows
        << ",\"response_400ms_rows\":" << value.response_400ms_rows
        << ",\"baseline_median\":[" << value.baseline_median_x << ','
        << value.baseline_median_y << ']'
        << ",\"baseline_mad\":[" << value.baseline_mad_x << ','
        << value.baseline_mad_y << ']'
        << ",\"observed_sum\":[" << value.observed_sum_x << ','
        << value.observed_sum_y << ']'
        << ",\"residual_abs_sum\":" << value.residual_abs_sum
        << ",\"residual_abs_max\":" << value.residual_abs_max
        << ",\"anomaly_count\":" << value.anomaly_count
        << ",\"anomaly_dropped\":" << value.anomaly_dropped
        << ",\"cohort_profile_incomplete\":"
        << (value.cohort_profile_incomplete ? "true" : "false")
        << ",\"command_bin_upper_bounds_percent\":[1.5,2.5,4.0,7.5,12.5]"
        << ",\"cohort_grid_columns\":[\"mode\",\"axis_bin\",\"command_bin\","
           "\"pair_count\",\"valid_count\",\"invalid_count\","
           "\"snr_pass_count\",\"below_noise_count\","
           "\"attempted_delivered_x\",\"attempted_delivered_y\","
           "\"attempted_delivered_abs\",\"attempted_delivered_count\","
           "\"delivered_x\",\"delivered_y\","
           "\"delivered_abs\",\"delivered_energy\","
           "\"observed_x\",\"observed_y\",\"observed_abs\","
           "\"observed_energy\",\"delivered_observed_dot\","
           "\"delivered_observed_cross\",\"sign_agree\","
           "\"sign_disagree\","
           "\"residual_sum\",\"residual_max\",\"residual_count\"]"
        << ",\"cohort_grid_active_count\":" << summary.active_cell_count
        << ",\"cohort_grid_overflow\":"
        << (summary.active_cell_overflow ? "true" : "false")
        << ",\"cohort_grid\":[";
    for (std::size_t position = 0; position < summary.active_cell_count; ++position) {
        if (position != 0) output_ << ',';
        const std::size_t index = summary.cell_indices[position];
        const auto& cell = summary.cells[position];
        const std::size_t mode = index /
            (kGate25AxisCapacity * kGate25CommandBinCapacity);
        const std::size_t remainder = index %
            (kGate25AxisCapacity * kGate25CommandBinCapacity);
        const std::size_t axis = remainder / kGate25CommandBinCapacity;
        const std::size_t command_bin = remainder % kGate25CommandBinCapacity;
        output_ << '[' << mode << ',' << axis << ',' << command_bin
            << ',' << cell.pair_count << ',' << cell.valid_count
            << ',' << cell.invalid_count << ',' << cell.snr_pass_count
            << ',' << cell.below_noise_count
            << ',' << cell.attempted_delivered_final_sum_x
            << ',' << cell.attempted_delivered_final_sum_y
            << ',' << cell.attempted_delivered_final_abs_sum
            << ',' << cell.attempted_delivered_count
             << ',' << cell.delivered_final_sum_x
             << ',' << cell.delivered_final_sum_y
             << ',' << cell.delivered_final_abs_sum
             << ',' << cell.delivered_final_energy_sum
             << ',' << cell.observed_camera_sum_x
             << ',' << cell.observed_camera_sum_y
             << ',' << cell.observed_camera_abs_sum
             << ',' << cell.observed_camera_energy_sum
             << ',' << cell.delivered_observed_dot_sum
             << ',' << cell.delivered_observed_cross_sum
             << ',' << cell.component_sign_agree_count
             << ',' << cell.component_sign_disagree_count
             << ',' << cell.ledger_residual_sum
            << ',' << cell.ledger_residual_max
            << ',' << cell.ledger_residual_count << ']';
    }
    output_ << "],\"first_seq\":0,\"last_seq\":0,\"expected\":0"
        << ",\"written\":0,\"dropped\":0,\"complete\":true}\n";
    ++gate25_serialized_;
    finish_serialized_line();
}
#endif

void RuntimeTelemetry::writer_loop() {
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    bool prefer_gate25 = true;
    while (true) {
        TelemetryRecord record;
        Gate25AggregateTransport gate25_aggregate;
        Gate25Anomaly gate25_anomaly;
        bool take_gate25 = false;
        bool take_gate25_anomaly = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                const bool gate_pending = gate25_transport_ != nullptr &&
                    (gate25_transport_->aggregate_count != 0 ||
                     gate25_transport_->anomaly_count != 0);
                return !running_.load() || queue_count_ > 0 ||
                    gate_pending;
            });
            const bool gate_pending = gate25_transport_ != nullptr &&
                (gate25_transport_->aggregate_count != 0 ||
                 gate25_transport_->anomaly_count != 0);
            if (queue_count_ == 0 && !gate_pending) {
                if (!running_.load()) break;
                continue;
            }
            const auto deadline_ns = shutdown_deadline_ns_.load();
            const auto now_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (!running_.load() && deadline_ns != 0 && now_ns >= deadline_ns) {
                dropped_normal_.fetch_add(queue_count_);
                if (gate25_transport_) {
                    gate25_dropped_.fetch_add(
                        gate25_transport_->aggregate_count +
                        gate25_transport_->anomaly_count);
                    gate25_transport_->aggregate_count = 0;
                    gate25_transport_->anomaly_count = 0;
                    gate25_transport_->aggregate_head =
                        gate25_transport_->aggregate_tail;
                    gate25_transport_->anomaly_head =
                        gate25_transport_->anomaly_tail;
                }
                queue_count_ = 0;
                queue_head_ = queue_tail_;
                break;
            }
            const bool take_gate = gate_pending &&
                (queue_count_ == 0 || prefer_gate25);
            if (!take_gate && queue_count_ != 0) {
                record = queue_[queue_head_];
                queue_head_ = (queue_head_ + 1) % queue_.size();
                --queue_count_;
                prefer_gate25 = true;
            } else {
                take_gate25 = true;
                auto& transport = *gate25_transport_;
                // Aggregate snapshots are kept on their own channel.  This
                // lets a summary plus the configured anomaly batch fit
                // without coupling their capacities or copying a dense
                // aggregate into every anomaly slot.
                if (transport.aggregate_count != 0) {
                    gate25_aggregate =
                        transport.aggregate_queue[transport.aggregate_head];
                    transport.aggregate_head =
                        (transport.aggregate_head + 1) %
                        kGate25AggregateQueueCapacity;
                    --transport.aggregate_count;
                } else {
                    gate25_anomaly =
                        transport.anomaly_queue[transport.anomaly_head];
                    transport.anomaly_head =
                        (transport.anomaly_head + 1) %
                        kGate25AnomalyQueueCapacity;
                    --transport.anomaly_count;
                    take_gate25_anomaly = true;
                }
                prefer_gate25 = false;
            }
        }
        if (take_gate25) {
            if (take_gate25_anomaly) {
                serialize_gate25_anomaly(gate25_anomaly);
            } else {
                serialize_gate25_aggregate(gate25_aggregate);
            }
        } else {
            serialize(record);
        }
    }
#else
    while (true) {
        TelemetryRecord record;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] {
                return !running_.load() || queue_count_ > 0;
            });
            if (queue_count_ == 0) {
                if (!running_.load()) break;
                continue;
            }
            const auto deadline_ns = shutdown_deadline_ns_.load();
            const auto now_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (!running_.load() && deadline_ns != 0 && now_ns >= deadline_ns) {
                dropped_normal_.fetch_add(queue_count_);
                queue_count_ = 0;
                queue_head_ = queue_tail_;
                break;
            }
            record = queue_[queue_head_];
            queue_head_ = (queue_head_ + 1) % queue_.size();
            --queue_count_;
        }
        serialize(record);
    }
#endif
}

} // namespace runtime_app
