#include "runtime_telemetry.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <string>

namespace runtime_app {
namespace {

const char* kind_name(TelemetryRecordKind kind) {
    switch (kind) {
    case TelemetryRecordKind::VisionFrame: return "VisionFrame";
    case TelemetryRecordKind::RuntimeEvent: return "RuntimeEvent";
    case TelemetryRecordKind::ManualControllerTick:
    default: return "ManualControllerTick";
    }
}

} // namespace

RuntimeTelemetry::RuntimeTelemetry(RuntimeTelemetryOptions options)
    : options_(std::move(options)) {
    options_.queue_capacity = std::max<std::size_t>(1, options_.queue_capacity);
    options_.max_files = std::max<std::size_t>(1, options_.max_files);
    if (options_.enabled) queue_.resize(options_.queue_capacity);
}

RuntimeTelemetry::~RuntimeTelemetry() {
    stop();
}

void RuntimeTelemetry::start() {
    if (!options_.enabled || !options_.start_writer) return;
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    writer_ = std::thread(&RuntimeTelemetry::writer_loop, this);
    ++writers_started_;
}

void RuntimeTelemetry::stop() {
    if (!writer_.joinable()) return;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(options_.shutdown_timeout_ms);
    shutdown_deadline_ns_.store(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count()));
    running_.store(false);
    condition_.notify_all();
    writer_.join();
    if (output_.is_open()) output_.close();
}

bool RuntimeTelemetry::enqueue(const TelemetryRecord& record) noexcept {
    if (!options_.enabled || writer_failed_.load()) return false;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        record.critical ? ++dropped_critical_ : ++dropped_normal_;
        return false;
    }
    if (queue_count_ >= options_.queue_capacity) {
        record.critical ? ++dropped_critical_ : ++dropped_normal_;
        return false;
    }
    if (record.kind == TelemetryRecordKind::VisionFrame && record.frame_id != 0) {
        if (record.frame_id <= last_vision_frame_id_) {
            ++duplicate_vision_;
            return false;
        }
        last_vision_frame_id_ = record.frame_id;
    }
    queue_[queue_tail_] = record;
    queue_tail_ = (queue_tail_ + 1) % queue_.size();
    ++queue_count_;
    ++accepted_;
    const auto size = static_cast<std::uint64_t>(queue_count_);
    auto previous = high_watermark_.load();
    while (size > previous && !high_watermark_.compare_exchange_weak(previous, size)) {}
    lock.unlock();
    condition_.notify_one();
    return true;
}

RuntimeTelemetryCounters RuntimeTelemetry::counters() const noexcept {
    return RuntimeTelemetryCounters{
        accepted_.load(), dropped_normal_.load(), dropped_critical_.load(),
        duplicate_vision_.load(), serialized_.load(), writers_started_.load(),
        writer_failures_.load(), high_watermark_.load()};
}

const std::filesystem::path& RuntimeTelemetry::log_path() const noexcept {
    return log_path_;
}

bool RuntimeTelemetry::open_next_file() {
    try {
        std::filesystem::create_directories(options_.directory);
        if (output_.is_open()) output_.close();
        const std::size_t slot = file_index_++ % options_.max_files;
        log_path_ = options_.directory /
            ("native_runtime_telemetry_" + std::to_string(slot) + ".jsonl");
        output_.open(log_path_, std::ios::out | std::ios::trunc);
        current_size_ = 0;
        if (!output_.is_open()) {
            if (!writer_failed_.exchange(true)) ++writer_failures_;
            return false;
        }
        return true;
    } catch (...) {
        if (!writer_failed_.exchange(true)) ++writer_failures_;
        return false;
    }
}

void RuntimeTelemetry::serialize(const TelemetryRecord& record) {
    if (!output_.is_open() && !open_next_file()) return;
    output_ << '{'
        << "\"type\":\"" << kind_name(record.kind) << '\"'
        << ",\"event_id\":" << record.event_id
        << ",\"tick_id\":" << record.tick_id
        << ",\"frame_id\":" << record.frame_id
        << ",\"intent_id\":" << record.intent_id
        << ",\"timestamp_ns\":" << record.timestamp_ns
        << ",\"manual_x\":" << record.manual_x
        << ",\"manual_y\":" << record.manual_y
        << ",\"ai_x\":" << record.ai_x
        << ",\"ai_y\":" << record.ai_y
        << ",\"final_x\":" << record.final_x
        << ",\"final_y\":" << record.final_y
        << ",\"controller_pipeline_ms\":" << record.controller_pipeline_ms
        << ",\"vigem_update_ms\":" << record.vigem_update_ms
        << ",\"event_reason_flags\":" << record.event_reason_flags
        << "}\n";
    ++serialized_;
    current_size_ = static_cast<std::size_t>(output_.tellp());
    if (options_.rotate_size_bytes > 0 && current_size_ >= options_.rotate_size_bytes) {
        open_next_file();
    }
}

void RuntimeTelemetry::writer_loop() {
    while (true) {
        TelemetryRecord record;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return !running_.load() || queue_count_ > 0; });
            if (queue_count_ == 0) {
                if (!running_.load()) break;
                continue;
            }
            const auto deadline_ns = shutdown_deadline_ns_.load();
            const auto now_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (!running_.load() && deadline_ns != 0 && now_ns >= deadline_ns) {
                dropped_normal_.fetch_add(queue_count_);
                queue_count_ = 0;
                queue_head_ = queue_tail_;
                break;
            }
            record = queue_[queue_head_];
            queue_head_ = (queue_head_ + 1) % queue_.size();
            --queue_count_;
        }
        serialize(record);
    }
}

} // namespace runtime_app
