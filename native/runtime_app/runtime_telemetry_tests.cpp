#include "runtime_telemetry.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void require(bool value, int line) {
    if (!value) {
        std::cerr << "require failed at line " << line << '\n';
        std::abort();
    }
}
#define REQUIRE(value) require((value), __LINE__)

runtime_app::TelemetryRecord record(std::uint64_t tick, std::uint64_t frame) {
    runtime_app::TelemetryRecord value;
    value.kind = runtime_app::TelemetryRecordKind::ManualControllerTick;
    value.tick_id = tick;
    value.frame_id = frame;
    value.timestamp_ns = tick * 1'000'000;
    value.manual_x = 0.25f;
    value.final_x = 0.50f;
    return value;
}

void test_disabled_mode_has_zero_side_effects() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    REQUIRE(!telemetry.enqueue(record(1, 1)));
    const auto counters = telemetry.counters();
    REQUIRE(counters.writer_threads_started == 0);
    REQUIRE(counters.serialized_records == 0);
    REQUIRE(counters.accepted_records == 0);
    REQUIRE(telemetry.log_path().empty());
}

void test_bounded_queue_drops_exact_overflow_without_writer() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.queue_capacity = 2;
    options.start_writer = false;
    runtime_app::RuntimeTelemetry telemetry(options);
    REQUIRE(telemetry.enqueue(record(1, 1)));
    REQUIRE(telemetry.enqueue(record(2, 2)));
    REQUIRE(!telemetry.enqueue(record(3, 3)));
    const auto counters = telemetry.counters();
    REQUIRE(counters.accepted_records == 2);
    REQUIRE(counters.dropped_normal_records == 1);
    REQUIRE(counters.queue_high_watermark == 2);
    REQUIRE(counters.serialized_records == 0);
}

void test_writer_serializes_records_and_deduplicates_vision_frames() {
    const auto directory = std::filesystem::temp_directory_path() /
        "cod_native_runtime_telemetry_tests";
    std::filesystem::remove_all(directory);
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.directory = directory;
    options.queue_capacity = 32;
    runtime_app::RuntimeTelemetry telemetry(options);
    telemetry.start();
    auto vision = record(1, 42);
    vision.kind = runtime_app::TelemetryRecordKind::VisionFrame;
    REQUIRE(telemetry.enqueue(vision));
    REQUIRE(!telemetry.enqueue(vision));
    REQUIRE(telemetry.enqueue(record(2, 42)));
    telemetry.stop();
    const auto counters = telemetry.counters();
    REQUIRE(counters.writer_threads_started == 1);
    REQUIRE(counters.serialized_records == 2);
    REQUIRE(counters.duplicate_vision_frames == 1);
    REQUIRE(std::filesystem::exists(telemetry.log_path()));
    std::filesystem::remove_all(directory);
}

} // namespace

int main() {
    test_disabled_mode_has_zero_side_effects();
    test_bounded_queue_drops_exact_overflow_without_writer();
    test_writer_serializes_records_and_deduplicates_vision_frames();
    return 0;
}
