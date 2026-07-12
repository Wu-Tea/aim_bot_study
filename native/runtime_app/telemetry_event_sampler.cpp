#include "telemetry_event_sampler.h"

#include <algorithm>
#include <cmath>

namespace runtime_app {

TelemetryEventSampler::TelemetryEventSampler(TelemetryEventSamplerOptions options)
    : options_(options) {
    options_.ring_hz = std::max(1u, options_.ring_hz);
    options_.normal_hz = std::max(1u, std::min(options_.normal_hz, options_.ring_hz));
    pending_.reserve(kMaxRingSamples * 4);
}

void TelemetryEventSampler::observe(const EventSample& sample) {
    ring_[ring_head_] = sample;
    ring_head_ = (ring_head_ + 1) % kMaxRingSamples;
    ring_count_ = std::min(kMaxRingSamples, ring_count_ + 1);

    if (post_event_until_ns_ != 0 && sample.timestamp_ns <= post_event_until_ns_) {
        append_pending_unique(sample);
        return;
    }
    if (post_event_until_ns_ != 0 && sample.timestamp_ns > post_event_until_ns_)
        post_event_until_ns_ = 0;

    normal_accumulator_ += options_.normal_hz;
    if (normal_accumulator_ >= options_.ring_hz) {
        normal_accumulator_ -= options_.ring_hz;
        append_pending_unique(sample);
    }
}

void TelemetryEventSampler::trigger(InputEventKind, std::uint64_t timestamp_ns) {
    const std::uint64_t pre_ns = static_cast<std::uint64_t>(options_.pre_event_ms) * 1'000'000;
    const std::uint64_t earliest = timestamp_ns > pre_ns ? timestamp_ns - pre_ns : 0;
    const std::size_t first = (ring_head_ + kMaxRingSamples - ring_count_) % kMaxRingSamples;
    for (std::size_t i = 0; i < ring_count_; ++i) {
        const EventSample& value = ring_[(first + i) % kMaxRingSamples];
        if (value.timestamp_ns >= earliest && value.timestamp_ns <= timestamp_ns)
            append_pending_unique(value);
    }
    post_event_until_ns_ = timestamp_ns +
        static_cast<std::uint64_t>(options_.post_event_ms) * 1'000'000;
    std::sort(pending_.begin(), pending_.end(), [](const EventSample& a, const EventSample& b) {
        return a.sample_seq < b.sample_seq;
    });
}

std::vector<EventSample> TelemetryEventSampler::drain() {
    std::vector<EventSample> result;
    result.swap(pending_);
    pending_.reserve(kMaxRingSamples * 4);
    return result;
}

void TelemetryEventSampler::append_pending_unique(const EventSample& sample) {
    const auto found = std::find_if(pending_.begin(), pending_.end(), [&](const EventSample& value) {
        return value.sample_seq == sample.sample_seq;
    });
    if (found == pending_.end()) pending_.push_back(sample);
}

UserInputEpisodeCollector::UserInputEpisodeCollector(UserInputEpisodeOptions options)
    : options_(options) {}

std::vector<InputEventMarker> UserInputEpisodeCollector::observe(const EventSample& sample) {
    std::vector<InputEventMarker> events;
    events.reserve(2);
    const float magnitude = std::sqrt(
        sample.controller.manual_x * sample.controller.manual_x +
        sample.controller.manual_y * sample.controller.manual_y);
    auto emit = [&](InputEventKind kind) {
        events.push_back({kind, active_episode_id_, sample.sample_seq,
            sample.timestamp_ns, magnitude});
    };

    if (!active_) {
        if (magnitude < options_.active_threshold) return events;
        active_ = true;
        active_episode_id_ = next_episode_id_++;
        peak_magnitude_ = magnitude;
        last_nonzero_x_ = sample.controller.manual_x;
        below_since_ns_ = 0;
        emit(InputEventKind::InputStarted);
        emit(InputEventKind::InputPeak);
        return events;
    }

    if (magnitude > peak_magnitude_) {
        peak_magnitude_ = magnitude;
        emit(InputEventKind::InputPeak);
    }
    if (std::fabs(sample.controller.manual_x) >= options_.active_threshold &&
        std::fabs(last_nonzero_x_) >= options_.active_threshold &&
        sample.controller.manual_x * last_nonzero_x_ < 0.0f) {
        emit(InputEventKind::DirectionReversed);
    }
    if (std::fabs(sample.controller.manual_x) >= options_.active_threshold)
        last_nonzero_x_ = sample.controller.manual_x;

    if (magnitude <= options_.release_threshold) {
        if (below_since_ns_ == 0) below_since_ns_ = sample.timestamp_ns;
        if (sample.timestamp_ns - below_since_ns_ >= options_.settle_ns) {
            emit(InputEventKind::InputEnded);
            active_ = false;
            active_episode_id_ = 0;
            below_since_ns_ = 0;
        }
    } else {
        below_since_ns_ = 0;
    }
    return events;
}

} // namespace runtime_app
