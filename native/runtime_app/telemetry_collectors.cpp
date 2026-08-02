#include "telemetry_collectors.h"

#include "ads_transition_collector.h"
#include "control_response_window.h"
#include "telemetry_event_sampler.h"
#include "telemetry_target_identity.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

namespace runtime_app {

struct TelemetryCollectors::State {
    TelemetryTargetIdentity identity;
    TelemetryEventSampler sampler{{250, 100, 100, 300}};
    UserInputEpisodeCollector episodes{{0.10f, 0.05f, 12'000'000}};
    ControlResponseWindowAssembler responses;
    AdsTransitionCollector ads;
    std::uint64_t next_sample_seq = 1;
    std::uint64_t next_ads_vision_seq = 1;
    std::uint64_t last_ring_sample_ns = 0;
    std::uint64_t last_tick_ns = 0;
    std::uint64_t last_delivered_record_ns = 0;
    std::uint64_t delivered_record_interval_ns = 4'000'000;
    unsigned int last_input_reconnect_count = 0;
    unsigned int last_output_reconnect_count = 0;
    bool has_reconnect_counts = false;
    std::uint64_t ads_epoch = 0;
    bool last_aiming = false;
    bool has_last_aiming = false;
    bool has_target = false;
    std::uint64_t target_track_id = 0;
    TargetIdentityQuality target_identity_quality = TargetIdentityQuality::None;
    float target_dx = 0.0f;
    float target_dy = 0.0f;
    std::uint32_t detector_box_count = 0;
    float target_confidence = 0.0f;
    std::array<char, 32> target_source{};
    std::array<char, 24> target_tier{};
};

namespace {
template <std::size_t N>
void copy_text(std::array<char, N>& destination, const char* source) {
    if (source == nullptr) source = "unknown";
    std::snprintf(destination.data(), destination.size(), "%s", source);
}
}

TelemetryCollectors::TelemetryCollectors(
    bool enabled,
    RuntimeTelemetry* sink,
    TelemetrySessionContext context)
    : sink_(sink) {
    if (enabled && sink_ != nullptr) {
        state_ = std::make_unique<State>();
        const int requested_hz = context.telemetry_hz > 0
            ? context.telemetry_hz : 250;
        const int persisted_hz = std::clamp(requested_hz, 1, 250);
        state_->delivered_record_interval_ns = static_cast<std::uint64_t>(
            1'000'000'000ull / static_cast<unsigned int>(persisted_hz));
        TelemetryRecord metadata;
        metadata.type = TelemetryRecordType::SessionMetadata;
        metadata.critical = true;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::random_device random;
        const auto random_bits = (static_cast<std::uint64_t>(random()) << 32) ^ random();
        std::snprintf(metadata.session_metadata.session_id.data(),
            metadata.session_metadata.session_id.size(), "%016llx%016llx",
            static_cast<unsigned long long>(now),
            static_cast<unsigned long long>(random_bits));
        copy_text(metadata.session_metadata.build_commit, context.build_commit);
        copy_text(metadata.session_metadata.config_hash, context.config_hash);
        copy_text(metadata.session_metadata.engine_hash, context.engine_hash);
        copy_text(metadata.session_metadata.executable_sha256, context.executable_sha256);
        copy_text(metadata.session_metadata.tracker_backend, context.tracker_backend);
        metadata.session_metadata.capture_width = context.capture_width;
        metadata.session_metadata.capture_height = context.capture_height;
        metadata.session_metadata.active_capture_fps = context.active_capture_fps;
        metadata.session_metadata.idle_capture_fps = context.idle_capture_fps;
        metadata.session_metadata.controller_tick_hz = context.controller_tick_hz;
        metadata.session_metadata.telemetry_hz = context.telemetry_hz;
        enqueue(metadata);
    }
}

TelemetryCollectors::~TelemetryCollectors() = default;

bool TelemetryCollectors::enabled() const noexcept { return state_ != nullptr; }

void TelemetryCollectors::observe_tick(const TelemetryTickInput& input) noexcept {
    if (!state_) return;
    State& state = *state_;
    const bool aim_started = input.aiming && (!state.has_last_aiming || !state.last_aiming);
    const bool aim_stopped = !input.aiming && state.has_last_aiming && state.last_aiming;
    if (aim_started) {
        ++state.ads_epoch;
        state.ads.on_ads_pressed(input.sample_ns);
        state.sampler.trigger(InputEventKind::AdsPressed, input.sample_ns);
        ++counters_.state_transitions;
    } else if (aim_stopped) {
        state.sampler.trigger(InputEventKind::AdsReleased, input.sample_ns);
        ++counters_.state_transitions;
    }
    state.last_aiming = input.aiming;
    state.has_last_aiming = true;

    ResponseControllerSample command;
    command.sample_seq = input.tick_id;
    command.output_sent_ns = input.output_sent_ns;
    command.physical_x = input.physical_x; command.physical_y = input.physical_y;
    command.physical_left_x = input.physical_left_x;
    command.physical_left_y = input.physical_left_y;
    command.manual_x = input.manual_x; command.manual_y = input.manual_y;
    command.ai_x = input.ai_x; command.ai_y = input.ai_y;
    command.pre_recoil_x = input.pre_recoil_x; command.pre_recoil_y = input.pre_recoil_y;
    command.recoil_x = input.recoil_x; command.recoil_y = input.recoil_y;
    command.final_x = input.final_x; command.final_y = input.final_y;
    command.final_left_x = input.final_left_x;
    command.final_left_y = input.final_left_y;
    command.ads_epoch = state.ads_epoch;
    command.output_delivered = input.output_delivered;
    command.output_disabled = input.output_disabled;
    command.firing = input.final_fire_button;
    command.recoil_active = std::hypot(input.recoil_x, input.recoil_y) > 1.0e-5f;
    command.saturated = input.output_saturated;
    state.responses.observe_controller(command);

    const bool reconnect_changed = state.has_reconnect_counts &&
        (input.input_reconnect_count != state.last_input_reconnect_count ||
         input.output_reconnect_count != state.last_output_reconnect_count);
    const bool delivered_interval_elapsed =
        state.last_delivered_record_ns == 0 ||
        input.sample_ns < state.last_delivered_record_ns ||
        input.sample_ns - state.last_delivered_record_ns >=
            state.delivered_record_interval_ns;
    const bool persist_delivered = delivered_interval_elapsed || aim_started ||
        aim_stopped || !input.output_delivered || input.output_error_code != 0 ||
        reconnect_changed;
    if (persist_delivered) {
        TelemetryRecord delivered_record;
        delivered_record.type = TelemetryRecordType::DeliveredControlSample;
        delivered_record.tick_id = input.tick_id;
        delivered_record.sample_seq = input.tick_id;
        delivered_record.timestamps.output_sent_ns = input.output_sent_ns;
        auto& delivered = delivered_record.delivered_control;
        delivered.sample_seq = input.tick_id;
        delivered.applied_at_ns = input.output_sent_ns;
        delivered.physical_right_x = input.physical_x;
        delivered.physical_right_y = input.physical_y;
        delivered.physical_left_x = input.physical_left_x;
        delivered.physical_left_y = input.physical_left_y;
        delivered.manual_x = input.manual_x;
        delivered.manual_y = input.manual_y;
        delivered.ai_x = input.ai_x;
        delivered.ai_y = input.ai_y;
        delivered.pre_recoil_x = input.pre_recoil_x;
        delivered.pre_recoil_y = input.pre_recoil_y;
        delivered.recoil_x = input.recoil_x;
        delivered.recoil_y = input.recoil_y;
        delivered.final_right_x = input.final_x;
        delivered.final_right_y = input.final_y;
        delivered.final_left_x = input.final_left_x;
        delivered.final_left_y = input.final_left_y;
        delivered.ads_epoch = state.ads_epoch;
        delivered.output_delivered = input.output_delivered;
        delivered.output_disabled = input.output_disabled;
        delivered.firing = input.final_fire_button;
        delivered.recoil_active = command.recoil_active;
        delivered.saturated = input.output_saturated;
        enqueue(delivered_record);
        state.last_delivered_record_ns = input.sample_ns;
        ++counters_.delivered_control_records;
    }
    state.last_input_reconnect_count = input.input_reconnect_count;
    state.last_output_reconnect_count = input.output_reconnect_count;
    state.has_reconnect_counts = true;

    const float dt = state.last_tick_ns != 0 && input.sample_ns > state.last_tick_ns
        ? static_cast<float>(input.sample_ns - state.last_tick_ns) / 1'000'000'000.0f : 0.0f;
    state.last_tick_ns = input.sample_ns;
    state.ads.observe_command(
        std::hypot(input.manual_x, input.manual_y) * dt,
        std::hypot(input.ai_x, input.ai_y) * dt,
        std::hypot(input.recoil_x, input.recoil_y) * dt);
    state.ads.on_tick(input.sample_ns);

    if (state.last_ring_sample_ns == 0 ||
        input.sample_ns - state.last_ring_sample_ns >= 4'000'000) {
        state.last_ring_sample_ns = input.sample_ns;
        EventSample sample;
        sample.sample_seq = state.next_sample_seq++;
        sample.timestamp_ns = input.sample_ns;
        sample.target_track_id = input.selected_track_id;
        sample.timestamps.physical_read_ns = input.physical_read_ns;
        sample.timestamps.controller_consume_ns = input.controller_consume_ns;
        sample.timestamps.output_sent_ns = input.output_sent_ns;
        sample.timestamps.sample_ns = input.sample_ns;
        sample.controller.physical_connected = input.physical_connected;
        sample.controller.current_observed_target_present =
            input.current_observed_target_present;
        sample.controller.output_delivered = input.output_delivered;
        sample.controller.output_backend_connected = input.output_backend_connected;
        sample.controller.output_error_code = input.output_error_code;
        sample.controller.input_reconnect_count = input.input_reconnect_count;
        sample.controller.output_reconnect_count = input.output_reconnect_count;
        sample.controller.physical_x = input.physical_x;
        sample.controller.physical_y = input.physical_y;
        sample.controller.manual_x = input.manual_x;
        sample.controller.manual_y = input.manual_y;
        sample.controller.filtered_manual_x = input.filtered_manual_x;
        sample.controller.filtered_manual_y = input.filtered_manual_y;
        sample.controller.manual_confidence = input.manual_confidence;
        sample.controller.ai_x = input.ai_x;
        sample.controller.ai_y = input.ai_y;
        sample.controller.fresh_vision_validated_manual_proposal_x =
            input.fresh_vision_validated_manual_proposal_x;
        sample.controller.fresh_vision_validated_manual_proposal_y =
            input.fresh_vision_validated_manual_proposal_y;
        sample.controller.fresh_vision_validated_ai_proposal_x =
            input.fresh_vision_validated_ai_proposal_x;
        sample.controller.fresh_vision_validated_ai_proposal_y =
            input.fresh_vision_validated_ai_proposal_y;
        sample.controller.fresh_vision_manual_radial_scale =
            input.fresh_vision_manual_radial_scale;
        sample.controller.fresh_vision_wrong_way_policy_applied =
            input.fresh_vision_wrong_way_policy_applied;
        sample.controller.fresh_vision_ai_radial_bound_applied =
            input.fresh_vision_ai_radial_bound_applied;
        sample.controller.fresh_vision_ai_radial_scale =
            input.fresh_vision_ai_radial_scale;
        sample.controller.fresh_vision_predictive_envelope_applied =
            input.fresh_vision_predictive_envelope_applied;
        sample.controller.fresh_vision_escape_latched =
            input.fresh_vision_escape_latched;
        sample.controller.fresh_vision_authoritative_error_x =
            input.fresh_vision_authoritative_error_x;
        sample.controller.fresh_vision_authoritative_error_y =
            input.fresh_vision_authoritative_error_y;
        sample.controller.fresh_vision_predicted_error_x =
            input.fresh_vision_predicted_error_x;
        sample.controller.fresh_vision_predicted_error_y =
            input.fresh_vision_predicted_error_y;
        sample.controller.fresh_vision_raw_manual_radial =
            input.fresh_vision_raw_manual_radial;
        sample.controller.fresh_vision_raw_ai_radial =
            input.fresh_vision_raw_ai_radial;
        sample.controller.fresh_vision_strongest_valid_radial =
            input.fresh_vision_strongest_valid_radial;
        sample.controller.fresh_vision_stopping_radial =
            input.fresh_vision_stopping_radial;
        sample.controller.fresh_vision_permitted_radial =
            input.fresh_vision_permitted_radial;
        sample.controller.fresh_vision_pre_slew_radial =
            input.fresh_vision_pre_slew_radial;
        sample.controller.fresh_vision_final_radial =
            input.fresh_vision_final_radial;
        sample.controller.fresh_vision_horizon_seconds =
            input.fresh_vision_horizon_seconds;
        sample.controller.fresh_vision_horizon_y_seconds =
            input.fresh_vision_horizon_y_seconds;
        sample.controller.fresh_vision_max_force_x =
            input.fresh_vision_max_force_x;
        sample.controller.fresh_vision_max_force_y =
            input.fresh_vision_max_force_y;
        sample.controller.fresh_vision_envelope_target_x =
            input.fresh_vision_envelope_target_x;
        sample.controller.fresh_vision_envelope_target_y =
            input.fresh_vision_envelope_target_y;
        copy_text(
            sample.controller.fresh_vision_envelope_reason,
            input.fresh_vision_envelope_reason);
        copy_text(
            sample.controller.fresh_vision_envelope_source,
            input.fresh_vision_envelope_source);
        sample.controller.bodylock_error_rate_x = input.bodylock_error_rate_x;
        sample.controller.bodylock_error_rate_y = input.bodylock_error_rate_y;
        sample.controller.bodylock_position_stick_x =
            input.bodylock_position_stick_x;
        sample.controller.bodylock_position_stick_y =
            input.bodylock_position_stick_y;
        sample.controller.bodylock_motion_stick_x =
            input.bodylock_motion_stick_x;
        sample.controller.bodylock_motion_stick_y =
            input.bodylock_motion_stick_y;
        sample.controller.bodylock_effective_motion_stick_x =
            input.bodylock_effective_motion_stick_x;
        sample.controller.bodylock_effective_motion_stick_y =
            input.bodylock_effective_motion_stick_y;
        sample.controller.bodylock_radial_motion_bound =
            input.bodylock_radial_motion_bound;
        copy_text(
            sample.controller.bodylock_constraint_reason,
            input.bodylock_constraint_reason);
        sample.controller.requested_assist_x = input.requested_assist_x;
        sample.controller.requested_assist_y = input.requested_assist_y;
        sample.controller.shaped_assist_x = input.shaped_assist_x;
        sample.controller.shaped_assist_y = input.shaped_assist_y;
        sample.controller.post_ai_x = input.post_ai_x;
        sample.controller.post_ai_y = input.post_ai_y;
        sample.controller.dynamic_adjustment_x = input.dynamic_adjustment_x;
        sample.controller.dynamic_adjustment_y = input.dynamic_adjustment_y;
        sample.controller.post_dynamic_x = input.post_dynamic_x;
        sample.controller.post_dynamic_y = input.post_dynamic_y;
        sample.controller.ads_brake_x = input.ads_brake_x;
        sample.controller.ads_brake_y = input.ads_brake_y;
        sample.controller.post_ads_brake_x = input.post_ads_brake_x;
        sample.controller.post_ads_brake_y = input.post_ads_brake_y;
        sample.controller.ads_carry_brake_x = input.ads_carry_brake_x;
        sample.controller.ads_carry_brake_y = input.ads_carry_brake_y;
        sample.controller.post_ads_carry_brake_x = input.post_ads_carry_brake_x;
        sample.controller.post_ads_carry_brake_y = input.post_ads_carry_brake_y;
        sample.controller.pre_recoil_x = input.pre_recoil_x;
        sample.controller.pre_recoil_y = input.pre_recoil_y;
        sample.controller.recoil_x = input.recoil_x;
        sample.controller.recoil_y = input.recoil_y;
        sample.controller.final_x = input.final_x;
        sample.controller.final_y = input.final_y;
        sample.controller.remaining_work_x = input.remaining_work_x;
        sample.controller.remaining_work_y = input.remaining_work_y;
        sample.controller.delivered_camera_work_x =
            input.delivered_camera_work_x;
        sample.controller.delivered_camera_work_y =
            input.delivered_camera_work_y;
        sample.controller.remaining_work_confidence =
            input.remaining_work_confidence;
        sample.controller.remaining_work_valid = input.remaining_work_valid;
        sample.controller.selected_track_id = input.selected_track_id;
        sample.controller.selected_observation_id = input.selected_observation_id;
        sample.controller.backing_frame_id = input.backing_frame_id;
        sample.controller.track_observation_age_ms = input.track_observation_age_ms;
        sample.controller.track_position_sigma = input.track_position_sigma;
        sample.controller.track_ambiguity = input.track_ambiguity;
        sample.controller.left_trigger = input.left_trigger;
        sample.controller.right_trigger = input.right_trigger;
        sample.controller.has_target = state.has_target;
        sample.controller.aim_authority = input.aim_authority;
        sample.controller.fire_authority = input.fire_authority;
        sample.controller.ads_brake_active = input.ads_brake_active;
        sample.controller.ads_carry_brake_active = input.ads_carry_brake_active;
        sample.controller.ads_completion_active = input.ads_completion_active;
        sample.controller.ads_completion_stable_frames = input.ads_completion_stable_frames;
        sample.controller.ads_completion_radius_px = input.ads_completion_radius_px;
        sample.controller.ads_completion_required_frames = input.ads_completion_required_frames;
        sample.controller.ads_completion_max_ms = input.ads_completion_max_ms;
        copy_text(sample.controller.ads_completion_reason, input.ads_completion_reason);
        sample.controller.auto_fire_requested = input.auto_fire_requested;
        sample.controller.auto_fire_aim_ready = input.auto_fire_aim_ready;
        sample.controller.auto_fire_allowed = input.auto_fire_allowed;
        sample.controller.auto_fire_active = input.auto_fire_active;
        sample.controller.auto_fire_pulse_starts = input.auto_fire_pulse_starts;
        sample.controller.auto_fire_pulse_pressed = input.auto_fire_pulse_pressed;
        sample.controller.auto_fire_cadence_wait = input.auto_fire_cadence_wait;
        sample.controller.final_fire_button = input.final_fire_button;
        copy_text(
            sample.controller.auto_fire_block_reason,
            input.auto_fire_block_reason);
        copy_text(sample.controller.assist_authority, input.assist_authority);
        copy_text(sample.controller.assist_authority_reason, input.assist_authority_reason);
        copy_text(sample.controller.bodylock_lifecycle, input.bodylock_lifecycle);
        copy_text(
            sample.controller.bodylock_transition_reason,
            input.bodylock_transition_reason);
        copy_text(sample.controller.assist_limit_reason, input.assist_limit_reason);
        sample.controller.manual_takeover_active = input.manual_takeover_active;
        sample.controller.detector_box_count = state.detector_box_count;
        sample.controller.production_target_confidence = state.target_confidence;
        sample.controller.target_dx = state.target_dx;
        sample.controller.target_dy = state.target_dy;
        sample.controller.target_error_px = std::hypot(state.target_dx, state.target_dy);
        sample.controller.target_identity_quality = state.target_identity_quality;
        copy_text(sample.controller.aim_mode, input.aim_mode);
        copy_text(sample.controller.production_target_source, state.target_source.data());
        copy_text(sample.controller.production_target_tier, state.target_tier.data());
        state.sampler.observe(sample);
        for (const auto& event : state.episodes.observe(sample)) {
            TelemetryRecord record;
            record.type = TelemetryRecordType::InputEvent;
            record.event_id = event.input_episode_id;
            record.sample_seq = event.sample_seq;
            record.timestamps.sample_ns = event.timestamp_ns;
            record.input_event = {event.kind, event.input_episode_id, event.magnitude};
            record.readiness = TelemetryReadiness::ProfileEligible;
            enqueue(record);
        }
    }

    for (const auto& sample : state.sampler.drain()) {
        TelemetryRecord record;
        record.type = TelemetryRecordType::ControllerSample;
        record.tick_id = input.tick_id;
        record.sample_seq = sample.sample_seq;
        record.target_track_id = sample.target_track_id;
        record.timestamps = sample.timestamps;
        record.controller = sample.controller;
        record.readiness = TelemetryReadiness::ProfileEligible;
        enqueue(record);
    }
    flush_ads_event();
}

void TelemetryCollectors::observe_new_vision(const TelemetryVisionInput& input) noexcept {
    if (!state_ || input.frame_id == 0) return;
    State& state = *state_;
    TargetIdentityObservation observation;
    observation.frame_id = input.frame_id;
    observation.frame_width = input.frame_width;
    observation.frame_height = input.frame_height;
    observation.live = input.has_target && input.live;
    observation.projected = input.projected;
    observation.explicit_switch = input.explicit_switch;
    observation.association_ambiguous = input.association_ambiguous;
    observation.x1 = input.x1; observation.y1 = input.y1;
    observation.x2 = input.x2; observation.y2 = input.y2;
    observation.target_x = input.target_x; observation.target_y = input.target_y;
    const TargetIdentityResult identity = state.identity.observe(observation);
    state.has_target = input.has_target;
    state.target_track_id = identity.track_id;
    state.target_identity_quality = identity.quality;
    state.target_dx = input.has_target ? input.target_x - input.screen_center_x : 0.0f;
    state.target_dy = input.has_target ? input.target_y - input.screen_center_y : 0.0f;
    state.detector_box_count = input.detector_box_count;
    state.target_confidence = input.target_confidence;
    copy_text(state.target_source, input.target_source);
    copy_text(state.target_tier, input.target_tier);
    if (identity.event != TargetEventKind::None) {
        TelemetryRecord record;
        record.type = TelemetryRecordType::TargetEvent;
        record.critical = true;
        record.frame_id = input.frame_id;
        record.target_track_id = identity.track_id;
        record.timestamps.vision_capture_ns = input.captured_at_ns;
        record.target_event = {identity.event, identity.quality, identity.previous_track_id};
        enqueue(record);
        ++counters_.state_transitions;
    }

    ResponseVisionFrame response_frame;
    response_frame.frame_id = input.frame_id;
    response_frame.captured_at_ns = input.captured_at_ns;
    response_frame.inferred_at_ns = input.inferred_at_ns;
    response_frame.controller_consume_ns = input.controller_consume_ns;
    response_frame.frame_width = input.frame_width;
    response_frame.frame_height = input.frame_height;
    response_frame.target_track_id = identity.track_id;
    response_frame.identity_quality = identity.quality;
    response_frame.live = input.has_target && input.live;
    response_frame.dx = input.target_x - input.screen_center_x;
    response_frame.dy = input.target_y - input.screen_center_y;
    response_frame.predicted_motion_x = input.predicted_motion_x;
    response_frame.predicted_motion_y = input.predicted_motion_y;
    if (const auto response = state.responses.observe_vision(response_frame)) {
        TelemetryRecord record;
        record.type = TelemetryRecordType::ControlResponseWindow;
        record.target_track_id = response->target_track_id;
        record.readiness = response->readiness;
        record.completeness = response->completeness;
        record.control_response.reason = response->reason;
        record.control_response.frame_id_before = response->frame_id_before;
        record.control_response.frame_id_after = response->frame_id_after;
        record.control_response.delta_error_x = response->delta_error_x;
        record.control_response.delta_error_y = response->delta_error_y;
        record.control_response.residual_x = response->residual_x;
        record.control_response.residual_y = response->residual_y;
        record.control_response.manual_x_integral = response->manual_x_integral;
        record.control_response.manual_y_integral = response->manual_y_integral;
        record.control_response.ai_x_integral = response->ai_x_integral;
        record.control_response.ai_y_integral = response->ai_y_integral;
        record.control_response.pre_recoil_x_integral = response->pre_recoil_x_integral;
        record.control_response.pre_recoil_y_integral = response->pre_recoil_y_integral;
        record.control_response.recoil_x_integral = response->recoil_x_integral;
        record.control_response.recoil_y_integral = response->recoil_y_integral;
        record.control_response.final_x_integral = response->final_x_integral;
        record.control_response.final_y_integral = response->final_y_integral;
        enqueue(record);
    }

    AdsVisualFrame ads_frame;
    ads_frame.frame_id = input.frame_id;
    ads_frame.sample_seq = state.next_ads_vision_seq++;
    ads_frame.captured_at_ns = input.captured_at_ns;
    ads_frame.target_track_id = identity.track_id;
    ads_frame.identity_quality = identity.quality;
    ads_frame.live = input.has_target && input.live;
    ads_frame.x1 = input.x1; ads_frame.y1 = input.y1;
    ads_frame.x2 = input.x2; ads_frame.y2 = input.y2;
    ads_frame.target_x = input.target_x; ads_frame.target_y = input.target_y;
    ads_frame.screen_center_x = input.screen_center_x;
    ads_frame.screen_center_y = input.screen_center_y;
    ads_frame.motion_residual_px = input.motion_residual_px;
    if (!input.aiming) state.ads.observe_hipfire(ads_frame);
    else state.ads.observe_vision(ads_frame);
    flush_ads_event();
}

void TelemetryCollectors::observe_committed_capture(
    const pipeline_contract::CommittedCaptureObservation& observation) noexcept {
    if (!state_ || !pipeline_contract::valid(observation)) return;
    TelemetryRecord record;
    record.type = TelemetryRecordType::CommittedCaptureObservation;
    record.frame_id = observation.source_frame_id;
    record.target_track_id = observation.persistent_target_id;
    record.timestamps.vision_capture_ns = observation.captured_at_ns;
    record.timestamps.inference_ready_ns = observation.result_at_ns;
    record.vision_sample_quality = observation.strong_observation
        ? VisionSampleQuality::Normal : VisionSampleQuality::SoftWeight;
    record.identification_update_outcome =
        IdentificationUpdateOutcome::NotEvaluated;
    auto& value = record.committed_observation;
    value.source_frame_id = observation.source_frame_id;
    value.source_observation_id = observation.source_observation_id;
    value.persistent_target_id = observation.persistent_target_id;
    value.viewport_sequence = observation.viewport_sequence;
    value.viewport_source_frame_id = observation.viewport_source_frame_id;
    value.captured_at_ns = observation.captured_at_ns;
    value.result_at_ns = observation.result_at_ns;
    value.controller_consume_ns = observation.controller_consume_ns;
    value.stable_error_x = observation.stable_error_px.x;
    value.stable_error_y = observation.stable_error_px.y;
    value.stable_body_width = observation.stable_body_size_px.x;
    value.stable_body_height = observation.stable_body_size_px.y;
    value.raw_body_x = observation.raw_body_box_px.x;
    value.raw_body_y = observation.raw_body_box_px.y;
    value.raw_body_width = observation.raw_body_box_px.w;
    value.raw_body_height = observation.raw_body_box_px.h;
    value.motion_anchor_x = observation.motion_anchor_px.x;
    value.motion_anchor_y = observation.motion_anchor_px.y;
    value.motion_anchor_score = observation.motion_anchor_score;
    value.viewport_offset_x = observation.viewport_offset_px.x;
    value.viewport_offset_y = observation.viewport_offset_px.y;
    value.target_acceleration_x = observation.target_acceleration_px_per_sec2.x;
    value.target_acceleration_y = observation.target_acceleration_px_per_sec2.y;
    value.reliability = observation.reliability;
    value.normalized_size = observation.normalized_size;
    value.ads_epoch = observation.ads_epoch;
    value.eligible_candidate_count = observation.eligible_candidate_count;
    value.lifecycle = static_cast<std::uint8_t>(observation.lifecycle);
    value.motion = static_cast<std::uint8_t>(observation.motion);
    value.mode = static_cast<std::uint8_t>(observation.mode);
    value.fresh_observed = observation.fresh_observed;
    value.strong_observation = observation.strong_observation;
    value.stable_coordinates_valid = observation.stable_coordinates_valid;
    value.has_motion_anchor = observation.has_motion_anchor;
    value.reused_or_projected = observation.reused_or_projected;
    enqueue(record);
}

void TelemetryCollectors::observe_acquisition_trace(
    const TelemetryAcquisitionTraceInput& input) noexcept {
    if (!state_ || input.source_frame_id == 0) return;
    TelemetryRecord record;
    record.type = TelemetryRecordType::AdsAcquisitionTrace;
    record.frame_id = input.source_frame_id;
    record.tick_id = input.controller_tick_id;
    record.target_track_id = input.persistent_target_id;
    record.timestamps.vision_capture_ns = input.capture_acquire_begin_ns;
    record.timestamps.inference_ready_ns = input.result_ready_ns;
    record.timestamps.controller_consume_ns = input.controller_consume_ns;
    record.timestamps.output_sent_ns = input.vigem_submit_complete_ns;
    auto& value = record.ads_acquisition_trace;
    value.source_frame_id = input.source_frame_id;
    value.source_observation_id = input.source_observation_id;
    value.persistent_target_id = input.persistent_target_id;
    value.physical_ads_epoch = input.physical_ads_epoch;
    value.target_acquisition_id = input.target_acquisition_id;
    value.controller_tick_id = input.controller_tick_id;
    value.capture_acquire_begin_ns = input.capture_acquire_begin_ns;
    value.capture_acquire_complete_ns = input.capture_acquire_complete_ns;
    value.capture_copy_complete_ns = input.capture_copy_complete_ns;
    value.accumulated_frames = input.accumulated_frames;
    value.ads_acquisition_begin_ns = input.ads_acquisition_begin_ns;
    value.ads_acquisition_complete_ns = input.ads_acquisition_complete_ns;
    value.result_ready_ns = input.result_ready_ns;
    value.vision_publish_ns = input.vision_publish_ns;
    value.controller_submit_complete_ns = input.controller_submit_complete_ns;
    value.controller_consume_ns = input.controller_consume_ns;
    value.plan_decision_ns = input.plan_decision_ns;
    value.final_output_ready_ns = input.final_output_ready_ns;
    value.first_requested_ai_ns = input.first_requested_ai_ns;
    value.first_shaped_ai_ns = input.first_shaped_ai_ns;
    value.first_fused_output_ns = input.first_fused_output_ns;
    value.vigem_submit_complete_ns = input.vigem_submit_complete_ns;
    value.first_effect_observed_ns = input.first_effect_observed_ns;
    value.preferred_source_id = input.preferred_source_id;
    value.selected_source_id = input.selected_source_id;
    value.candidate_count = input.candidate_count;
    value.acquisition_state = input.acquisition_state;
    value.decision_reason = input.decision_reason;
    value.source_decision_available = input.source_decision_available;
    value.source_decision_outcome = input.source_decision_outcome;
    value.source_decision_reason = input.source_decision_reason;
    value.acquisition_terminal_reason = input.acquisition_terminal_reason;
    value.selector_target_generation = input.selector_target_generation;
    value.selector_target_changed = input.selector_target_changed;
    value.source_present_qpc = input.source_present_qpc;
    value.source_present_qpc_frequency = input.source_present_qpc_frequency;
    value.source_present_available = input.source_present_available;
    value.plan_admitted = input.plan_admitted;
    value.acquisition_active = input.acquisition_active;
    value.acquisition_exists = input.acquisition_exists;
    value.vision_publish_available = input.vision_publish_available;
    value.has_first_requested_ai = input.has_first_requested_ai;
    value.has_first_shaped_ai = input.has_first_shaped_ai;
    value.has_first_fused_output = input.has_first_fused_output;
    value.effective_activation_radius_px = input.effective_activation_radius_px;
    value.raw_error_x = input.raw_error_x;
    value.raw_error_y = input.raw_error_y;
    value.target_size_x = input.target_size_x;
    value.target_size_y = input.target_size_y;
    value.requested_ai_x = input.requested_ai_x;
    value.requested_ai_y = input.requested_ai_y;
    value.shaped_ai_x = input.shaped_ai_x;
    value.shaped_ai_y = input.shaped_ai_y;
    value.fused_output_x = input.fused_output_x;
    value.fused_output_y = input.fused_output_y;
    value.post_output_x = input.post_output_x;
    value.post_output_y = input.post_output_y;
    value.first_requested_ai_x = input.first_requested_ai_x;
    value.first_requested_ai_y = input.first_requested_ai_y;
    value.first_shaped_ai_x = input.first_shaped_ai_x;
    value.first_shaped_ai_y = input.first_shaped_ai_y;
    value.first_fused_output_x = input.first_fused_output_x;
    value.first_fused_output_y = input.first_fused_output_y;
    enqueue(record);
    ++counters_.acquisition_traces;
}

void TelemetryCollectors::observe_ego_motion_shadow(
    std::uint64_t source_frame_id,
    std::uint64_t controller_tick_id,
    const TelemetryEgoMotionShadowInput& input) noexcept {
    if (!state_ || !input.available) return;
    TelemetryRecord record;
    record.type = TelemetryRecordType::EgoMotionShadow;
    record.frame_id = source_frame_id != 0 ? source_frame_id : input.current_frame_id;
    record.tick_id = controller_tick_id;
    record.timestamps.inference_ready_ns = input.current_result_ns;
    record.timestamps.vision_capture_ns = input.previous_result_ns;
    auto& value = record.ego_motion_shadow;
    value.available = input.available;
    value.valid = input.valid;
    value.invalid_reason = input.invalid_reason;
    value.result_sequence = input.result_sequence;
    value.previous_frame_id = input.previous_frame_id;
    value.current_frame_id = input.current_frame_id;
    value.previous_present_qpc = input.previous_present_qpc;
    value.current_present_qpc = input.current_present_qpc;
    value.present_qpc_frequency = input.present_qpc_frequency;
    value.previous_result_ns = input.previous_result_ns;
    value.current_result_ns = input.current_result_ns;
    value.background_dx = input.background_dx;
    value.background_dy = input.background_dy;
    value.camera_dx = input.camera_dx;
    value.camera_dy = input.camera_dy;
    value.confidence = input.confidence;
    value.valid_background_ratio = input.valid_background_ratio;
    value.residual_px = input.residual_px;
    value.compute_ms = input.compute_ms;
    value.inlier_count = input.inlier_count;
    value.sample_count = input.sample_count;
    enqueue(record);
    ++counters_.ego_motion_records;
}

const control_learning::ControlHistory<1024>*
TelemetryCollectors::control_history() const noexcept {
    return state_ ? &state_->responses.history() : nullptr;
}

void TelemetryCollectors::observe_causal_shadow(
    const pipeline_contract::CommittedCaptureObservation& observation,
    const control_learning::SampleAssessment& assessment,
    const control_learning::CausalResponseEstimate& estimate,
    const control_learning::PendingMotionEstimate& pending,
    const control_learning::RolloutResult& rollout,
    const control_learning::Vec2d& final_output) noexcept {
    if (!state_) return;
    TelemetryRecord record;
    record.type = TelemetryRecordType::CausalResponseShadow;
    record.frame_id = observation.source_frame_id;
    record.target_track_id = observation.persistent_target_id;
    record.timestamps.vision_capture_ns = observation.captured_at_ns;
    record.timestamps.inference_ready_ns = observation.result_at_ns;
    auto& value = record.causal_shadow;
    value.best_delay_ms = estimate.best_delay_ms;
    value.selected_delay_ms = estimate.selected_delay_ms;
    value.selected_delay_confidence = estimate.selected_delay_confidence;
    value.right_confidence = estimate.right_confidence;
    value.left_confidence = estimate.left_confidence;
    value.joint_confidence = estimate.joint_confidence;
    value.excitation = estimate.excitation;
    value.residual = estimate.residual;
    value.pending_realized_x = static_cast<float>(pending.realized_px.x);
    value.pending_realized_y = static_cast<float>(pending.realized_px.y);
    value.pending_in_flight_x = static_cast<float>(pending.in_flight_px.x);
    value.pending_in_flight_y = static_cast<float>(pending.in_flight_px.y);
    value.pending_scheduled_x = static_cast<float>(pending.scheduled_px.x);
    value.pending_scheduled_y = static_cast<float>(pending.scheduled_px.y);
    value.pending_total_x = static_cast<float>(pending.pending_total_px.x);
    value.pending_total_y = static_cast<float>(pending.pending_total_px.y);
    value.pending_confidence = pending.confidence;
    value.reason_bits = assessment.reason_bits;
    value.accepted_delay_count = assessment.accepted_delay_count;
    value.accepted_by_any_delay = assessment.accepted_by_any_delay;
    value.delay_switch_pending = estimate.delay_switch_pending;
    value.pending_valid = pending.valid;
    value.rollout_valid = rollout.valid;
    value.rollout_best_scale = rollout.best_scale;
    value.rollout_confidence = rollout.confidence;
    value.rollout_candidate_count = static_cast<std::uint8_t>(rollout.candidate_count);
    value.rollout_uses_final_output = true;
    value.rollout_final_output_x = static_cast<float>(final_output.x);
    value.rollout_final_output_y = static_cast<float>(final_output.y);
    for (std::size_t i = 0; i < rollout.candidate_count && i < 5; ++i) {
        value.rollout_scales[i] = rollout.candidates[i].scale;
        value.rollout_costs[i] = static_cast<float>(rollout.candidates[i].cost);
    }
    switch (assessment.vision_quality) {
    case control_learning::VisionSampleQuality::Normal:
        record.vision_sample_quality = VisionSampleQuality::Normal; break;
    case control_learning::VisionSampleQuality::ReusedOrProjected:
        record.vision_sample_quality = VisionSampleQuality::SoftWeight; break;
    default:
        record.vision_sample_quality = VisionSampleQuality::HardReject; break;
    }
    switch (assessment.update_outcome) {
    case control_learning::IdentificationUpdateOutcome::Accepted:
        record.identification_update_outcome =
            IdentificationUpdateOutcome::AcceptedByAtLeastOneDelay; break;
    case control_learning::IdentificationUpdateOutcome::InsufficientExcitation:
        record.identification_update_outcome =
            IdentificationUpdateOutcome::InsufficientExcitation; break;
    case control_learning::IdentificationUpdateOutcome::HardRejected:
        record.identification_update_outcome =
            IdentificationUpdateOutcome::NoUsableDelay; break;
    default:
        record.identification_update_outcome =
            IdentificationUpdateOutcome::NotEvaluated; break;
    }
    enqueue(record);
}

void TelemetryCollectors::shutdown(std::uint64_t now_ns) noexcept {
    if (!state_) return;
    state_->ads.shutdown(now_ns);
    flush_ads_event();
}

TelemetryCollectorsCounters TelemetryCollectors::counters() const noexcept { return counters_; }

void TelemetryCollectors::enqueue(TelemetryRecord record) noexcept {
    if (!state_ || sink_ == nullptr) return;
    ++counters_.constructed_records;
    if (!sink_->enqueue(record) && record.critical) state_->ads.on_required_sample_dropped();
}

void TelemetryCollectors::flush_ads_event() noexcept {
    if (!state_) return;
    const auto event = state_->ads.take_completed();
    if (!event) return;
    TelemetryRecord record;
    record.type = TelemetryRecordType::AdsTransition;
    record.critical = true;
    record.event_id = event->ads_event_id;
    record.target_track_id = event->target_track_id;
    record.readiness = event->readiness;
    record.completeness = event->completeness;
    record.ads_transition.calibration_class = event->calibration_class;
    record.ads_transition.invalid_reason = event->invalid_reason;
    record.ads_transition.valid = event->valid;
    record.ads_transition.hipfire_frame_id = event->hipfire_frame_id;
    record.ads_transition.settled_frame_id = event->settled_frame_id;
    record.ads_transition.hipfire_dx = event->hipfire_dx;
    record.ads_transition.hipfire_dy = event->hipfire_dy;
    record.ads_transition.ads_dx = event->ads_dx;
    record.ads_transition.ads_dy = event->ads_dy;
    record.ads_transition.delta_dx = event->delta_dx;
    record.ads_transition.delta_dy = event->delta_dy;
    record.ads_transition.scale_x = event->scale_x;
    record.ads_transition.scale_y = event->scale_y;
    record.ads_transition.offset_x = event->offset_x;
    record.ads_transition.offset_y = event->offset_y;
    record.ads_transition.settle_confidence = event->settle_confidence;
    record.ads_transition.cumulative_manual = event->cumulative_manual;
    record.ads_transition.cumulative_ai = event->cumulative_ai;
    record.ads_transition.cumulative_recoil = event->cumulative_recoil;
    enqueue(record);
    ++counters_.state_transitions;
}

} // namespace runtime_app
