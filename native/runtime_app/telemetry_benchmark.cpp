#include "runtime_telemetry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace {
double percentile(std::vector<double> values, double q) {
    std::sort(values.begin(), values.end());
    const std::size_t rank = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(q * values.size())));
    return values.empty() ? 0.0 : values[rank - 1];
}
}

int main(int argc, char** argv) {
    std::size_t records = 1'000'000;
    std::string output = "telemetry_benchmark.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--records" && i + 1 < argc) records = std::stoull(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) output = argv[++i];
    }
    runtime_app::RuntimeTelemetryOptions options;
    options.enabled = true;
    options.start_writer = false;
    options.queue_capacity = records + 1;
    runtime_app::RuntimeTelemetry telemetry(options);
    runtime_app::TelemetryRecord record;
    std::vector<double> enqueue_ms;
    enqueue_ms.reserve(records);
    for (std::size_t i = 0; i < records; ++i) {
        record.tick_id = i + 1;
        const auto start = std::chrono::steady_clock::now();
        telemetry.enqueue(record);
        enqueue_ms.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    const auto counters = telemetry.counters();
    std::ofstream stream(output, std::ios::trunc);
    stream << "{\n"
        << "  \"records\": " << records << ",\n"
        << "  \"enqueue_ms_p50\": " << percentile(enqueue_ms, .50) << ",\n"
        << "  \"enqueue_ms_p95\": " << percentile(enqueue_ms, .95) << ",\n"
        << "  \"enqueue_ms_p99\": " << percentile(enqueue_ms, .99) << ",\n"
        << "  \"enqueue_ms_max\": " << *std::max_element(enqueue_ms.begin(), enqueue_ms.end()) << ",\n"
        << "  \"accepted\": " << counters.accepted_records << ",\n"
        << "  \"dropped\": " << counters.dropped_normal_records << "\n}\n";
    return 0;
}
