#include "runtime_telemetry.h"

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
    case TelemetryRecordType::ControlResponseWindow: return "control_response_window";
    case TelemetryRecordType::ControllerSample:
    default: return "controller_sample";
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

RuntimeTelemetryCounters RuntimeTelemetry::counters() const noexcept {
    return RuntimeTelemetryCounters{
        accepted_.load(), dropped_normal_.load(), dropped_critical_.load(),
        duplicate_vision_.load(), serialized_.load(), writers_started_.load(),
        writer_failures_.load(), high_watermark_.load()};
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
            << ",\"ai_x\":" << serialized_ai_x << ",\"ai_y\":" << serialized_ai_y
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
    default: break;
    }
    output_ << ",\"first_seq\":" << record.completeness.first_seq
        << ",\"last_seq\":" << record.completeness.last_seq
        << ",\"expected\":" << record.completeness.expected
        << ",\"written\":" << record.completeness.written
        << ",\"dropped\":" << record.completeness.dropped
        << ",\"complete\":" << (record.completeness.complete ? "true" : "false")
        << "}\n";
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
            condition_.wait(lock, [this] { return !running_.load() || queue_count_ > 0; });
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
