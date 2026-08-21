#include "runtime_telemetry.h"
#include "test_support/native_test_registry.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>

void test_runtime_telemetry_production_shape() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.start_writer = false;

    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryRecord record;
    record.type = runtime_app::TelemetryRecordType::SessionMetadata;

    if (!telemetry.enqueue(record)) {
        throw std::runtime_error("production-shape telemetry enqueue failed");
    }
    if (sizeof(runtime_app::TelemetryRecord) >= 2576 ||
        telemetry.ordinary_queue_capacity() != options.queue_capacity ||
        telemetry.ordinary_queue_size() != options.queue_capacity ||
        telemetry.ordinary_queue_bytes() !=
            options.queue_capacity * sizeof(runtime_app::TelemetryRecord) ||
        telemetry.counters().accepted_records != 1) {
        throw std::runtime_error("production-shape queue accounting failed");
    }

    std::cout << "production_shape_telemetry_record_bytes="
              << sizeof(runtime_app::TelemetryRecord)
              << " queue_capacity=" << telemetry.ordinary_queue_capacity()
              << " queue_bytes=" << telemetry.ordinary_queue_bytes()
              << " accepted=" << telemetry.counters().accepted_records << '\n';
}

void register_runtime_telemetry_production_shape_tests(native_test::Registry& registry) {
    registry.add_case("FeatureTelemetryAndDiagnostics", "runtime_telemetry_production_shape", test_runtime_telemetry_production_shape);
}
