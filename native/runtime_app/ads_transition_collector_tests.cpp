#include "ads_transition_collector.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, int line) {
    if (!value) { std::cerr << "require failed at line " << line << '\n'; std::abort(); }
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::AdsVisualFrame frame(std::uint64_t id, std::uint64_t track, float scale) {
    runtime_app::AdsVisualFrame value;
    value.frame_id = id;
    value.sample_seq = id;
    value.captured_at_ns = id * 10'000'000;
    value.target_track_id = track;
    value.identity_quality = runtime_app::TargetIdentityQuality::StrongGeometricMatch;
    value.live = true;
    value.x1 = 320 - 50 * scale;
    value.x2 = 320 + 50 * scale;
    value.y1 = 256 - 100 * scale;
    value.y2 = 256 + 100 * scale;
    value.screen_center_x = 320;
    value.screen_center_y = 256;
    value.target_x = 350;
    value.target_y = 230;
    return value;
}

void feed_clean_zoom(runtime_app::AdsTransitionCollector& collector, std::uint64_t track) {
    std::uint64_t id = 2;
    for (float scale : {1.10f, 1.25f, 1.39f, 1.40f, 1.40f, 1.40f})
        collector.observe_vision(frame(id++, track, scale));
}

void test_one_hundred_clean_transitions_complete_once() {
    for (int attempt = 0; attempt < 100; ++attempt) {
        runtime_app::AdsTransitionCollector collector;
        collector.observe_hipfire(frame(1, 7, 1.0f));
        collector.on_ads_pressed(10'000'000);
        feed_clean_zoom(collector, 7);
        const auto event = collector.take_completed();
        REQUIRE(event.has_value());
        REQUIRE(event->valid);
        REQUIRE(event->calibration_class == runtime_app::AdsCalibrationClass::CalibrationClean);
        REQUIRE(event->readiness == runtime_app::TelemetryReadiness::ModelEligible);
        REQUIRE(event->hipfire_frame_id != event->settled_frame_id);
        REQUIRE(event->completeness.complete);
        REQUIRE(!collector.take_completed().has_value());
    }
}

void test_no_hipfire_target_is_invalid() {
    runtime_app::AdsTransitionCollector collector;
    collector.on_ads_pressed(10'000'000);
    const auto event = collector.take_completed();
    REQUIRE(event.has_value() && !event->valid);
    REQUIRE(event->invalid_reason == runtime_app::AdsInvalidReason::NoHipfireTarget);
}

void test_target_switch_is_invalid() {
    runtime_app::AdsTransitionCollector collector;
    collector.observe_hipfire(frame(1, 7, 1.0f));
    collector.on_ads_pressed(10'000'000);
    collector.observe_vision(frame(2, 8, 1.1f));
    const auto event = collector.take_completed();
    REQUIRE(event.has_value() && !event->valid);
    REQUIRE(event->invalid_reason == runtime_app::AdsInvalidReason::TargetSwitched);
}

void test_timeout_and_queue_overflow_are_explicit() {
    runtime_app::AdsTransitionCollector timeout;
    timeout.observe_hipfire(frame(1, 7, 1.0f));
    timeout.on_ads_pressed(10'000'000);
    timeout.on_tick(1'000'000'000);
    REQUIRE(timeout.take_completed()->invalid_reason == runtime_app::AdsInvalidReason::AdsNotSettled);

    runtime_app::AdsTransitionCollector overflow;
    overflow.observe_hipfire(frame(1, 7, 1.0f));
    overflow.on_ads_pressed(10'000'000);
    overflow.on_required_sample_dropped();
    REQUIRE(overflow.take_completed()->invalid_reason == runtime_app::AdsInvalidReason::QueueOverflow);
}

void test_motion_or_commands_use_conditional_model_class() {
    runtime_app::AdsTransitionCollector collector;
    collector.observe_hipfire(frame(1, 7, 1.0f));
    collector.on_ads_pressed(10'000'000);
    collector.observe_command(0.08f, 0.02f, 0.0f);
    feed_clean_zoom(collector, 7);
    const auto event = collector.take_completed();
    REQUIRE(event.has_value() && event->valid);
    REQUIRE(event->calibration_class == runtime_app::AdsCalibrationClass::ConditionalModel);
}
}

int main() {
    test_one_hundred_clean_transitions_complete_once();
    test_no_hipfire_target_is_invalid();
    test_target_switch_is_invalid();
    test_timeout_and_queue_overflow_are_explicit();
    test_motion_or_commands_use_conditional_model_class();
    return 0;
}
