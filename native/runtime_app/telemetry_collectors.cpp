#include "telemetry_collectors.h"

#include "ads_transition_collector.h"
#include "control_response_window.h"
#include "telemetry_event_sampler.h"
#include "telemetry_target_identity.h"

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
    command.manual_x = input.manual_x; command.manual_y = input.manual_y;
    command.ai_x = input.ai_x; command.ai_y = input.ai_y;
    command.pre_recoil_x = input.pre_recoil_x; command.pre_recoil_y = input.pre_recoil_y;
    command.recoil_x = input.recoil_x; command.recoil_y = input.recoil_y;
    command.final_x = input.final_x; command.final_y = input.final_y;
    state.responses.observe_controller(command);

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
        sample.controller.ai_x = input.ai_x;
        sample.controller.ai_y = input.ai_y;
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
