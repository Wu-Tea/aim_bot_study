#include "runtime_telemetry.h"

#include "../pipeline_contract/target_plan.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <string>

namespace runtime_app {
namespace {

const char* record_type_name(TelemetryRecordType type) {
    switch (type) {
    case TelemetryRecordType::SessionMetadata: return "session_metadata";
    case TelemetryRecordType::InputEvent: return "input_event";
    case TelemetryRecordType::TargetEvent: return "target_event";
    case TelemetryRecordType::AdsTransitionSample: return "ads_transition_sample";
    case TelemetryRecordType::AdsTransition: return "ads_transition";
    case TelemetryRecordType::CommittedCaptureObservation:
        return "committed_capture_observation";
    case TelemetryRecordType::DeliveredControlSample: return "delivered_control_sample";
    case TelemetryRecordType::AdsAcquisitionTrace: return "ads_acquisition_trace";
    case TelemetryRecordType::ControllerSample:
    default: return "controller_sample";
    }
}

const char* ads_acquisition_state_name(std::uint8_t value) {
    using State = pipeline_contract::AdsAcquisitionState;
    switch (static_cast<State>(value)) {
    case State::ArmedWaitingForTarget: return "armed_waiting_for_target";
    case State::AcquiringNominal: return "acquiring_nominal";
    case State::AcquiringExtended: return "acquiring_extended";
    case State::AcquiringManualSafe: return "acquiring_manual_safe";
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
    case Reason::InvalidSelectorProtocol: return "invalid_selector_protocol";
    case Reason::SelectorNoSelection: return "selector_no_selection";
    case Reason::OutsideAssociationRadius: return "outside_association_radius";
    case Reason::StaleCapture: return "stale_capture";
    case Reason::DuplicateFrame: return "duplicate_frame";
    case Reason::OldControlEpoch: return "old_control_epoch";
    case Reason::LowReliability: return "low_reliability";
    case Reason::FriendlyOrCueReject: return "friendly_or_cue_reject";
    case Reason::BodylockOutsideContinuation: return "bodylock_outside_continuation";
    case Reason::AdsAlreadyConsumed: return "ads_already_consumed";
    case Reason::AcquisitionCeiling: return "acquisition_ceiling";
    case Reason::ExtensionBudgetElapsed: return "extension_budget_elapsed";
    case Reason::Settled: return "settled";
    case Reason::CenterCross: return "center_cross";
    case Reason::NonHelpfulOutput: return "non_helpful_output";
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

const char* vision_sample_quality_name(VisionSampleQuality value) {
    switch (value) {
    case VisionSampleQuality::SoftWeight: return "soft_weight";
    case VisionSampleQuality::HardReject: return "hard_reject";
    case VisionSampleQuality::Normal:
    default: return "normal";
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

} // namespace

RuntimeTelemetry::RuntimeTelemetry(RuntimeTelemetryOptions options)
    : options_(std::move(options)) {
    options_.queue_capacity = std::max<std::size_t>(1, options_.queue_capacity);
    options_.max_files = std::max<std::size_t>(1, options_.max_files);
    if (options_.enabled) queue_.resize(options_.queue_capacity);
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
    if (record.type == TelemetryRecordType::CommittedCaptureObservation &&
        record.frame_id != 0) {
        if (record.frame_id <= last_vision_frame_id_) {
            ++duplicate_source_;
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

RuntimeTelemetryCounters RuntimeTelemetry::counters() const noexcept {
    return RuntimeTelemetryCounters{
        accepted_.load(), dropped_normal_.load(), dropped_critical_.load(),
        duplicate_source_.load(), serialized_.load(), writers_started_.load(),
        writer_failures_.load(), high_watermark_.load()};
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
    output_ << '{'
        << "\"schema_version\":" << record.schema_version
        << ",\"type\":\"" << record_type_name(record.type) << '\"'
        << ",\"readiness\":\"" << readiness_name(record.readiness) << '\"'
        << ",\"event_id\":" << record.event_id
        << ",\"tick_id\":" << record.tick_id
        << ",\"frame_id\":" << record.frame_id
        << ",\"intent_id\":" << record.intent_id
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
            ;
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
            << ",\"manual_x\":" << record.controller.manual_x
            << ",\"manual_y\":" << record.controller.manual_y
            << ",\"filtered_manual_x\":" << record.controller.filtered_manual_x
            << ",\"filtered_manual_y\":" << record.controller.filtered_manual_y
            << ",\"manual_confidence\":" << record.controller.manual_confidence
            << ",\"ai_x\":" << record.controller.ai_x
            << ",\"ai_y\":" << record.controller.ai_y
            << ",\"target_final\":[" << record.controller.target_final_x
            << ',' << record.controller.target_final_y << ']'
            << ",\"ai_correction\":[" << record.controller.ai_correction_x
             << ',' << record.controller.ai_correction_y << ']'
             << ",\"manual_authority_mode\":\""
             << record.controller.manual_authority_mode.data() << '"'
             << ",\"assist_control_phase\":\""
             << record.controller.assist_control_phase.data() << '"'
             << ",\"operation_class\":\""
             << record.controller.operation_class.data() << '"'
             << ",\"operation_confidence\":"
             << record.controller.operation_confidence
             << ",\"direction_trust\":"
             << record.controller.direction_trust
             << ",\"recoil_pull_strength\":"
             << record.controller.recoil_pull_strength
             << ",\"manual_passthrough_x\":"
             << (record.controller.manual_passthrough_x ? "true" : "false")
             << ",\"manual_passthrough_y\":"
             << (record.controller.manual_passthrough_y ? "true" : "false")
             << ",\"manual_correction_x\":"
             << (record.controller.manual_correction_x ? "true" : "false")
             << ",\"manual_correction_y\":"
             << (record.controller.manual_correction_y ? "true" : "false")
             << ",\"manual_boundary_x\":"
             << (record.controller.manual_boundary_x ? "true" : "false")
             << ",\"manual_boundary_y\":"
             << (record.controller.manual_boundary_y ? "true" : "false")
             << ",\"manual_exit_requested\":"
             << (record.controller.manual_exit_requested ? "true" : "false")
             << ",\"handover_requested\":"
             << (record.controller.handover_requested ? "true" : "false")
             << ",\"handover_braking\":"
             << (record.controller.handover_braking ? "true" : "false")
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
            << ",\"pre_recoil_x\":" << record.controller.pre_recoil_x
            << ",\"pre_recoil_y\":" << record.controller.pre_recoil_y
            << ",\"recoil_x\":" << record.controller.recoil_x
             << ",\"recoil_y\":" << record.controller.recoil_y
             << ",\"final_x\":" << record.controller.final_x
             << ",\"final_y\":" << record.controller.final_y
             << ",\"observed_error_x\":" << record.controller.observed_error_x
             << ",\"observed_error_y\":" << record.controller.observed_error_y
             << ",\"control_error_x\":" << record.controller.control_error_x
             << ",\"control_error_y\":" << record.controller.control_error_y
             << ",\"source_aim\":[" << record.controller.source_aim_x
             << ',' << record.controller.source_aim_y << ']'
             << ",\"desired_aim\":[" << record.controller.desired_aim_x
             << ',' << record.controller.desired_aim_y << ']'
             << ",\"desired_point_normalized\":["
             << record.controller.desired_point_u << ','
             << record.controller.desired_point_v << ']'
             << ",\"aim_region\":[" << record.controller.aim_region_x1
             << ',' << record.controller.aim_region_y1 << ','
             << record.controller.aim_region_x2 << ','
             << record.controller.aim_region_y2 << ']'
             << ",\"has_aim_region\":"
             << (record.controller.has_aim_region ? "true" : "false")
             << ",\"visual_authority\":"
             << record.controller.visual_authority
             << ",\"enemy_cue_current\":"
             << (record.controller.enemy_cue_current ? "true" : "false")
             << ",\"enemy_identity_confirmed\":"
             << (record.controller.enemy_identity_confirmed ? "true" : "false")
             << ",\"enemy_cue_checked\":"
             << (record.controller.enemy_cue_checked ? "true" : "false")
             << ",\"aim_region_source\":\""
             << record.controller.aim_region_source.data() << '"'
             << ",\"desired_point_source\":\""
             << record.controller.desired_point_source.data() << '"'
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
             << ",\"assist_authority\":\"" << record.controller.assist_authority.data() << '"'
             << ",\"assist_authority_reason\":\"" << record.controller.assist_authority_reason.data() << '"'
              << ",\"bodylock_lifecycle\":\"" << record.controller.bodylock_lifecycle.data() << '"'
             << ",\"assist_limit_reason\":\"" << record.controller.assist_limit_reason.data() << '"'
             << ",\"has_target\":" << (record.controller.has_target ? "true" : "false")
            << ",\"aim_authority\":" << (record.controller.aim_authority ? "true" : "false")
             << ",\"fire_authority\":" << (record.controller.fire_authority ? "true" : "false")
            << ",\"auto_fire_requested\":" << (record.controller.auto_fire_requested ? "true" : "false")
            << ",\"auto_fire_aim_ready\":" << (record.controller.auto_fire_aim_ready ? "true" : "false")
            << ",\"auto_fire_allowed\":" << (record.controller.auto_fire_allowed ? "true" : "false")
            << ",\"auto_fire_active\":" << (record.controller.auto_fire_active ? "true" : "false")
            << ",\"auto_fire_pulse_starts\":" << record.controller.auto_fire_pulse_starts
            << ",\"auto_fire_pulse_pressed\":" << (record.controller.auto_fire_pulse_pressed ? "true" : "false")
            << ",\"auto_fire_cadence_wait\":" << (record.controller.auto_fire_cadence_wait ? "true" : "false")
            << ",\"final_fire_button\":" << (record.controller.final_fire_button ? "true" : "false")
            << ",\"auto_fire_block_reason\":\"" << record.controller.auto_fire_block_reason.data() << '"'
            << ",\"enemy_mark_request_pending\":"
            << (record.controller.enemy_mark_request_pending ? "true" : "false")
            << ",\"enemy_mark_synthetic_pressed\":"
            << (record.controller.enemy_mark_synthetic_pressed ? "true" : "false")
            << ",\"enemy_mark_fired\":"
            << (record.controller.enemy_mark_fired ? "true" : "false")
            << ",\"enemy_mark_canceled\":"
            << (record.controller.enemy_mark_canceled ? "true" : "false")
            << ",\"enemy_mark_confirmation_frames\":"
            << record.controller.enemy_mark_confirmation_frames
            << ",\"enemy_mark_target_scope\":"
            << record.controller.enemy_mark_target_scope
            << ",\"enemy_mark_target_generation\":"
            << record.controller.enemy_mark_target_generation
            << ",\"enemy_mark_last_scope\":"
            << record.controller.enemy_mark_last_scope
            << ",\"enemy_mark_last_generation\":"
            << record.controller.enemy_mark_last_generation
            << ",\"enemy_mark_block_reason\":\""
            << record.controller.enemy_mark_block_reason.data() << '"'
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
    case TelemetryRecordType::CommittedCaptureObservation: {
        const auto& value = record.committed_observation;
        const auto& provenance = has_session_metadata_
            ? session_metadata_.session_metadata : record.session_metadata;
        output_ << ",\"schema\":\"committed_capture_observation_v1\""
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
            << '}'
            << ",\"vision_sample_quality\":\""
            << vision_sample_quality_name(record.vision_sample_quality) << '"'
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
    case TelemetryRecordType::DeliveredControlSample: {
        const auto& value = record.delivered_control;
        // High-rate delivery timeline only. Controller decomposition lives in
        // controller_sample and runtime identity lives in session_metadata.
        output_ << ",\"schema\":\"delivered_control_sample_v2\""
            << ",\"control\":{"
            << "\"sample_seq\":" << value.sample_seq
            << ",\"applied_at_ns\":" << value.applied_at_ns
            << ",\"final_right\":[" << value.final_right_x << ',' << value.final_right_y << ']'
            << ",\"final_left\":[" << value.final_left_x << ',' << value.final_left_y << ']'
            << ",\"ads_epoch\":" << value.ads_epoch
            << ",\"output_delivered\":" << (value.output_delivered ? "true" : "false")
            << ",\"output_disabled\":" << (value.output_disabled ? "true" : "false")
            << ",\"firing\":" << (value.firing ? "true" : "false")
            << ",\"recoil_active\":" << (value.recoil_active ? "true" : "false")
            << ",\"saturated\":" << (value.saturated ? "true" : "false") << '}';
        break;
    }
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


void RuntimeTelemetry::writer_loop() {
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
}

} // namespace runtime_app
