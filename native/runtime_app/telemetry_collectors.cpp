#include "telemetry_collectors.h"

#include "ads_transition_collector.h"
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
    std::unique_ptr<TelemetryTargetIdentity> identity;
    std::unique_ptr<TelemetryEventSampler> sampler;
    std::unique_ptr<UserInputEpisodeCollector> episodes;
    std::unique_ptr<AdsTransitionCollector> ads;
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
        state_->identity = std::make_unique<TelemetryTargetIdentity>();
        state_->sampler = std::make_unique<TelemetryEventSampler>(
            TelemetryEventSamplerOptions{250, 100, 100, 300});
        state_->episodes = std::make_unique<UserInputEpisodeCollector>(
            UserInputEpisodeOptions{0.10f, 0.05f, 12'000'000});
        state_->ads = std::make_unique<AdsTransitionCollector>();
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
        state.ads->on_ads_pressed(input.sample_ns);
        state.sampler->trigger(InputEventKind::AdsPressed, input.sample_ns);
        ++counters_.state_transitions;
    } else if (aim_stopped) {
        state.sampler->trigger(InputEventKind::AdsReleased, input.sample_ns);
        ++counters_.state_transitions;
    }
    state.last_aiming = input.aiming;
    state.has_last_aiming = true;
    const bool recoil_active =
        std::hypot(input.controller.recoil_x, input.controller.recoil_y) > 1.0e-5f;
    const bool reconnect_changed = state.has_reconnect_counts &&
        (input.controller.input_reconnect_count != state.last_input_reconnect_count ||
         input.controller.output_reconnect_count != state.last_output_reconnect_count);
    const bool delivered_interval_elapsed =
        state.last_delivered_record_ns == 0 ||
        input.sample_ns < state.last_delivered_record_ns ||
        input.sample_ns - state.last_delivered_record_ns >=
            state.delivered_record_interval_ns;
    const bool persist_delivered = delivered_interval_elapsed || aim_started ||
        aim_stopped || !input.controller.output_delivered || input.controller.output_error_code != 0 ||
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
        delivered.final_right_x = input.controller.final_x;
        delivered.final_right_y = input.controller.final_y;
        delivered.final_left_x = input.final_left_x;
        delivered.final_left_y = input.final_left_y;
        delivered.ads_epoch = state.ads_epoch;
        delivered.output_delivered = input.controller.output_delivered;
        delivered.output_disabled = input.output_disabled;
        delivered.firing = input.controller.final_fire_button;
        delivered.recoil_active = recoil_active;
        delivered.saturated = input.output_saturated;
        enqueue(delivered_record);
        state.last_delivered_record_ns = input.sample_ns;
        ++counters_.delivered_control_records;
    }
    state.last_input_reconnect_count = input.controller.input_reconnect_count;
    state.last_output_reconnect_count = input.controller.output_reconnect_count;
    state.has_reconnect_counts = true;
    const float dt = state.last_tick_ns != 0 && input.sample_ns > state.last_tick_ns
        ? static_cast<float>(input.sample_ns - state.last_tick_ns) / 1'000'000'000.0f : 0.0f;
    state.last_tick_ns = input.sample_ns;
    state.ads->observe_command(
        std::hypot(input.controller.manual_x, input.controller.manual_y) * dt,
        std::hypot(input.controller.ai_x, input.controller.ai_y) * dt,
        std::hypot(input.controller.recoil_x, input.controller.recoil_y) * dt);
    state.ads->on_tick(input.sample_ns);
    if (state.last_ring_sample_ns == 0 ||
        input.sample_ns - state.last_ring_sample_ns >= 4'000'000) {
        state.last_ring_sample_ns = input.sample_ns;
        EventSample sample;
        sample.sample_seq = state.next_sample_seq++;
        sample.timestamp_ns = input.sample_ns;
        sample.target_track_id = input.controller.selected_track_id;
        sample.timestamps.physical_read_ns = input.physical_read_ns;
        sample.timestamps.controller_consume_ns = input.controller_consume_ns;
        sample.timestamps.output_sent_ns = input.output_sent_ns;
        sample.timestamps.sample_ns = input.sample_ns;
        sample.controller = input.controller;

        copy_text(
            sample.controller.manual_authority_mode,
            input.manual_authority_mode);
        copy_text(
            sample.controller.assist_control_phase,
            input.assist_control_phase);
        copy_text(
            sample.controller.operation_class,
            input.operation_class);

        copy_text(
            sample.controller.bodylock_constraint_reason,
            input.bodylock_constraint_reason);

        copy_text(sample.controller.aim_region_source, input.aim_region_source);
        copy_text(
            sample.controller.desired_point_source,
            input.desired_point_source);

        sample.controller.has_target = state.has_target;

        copy_text(
            sample.controller.auto_fire_block_reason,
            input.auto_fire_block_reason);

        copy_text(
            sample.controller.enemy_mark_block_reason,
            input.enemy_mark_block_reason);
        copy_text(sample.controller.assist_authority, input.assist_authority);
        copy_text(sample.controller.assist_authority_reason, input.assist_authority_reason);
        copy_text(sample.controller.bodylock_lifecycle, input.bodylock_lifecycle);
        copy_text(sample.controller.assist_limit_reason, input.assist_limit_reason);
        sample.controller.detector_box_count = state.detector_box_count;
        sample.controller.production_target_confidence = state.target_confidence;
        sample.controller.target_dx = state.target_dx;
        sample.controller.target_dy = state.target_dy;
        sample.controller.target_error_px = std::hypot(state.target_dx, state.target_dy);
        sample.controller.target_identity_quality = state.target_identity_quality;
        copy_text(sample.controller.aim_mode, input.aim_mode);
        copy_text(sample.controller.production_target_source, state.target_source.data());
        copy_text(sample.controller.production_target_tier, state.target_tier.data());
        state.sampler->observe(sample);
        for (const auto& event : state.episodes->observe(sample)) {
            TelemetryRecord record;
            record.type = TelemetryRecordType::InputEvent;
            record.event_id = event.input_episode_id;
            record.sample_seq = event.sample_seq;
            record.timestamps.sample_ns = event.timestamp_ns;
            record.input_event = {event.kind, event.input_episode_id, event.magnitude};
            record.readiness = TelemetryReadiness::ProfileEligible;
            enqueue(record);
            ++counters_.input_event_records;
        }
    }
    for (const auto& sample : state.sampler->drain()) {
        TelemetryRecord record;
        record.type = TelemetryRecordType::ControllerSample;
        record.tick_id = input.tick_id;
        record.sample_seq = sample.sample_seq;
        record.target_track_id = sample.target_track_id;
        record.timestamps = sample.timestamps;
        record.controller = sample.controller;
        record.readiness = TelemetryReadiness::ProfileEligible;
        enqueue(record);
        ++counters_.controller_sample_records;
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
    observation.explicit_switch = input.explicit_switch;
    observation.association_ambiguous = input.association_ambiguous;
    observation.x1 = input.x1; observation.y1 = input.y1;
    observation.x2 = input.x2; observation.y2 = input.y2;
    observation.target_x = input.target_x; observation.target_y = input.target_y;
    const TargetIdentityResult identity = state.identity->observe(observation);
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
        ++counters_.target_event_records;
        ++counters_.state_transitions;
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
    if (!input.aiming) state.ads->observe_hipfire(ads_frame);
    else state.ads->observe_vision(ads_frame);
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
    enqueue(record);
    ++counters_.committed_capture_records;
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
    record.ads_acquisition_trace = input;
    enqueue(record);
    ++counters_.acquisition_traces;
}

void TelemetryCollectors::shutdown(std::uint64_t now_ns) noexcept {
    if (!state_) return;
    state_->ads->shutdown(now_ns);
    flush_ads_event();
}

TelemetryCollectorsCounters TelemetryCollectors::counters() const noexcept { return counters_; }

void TelemetryCollectors::enqueue(TelemetryRecord record) noexcept {
    if (!state_ || sink_ == nullptr) return;
    ++counters_.constructed_records;
    if (!sink_->enqueue(record) && record.critical && state_->ads) {
        state_->ads->on_required_sample_dropped();
    }
}

void TelemetryCollectors::flush_ads_event() noexcept {
    if (!state_) return;
    if (!state_->ads) return;
    const auto event = state_->ads->take_completed();
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
    ++counters_.ads_transition_records;
    ++counters_.state_transitions;
}

} // namespace runtime_app
