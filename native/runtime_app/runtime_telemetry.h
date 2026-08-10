#pragma once

#include "telemetry_schema.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace runtime_app {

struct RuntimeTelemetryOptions {
    bool enabled = false;
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
    std::uint64_t duplicate_source_frames = 0;
    std::uint64_t serialized_records = 0;
    std::uint64_t writer_threads_started = 0;
    std::uint64_t writer_failures = 0;
    std::uint64_t queue_high_watermark = 0;
};

class RuntimeTelemetry {
public:
    explicit RuntimeTelemetry(RuntimeTelemetryOptions options);
    ~RuntimeTelemetry();

    RuntimeTelemetry(const RuntimeTelemetry&) = delete;
    RuntimeTelemetry& operator=(const RuntimeTelemetry&) = delete;

    void start();
    void stop();
    bool enqueue(const TelemetryRecord& record) noexcept;
    RuntimeTelemetryCounters counters() const noexcept;
    std::size_t ordinary_queue_size() const noexcept;
    std::size_t ordinary_queue_capacity() const noexcept;
    std::size_t ordinary_queue_bytes() const noexcept;
    const std::filesystem::path& log_path() const noexcept;

private:
    void writer_loop();
    bool open_next_file();
    void serialize(const TelemetryRecord& record);
    void finish_serialized_line();

    RuntimeTelemetryOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<TelemetryRecord> queue_;
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t queue_count_ = 0;
    std::uint64_t last_vision_frame_id_ = 0;
    std::thread writer_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> dropped_normal_{0};
    std::atomic<std::uint64_t> dropped_critical_{0};
    std::atomic<std::uint64_t> duplicate_source_{0};
    std::atomic<std::uint64_t> serialized_{0};
    std::atomic<std::uint64_t> writers_started_{0};
    std::atomic<std::uint64_t> writer_failures_{0};
    std::atomic<std::uint64_t> high_watermark_{0};
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
