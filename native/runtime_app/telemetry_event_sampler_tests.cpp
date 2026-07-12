#include "telemetry_event_sampler.h"

#include <cstdlib>
#include <iostream>
#include <unordered_set>

namespace {
void require(bool value, int line) {
    if (!value) { std::cerr << "require failed at line " << line << '\n'; std::abort(); }
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::EventSample sample(std::uint64_t seq, std::uint64_t timestamp_ns, float x = 0.0f) {
    runtime_app::EventSample value;
    value.sample_seq = seq;
    value.timestamp_ns = timestamp_ns;
    value.controller.manual_x = x;
    return value;
}

void test_event_flushes_unique_250hz_pre_window() {
    runtime_app::TelemetryEventSampler sampler({250, 100, 100, 300});
    for (std::uint64_t seq = 1; seq <= 50; ++seq)
        sampler.observe(sample(seq, seq * 4'000'000));
    sampler.trigger(runtime_app::InputEventKind::AdsPressed, 200'000'000);
    const auto emitted = sampler.drain();
    std::unordered_set<std::uint64_t> sequences;
    for (const auto& value : emitted) sequences.insert(value.sample_seq);
    REQUIRE(sequences.size() == emitted.size());
    REQUIRE(!emitted.empty());
    REQUIRE(emitted.front().timestamp_ns <= 100'000'000);
    REQUIRE(emitted.back().timestamp_ns == 200'000'000);
}

void test_event_continues_at_250hz_for_post_window() {
    runtime_app::TelemetryEventSampler sampler({250, 100, 100, 300});
    for (std::uint64_t seq = 1; seq <= 25; ++seq)
        sampler.observe(sample(seq, seq * 4'000'000));
    sampler.trigger(runtime_app::InputEventKind::TargetSwitched, 100'000'000);
    sampler.drain();
    for (std::uint64_t seq = 26; seq <= 100; ++seq)
        sampler.observe(sample(seq, seq * 4'000'000));
    const auto emitted = sampler.drain();
    REQUIRE(emitted.size() == 75);
    REQUIRE(emitted.front().sample_seq == 26);
    REQUIRE(emitted.back().sample_seq == 100);
}

void test_input_episode_marks_start_peak_reversal_and_end() {
    runtime_app::UserInputEpisodeCollector collector({0.10f, 0.05f, 12'000'000});
    auto events = collector.observe(sample(1, 0, 0.0f));
    REQUIRE(events.empty());
    events = collector.observe(sample(2, 4'000'000, 0.20f));
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].kind == runtime_app::InputEventKind::InputStarted);
    REQUIRE(events[1].kind == runtime_app::InputEventKind::InputPeak);
    events = collector.observe(sample(3, 8'000'000, 0.50f));
    REQUIRE(events.size() == 1 && events[0].kind == runtime_app::InputEventKind::InputPeak);
    events = collector.observe(sample(4, 12'000'000, -0.20f));
    REQUIRE(events.size() == 1 && events[0].kind == runtime_app::InputEventKind::DirectionReversed);
    collector.observe(sample(5, 16'000'000, 0.0f));
    collector.observe(sample(6, 24'000'000, 0.0f));
    events = collector.observe(sample(7, 28'000'000, 0.0f));
    REQUIRE(events.size() == 1 && events[0].kind == runtime_app::InputEventKind::InputEnded);
}
}

int main() {
    test_event_flushes_unique_250hz_pre_window();
    test_event_continues_at_250hz_for_post_window();
    test_input_episode_marks_start_peak_reversal_and_end();
    return 0;
}
