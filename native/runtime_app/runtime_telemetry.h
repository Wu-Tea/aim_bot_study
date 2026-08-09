#pragma once

#include "telemetry_schema.h"

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace runtime_app {

struct RuntimeTelemetryOptions {
    bool enabled = false;
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    // Gate transport is an independent feature switch.  Gate2.5 may run
    // alongside the ordinary collectors, so it must not be inferred from
    // gate_only.
    bool gate_enabled = false;
    // Gate-only keeps the ordinary queue deliberately small.  Gate25 has its
    // own bounded transport channel and must not make every ordinary slot
    // expensive merely because the diagnostic is enabled.
    bool gate_only = false;
#endif
    bool start_writer = true;
    std::filesystem::path directory = "runs/native_perf";
    std::size_t queue_capacity = 8192;
    std::size_t rotate_size_bytes = 256ull * 1024ull * 1024ull;
    std::size_t max_files = 10;
    unsigned int shutdown_timeout_ms = 1000;
};

struct RuntimeTelemetryCounters {
    std::uint64_t accepted_records = 0;
    std::uint64_t dropped_normal_records = 0;
    std::uint64_t dropped_critical_records = 0;
    std::uint64_t duplicate_vision_frames = 0;
    std::uint64_t serialized_records = 0;
    std::uint64_t writer_threads_started = 0;
    std::uint64_t writer_failures = 0;
    std::uint64_t queue_high_watermark = 0;
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    std::uint64_t gate25_accepted_records = 0;
    std::uint64_t gate25_dropped_records = 0;
    std::uint64_t gate25_serialized_records = 0;
    std::uint64_t gate25_queue_high_watermark = 0;
#endif
};

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
inline constexpr std::size_t kGate25TransportCellCapacity =
    kGate25CohortCellCount;
inline constexpr std::size_t kGate25AggregateQueueCapacity = 2;
inline constexpr std::size_t kGate25AnomalyQueueCapacity = 32;
// Kept as the public anomaly-burst capacity used by existing fixtures.  The
// aggregate and anomaly channels are physically separate.
inline constexpr std::size_t kGate25TransportQueueCapacity =
    kGate25AnomalyQueueCapacity;
inline constexpr std::size_t kGate25GateOnlyOrdinaryQueueCapacity = 16;

static_assert(kGate25TransportCellCapacity == kGate25CohortCellCount);
#endif

#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
struct Gate25AggregateTransport {
    Gate25AggregateScalars scalar{};
    std::uint16_t active_cell_count = 0;
    bool active_cell_overflow = false;
    std::array<std::uint16_t, kGate25TransportCellCapacity> cell_indices{};
    std::array<Gate25CohortCell, kGate25TransportCellCapacity> cells{};
};

static_assert(std::is_trivially_copyable_v<Gate25AggregateTransport>);
#endif

class RuntimeTelemetry {
public:
    explicit RuntimeTelemetry(RuntimeTelemetryOptions options);
    ~RuntimeTelemetry();

    RuntimeTelemetry(const RuntimeTelemetry&) = delete;
    RuntimeTelemetry& operator=(const RuntimeTelemetry&) = delete;

    void start();
    void stop();
    bool enqueue(const TelemetryRecord& record) noexcept;
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    bool enqueue_gate25_aggregate(
        const Gate25AggregateSnapshot& summary) noexcept;
    bool enqueue_gate25_anomaly(const Gate25Anomaly& anomaly) noexcept;
    std::size_t gate25_transport_queue_bytes() const noexcept;
#endif
    RuntimeTelemetryCounters counters() const noexcept;
    std::size_t ordinary_queue_size() const noexcept;
    std::size_t ordinary_queue_capacity() const noexcept;
    std::size_t ordinary_queue_bytes() const noexcept;
    const std::filesystem::path& log_path() const noexcept;

private:
    void writer_loop();
    bool open_next_file();
    void serialize(const TelemetryRecord& record);
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    void serialize_gate25_aggregate(const Gate25AggregateTransport& summary);
    void serialize_gate25_anomaly(const Gate25Anomaly& anomaly);
    static Gate25AggregateTransport compact_gate25_aggregate(
        const Gate25AggregateSnapshot& summary) noexcept;

    struct Gate25TransportState {
        std::array<Gate25AggregateTransport, kGate25AggregateQueueCapacity>
            aggregate_queue{};
        std::size_t aggregate_head = 0;
        std::size_t aggregate_tail = 0;
        std::size_t aggregate_count = 0;
        std::array<Gate25Anomaly, kGate25AnomalyQueueCapacity>
            anomaly_queue{};
        std::size_t anomaly_head = 0;
        std::size_t anomaly_tail = 0;
        std::size_t anomaly_count = 0;
    };
#endif
    void finish_serialized_line();

    RuntimeTelemetryOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<TelemetryRecord> queue_;
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t queue_count_ = 0;
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    std::unique_ptr<Gate25TransportState> gate25_transport_;
#endif
    std::uint64_t last_vision_frame_id_ = 0;
    std::thread writer_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> dropped_normal_{0};
    std::atomic<std::uint64_t> dropped_critical_{0};
    std::atomic<std::uint64_t> duplicate_vision_{0};
    std::atomic<std::uint64_t> serialized_{0};
    std::atomic<std::uint64_t> writers_started_{0};
    std::atomic<std::uint64_t> writer_failures_{0};
    std::atomic<std::uint64_t> high_watermark_{0};
#ifdef COD_NATIVE_RESEARCH_TELEMETRY_TEST_SEAMS
    std::atomic<std::uint64_t> gate25_accepted_{0};
    std::atomic<std::uint64_t> gate25_dropped_{0};
    std::atomic<std::uint64_t> gate25_serialized_{0};
    std::atomic<std::uint64_t> gate25_high_watermark_{0};
#endif
    std::atomic<bool> writer_failed_{false};
    std::atomic<std::uint64_t> shutdown_deadline_ns_{0};
    std::filesystem::path log_path_;
    std::ofstream output_;
    TelemetryRecord session_metadata_;
    bool has_session_metadata_ = false;
    std::size_t current_size_ = 0;
    std::size_t file_index_ = 0;
};

} // namespace runtime_app
