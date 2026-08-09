#include "runtime_telemetry.h"

#include <cstddef>
#include <iostream>

int main() {
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.start_writer = false;

    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryRecord record;
    record.type = runtime_app::TelemetryRecordType::SessionMetadata;

    if (!telemetry.enqueue(record)) {
        std::cerr << "production-shape telemetry enqueue failed\n";
        return 1;
    }
    if (sizeof(runtime_app::TelemetryRecord) >= 2576 ||
        telemetry.ordinary_queue_capacity() != options.queue_capacity ||
        telemetry.ordinary_queue_size() != options.queue_capacity ||
        telemetry.ordinary_queue_bytes() !=
            options.queue_capacity * sizeof(runtime_app::TelemetryRecord) ||
        telemetry.counters().accepted_records != 1) {
        std::cerr << "production-shape queue accounting failed\n";
        return 1;
    }

    std::cout << "production_shape_telemetry_record_bytes="
              << sizeof(runtime_app::TelemetryRecord)
              << " queue_capacity=" << telemetry.ordinary_queue_capacity()
              << " queue_bytes=" << telemetry.ordinary_queue_bytes()
              << " accepted=" << telemetry.counters().accepted_records << '\n';
    return 0;
}
