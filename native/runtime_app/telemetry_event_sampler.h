#pragma once

#include "telemetry_schema.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace runtime_app {

struct EventSample {
    std::uint64_t sample_seq = 0;
    std::uint64_t timestamp_ns = 0;
    float manual_x = 0.0f;
    float manual_y = 0.0f;
};

struct InputEventMarker {
    InputEventKind kind = InputEventKind::None;
    std::uint64_t input_episode_id = 0;
    std::uint64_t sample_seq = 0;
    std::uint64_t timestamp_ns = 0;
    float magnitude = 0.0f;
};

struct TelemetryEventSamplerOptions {
    unsigned int ring_hz = 250;
    unsigned int normal_hz = 100;
    unsigned int pre_event_ms = 100;
    unsigned int post_event_ms = 300;
};

class TelemetryEventSampler {
public:
    explicit TelemetryEventSampler(TelemetryEventSamplerOptions options);
    void observe(const EventSample& sample);
    void trigger(InputEventKind kind, std::uint64_t timestamp_ns);
    std::vector<EventSample> drain();

private:
    static constexpr std::size_t kMaxRingSamples = 128;
    void append_pending_unique(const EventSample& sample);

    TelemetryEventSamplerOptions options_;
    std::array<EventSample, kMaxRingSamples> ring_{};
    std::size_t ring_head_ = 0;
    std::size_t ring_count_ = 0;
    std::uint64_t post_event_until_ns_ = 0;
    unsigned int normal_accumulator_ = 0;
    std::vector<EventSample> pending_;
};

struct UserInputEpisodeOptions {
    float active_threshold = 0.10f;
    float release_threshold = 0.05f;
    std::uint64_t settle_ns = 12'000'000;
};

class UserInputEpisodeCollector {
public:
    explicit UserInputEpisodeCollector(UserInputEpisodeOptions options);
    std::vector<InputEventMarker> observe(const EventSample& sample);

private:
    UserInputEpisodeOptions options_;
    std::uint64_t next_episode_id_ = 1;
    std::uint64_t active_episode_id_ = 0;
    std::uint64_t below_since_ns_ = 0;
    float peak_magnitude_ = 0.0f;
    float last_nonzero_x_ = 0.0f;
    bool active_ = false;
};

} // namespace runtime_app
