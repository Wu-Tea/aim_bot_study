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
    running_.store(false);
    condition_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (output_.is_open()) output_.close();
}

bool RuntimeTelemetry::enqueue(const TelemetryRecord& record) noexcept {
    if (!options_.enabled) return false;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        record.critical ? ++dropped_critical_ : ++dropped_normal_;
        return false;
    }
    if (queue_.size() >= options_.queue_capacity) {
        record.critical ? ++dropped_critical_ : ++dropped_normal_;
        return false;
    }
    if (record.kind == TelemetryRecordKind::VisionFrame && record.frame_id != 0) {
        if (!accepted_vision_frames_.insert(record.frame_id).second) {
            ++duplicate_vision_;
            return false;
        }
    }
    queue_.push_back(record);
    ++accepted_;
    const auto size = static_cast<std::uint64_t>(queue_.size());
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
        high_watermark_.load()};
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
        return output_.is_open();
    } catch (...) {
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
        << "}\n";
    output_.flush();
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
            condition_.wait(lock, [this] { return !running_.load() || !queue_.empty(); });
            if (queue_.empty()) {
                if (!running_.load()) break;
                continue;
            }
            record = queue_.front();
            queue_.pop_front();
        }
        serialize(record);
    }
}

} // namespace runtime_app
