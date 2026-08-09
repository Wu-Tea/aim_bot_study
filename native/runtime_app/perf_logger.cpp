#include "perf_logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace runtime_app {

namespace {

constexpr double kHistogramBucketWidthMs = 0.25;
constexpr std::size_t kHistogramRegularBuckets = 512;
constexpr std::size_t kHistogramBucketCount = kHistogramRegularBuckets + 1;
constexpr std::size_t kSummaryQueueCapacity = 8;

struct LatencySummary {
    std::uint64_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

class FixedLatencyHistogram {
public:
    void observe(double value_ms) noexcept {
        if (!std::isfinite(value_ms) || value_ms < 0.0) return;
        const std::size_t index = value_ms >=
                kHistogramBucketWidthMs * static_cast<double>(kHistogramRegularBuckets)
            ? kHistogramRegularBuckets
            : static_cast<std::size_t>(value_ms / kHistogramBucketWidthMs);
        ++bins_[index];
        ++count_;
        sum_ += value_ms;
        max_ = std::max(max_, value_ms);
    }

    LatencySummary summary() const noexcept {
        LatencySummary value;
        value.count = count_;
        if (count_ == 0) return value;
        value.mean = sum_ / static_cast<double>(count_);
        value.p50 = quantile(0.50);
        value.p95 = quantile(0.95);
        value.p99 = quantile(0.99);
        value.max = max_;
        return value;
    }

    void reset() noexcept {
        bins_.fill(0);
        count_ = 0;
        sum_ = 0.0;
        max_ = 0.0;
    }

private:
    double quantile(double probability) const noexcept {
        if (count_ == 0) return 0.0;
        const std::uint64_t rank = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::ceil(
                   probability * static_cast<double>(count_))));
        std::uint64_t cumulative = 0;
        for (std::size_t index = 0; index < bins_.size(); ++index) {
            cumulative += bins_[index];
            if (cumulative >= rank) {
                if (index >= kHistogramRegularBuckets) {
                    return kHistogramBucketWidthMs *
                        static_cast<double>(kHistogramRegularBuckets);
                }
                return (static_cast<double>(index) + 0.5) *
                    kHistogramBucketWidthMs;
            }
        }
        return max_;
    }

    std::array<std::uint32_t, kHistogramBucketCount> bins_{};
    std::uint64_t count_ = 0;
    double sum_ = 0.0;
    double max_ = 0.0;
};

struct PerfSummaryRecord {
    std::uint64_t window_start_ns = 0;
    std::uint64_t window_end_ns = 0;
    double window_seconds = 0.0;
    double aiming_seconds = 0.0;
    double idle_seconds = 0.0;
    std::uint64_t controller_ticks = 0;
    std::uint64_t output_delivered = 0;
    std::uint64_t vision_frames = 0;
    std::uint64_t active_vision_frames = 0;
    std::uint64_t idle_vision_frames = 0;
    std::uint64_t accumulated_frames = 0;
    std::uint64_t accumulated_gt_one = 0;
    std::uint64_t active_accumulated_frames = 0;
    std::uint64_t active_accumulated_gt_one = 0;
    std::uint64_t idle_accumulated_frames = 0;
    std::uint64_t idle_accumulated_gt_one = 0;
    std::uint64_t writer_queue_dropped_total = 0;
    LatencySummary controller_tick;
    LatencySummary controller_pipeline;
    LatencySummary vigem_update;
    LatencySummary capture_to_result;
    LatencySummary copy_to_result;
    LatencySummary source_present_to_result;
    LatencySummary result_to_controller;
    LatencySummary result_to_vigem;
    LatencySummary vision_publish_to_vigem;
    LatencySummary controller_consume_to_vigem;
    LatencySummary controller_submit_to_final_output;
    LatencySummary final_output_to_vigem;
    LatencySummary source_present_to_vigem;
    LatencySummary cuda_map;
    LatencySummary preprocess;
    LatencySummary infer;
    LatencySummary gpu_total;
    LatencySummary output_copy_sync;
    LatencySummary output_copy;
    LatencySummary output_wait;
    LatencySummary sync_queue_residual;
    LatencySummary color_copy;
    LatencySummary cuda_unmap;
    LatencySummary ego_stage;
    LatencySummary ego_compute;
};

struct PerfSummaryWindow {
    std::uint64_t start_ns = 0;
    std::uint64_t last_controller_ns = 0;
    bool last_aiming = false;
    std::uint64_t aiming_ns = 0;
    std::uint64_t idle_ns = 0;
    std::uint64_t controller_ticks = 0;
    std::uint64_t output_delivered = 0;
    std::uint64_t vision_frames = 0;
    std::uint64_t active_vision_frames = 0;
    std::uint64_t idle_vision_frames = 0;
    std::uint64_t accumulated_frames = 0;
    std::uint64_t accumulated_gt_one = 0;
    std::uint64_t active_accumulated_frames = 0;
    std::uint64_t active_accumulated_gt_one = 0;
    std::uint64_t idle_accumulated_frames = 0;
    std::uint64_t idle_accumulated_gt_one = 0;
    FixedLatencyHistogram controller_tick;
    FixedLatencyHistogram controller_pipeline;
    FixedLatencyHistogram vigem_update;
    FixedLatencyHistogram capture_to_result;
    FixedLatencyHistogram copy_to_result;
    FixedLatencyHistogram source_present_to_result;
    FixedLatencyHistogram result_to_controller;
    FixedLatencyHistogram result_to_vigem;
    FixedLatencyHistogram vision_publish_to_vigem;
    FixedLatencyHistogram controller_consume_to_vigem;
    FixedLatencyHistogram controller_submit_to_final_output;
    FixedLatencyHistogram final_output_to_vigem;
    FixedLatencyHistogram source_present_to_vigem;
    FixedLatencyHistogram cuda_map;
    FixedLatencyHistogram preprocess;
    FixedLatencyHistogram infer;
    FixedLatencyHistogram gpu_total;
    FixedLatencyHistogram output_copy_sync;
    FixedLatencyHistogram output_copy;
    FixedLatencyHistogram output_wait;
    FixedLatencyHistogram sync_queue_residual;
    FixedLatencyHistogram color_copy;
    FixedLatencyHistogram cuda_unmap;
    FixedLatencyHistogram ego_stage;
    FixedLatencyHistogram ego_compute;

    void reset(std::uint64_t now_ns, bool aiming) noexcept {
        start_ns = now_ns;
        last_controller_ns = now_ns;
        last_aiming = aiming;
        aiming_ns = 0;
        idle_ns = 0;
        controller_ticks = 0;
        output_delivered = 0;
        vision_frames = 0;
        active_vision_frames = 0;
        idle_vision_frames = 0;
        accumulated_frames = 0;
        accumulated_gt_one = 0;
        active_accumulated_frames = 0;
        active_accumulated_gt_one = 0;
        idle_accumulated_frames = 0;
        idle_accumulated_gt_one = 0;
        controller_tick.reset();
        controller_pipeline.reset();
        vigem_update.reset();
        capture_to_result.reset();
        copy_to_result.reset();
        source_present_to_result.reset();
        result_to_controller.reset();
        result_to_vigem.reset();
        vision_publish_to_vigem.reset();
        controller_consume_to_vigem.reset();
        controller_submit_to_final_output.reset();
        final_output_to_vigem.reset();
        source_present_to_vigem.reset();
        cuda_map.reset();
        preprocess.reset();
        infer.reset();
        gpu_total.reset();
        output_copy_sync.reset();
        output_copy.reset();
        output_wait.reset();
        sync_queue_residual.reset();
        color_copy.reset();
        cuda_unmap.reset();
        ego_stage.reset();
        ego_compute.reset();
    }

    void advance_clock(std::uint64_t now_ns) noexcept {
        if (last_controller_ns == 0 || now_ns <= last_controller_ns) return;
        const std::uint64_t elapsed = now_ns - last_controller_ns;
        if (last_aiming) aiming_ns += elapsed;
        else idle_ns += elapsed;
        last_controller_ns = now_ns;
    }

    PerfSummaryRecord snapshot(std::uint64_t end_ns) const noexcept {
        PerfSummaryRecord record;
        record.window_start_ns = start_ns;
        record.window_end_ns = end_ns;
        record.window_seconds = end_ns > start_ns
            ? static_cast<double>(end_ns - start_ns) / 1'000'000'000.0 : 0.0;
        record.aiming_seconds = static_cast<double>(aiming_ns) / 1'000'000'000.0;
        record.idle_seconds = static_cast<double>(idle_ns) / 1'000'000'000.0;
        record.controller_ticks = controller_ticks;
        record.output_delivered = output_delivered;
        record.vision_frames = vision_frames;
        record.active_vision_frames = active_vision_frames;
        record.idle_vision_frames = idle_vision_frames;
        record.accumulated_frames = accumulated_frames;
        record.accumulated_gt_one = accumulated_gt_one;
        record.active_accumulated_frames = active_accumulated_frames;
        record.active_accumulated_gt_one = active_accumulated_gt_one;
        record.idle_accumulated_frames = idle_accumulated_frames;
        record.idle_accumulated_gt_one = idle_accumulated_gt_one;
        record.controller_tick = controller_tick.summary();
        record.controller_pipeline = controller_pipeline.summary();
        record.vigem_update = vigem_update.summary();
        record.capture_to_result = capture_to_result.summary();
        record.copy_to_result = copy_to_result.summary();
        record.source_present_to_result = source_present_to_result.summary();
        record.result_to_controller = result_to_controller.summary();
        record.result_to_vigem = result_to_vigem.summary();
        record.vision_publish_to_vigem = vision_publish_to_vigem.summary();
        record.controller_consume_to_vigem = controller_consume_to_vigem.summary();
        record.controller_submit_to_final_output =
            controller_submit_to_final_output.summary();
        record.final_output_to_vigem = final_output_to_vigem.summary();
        record.source_present_to_vigem = source_present_to_vigem.summary();
        record.cuda_map = cuda_map.summary();
        record.preprocess = preprocess.summary();
        record.infer = infer.summary();
        record.gpu_total = gpu_total.summary();
        record.output_copy_sync = output_copy_sync.summary();
        record.output_copy = output_copy.summary();
        record.output_wait = output_wait.summary();
        record.sync_queue_residual = sync_queue_residual.summary();
        record.color_copy = color_copy.summary();
        record.cuda_unmap = cuda_unmap.summary();
        record.ego_stage = ego_stage.summary();
        record.ego_compute = ego_compute.summary();
        return record;
    }
};

double safe_rate(std::uint64_t count, double seconds) noexcept {
    return seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0;
}

double safe_percent(std::uint64_t count, std::uint64_t total) noexcept {
    return total > 0
        ? 100.0 * static_cast<double>(count) / static_cast<double>(total) : 0.0;
}

void write_latency_json(
    std::ostream& output,
    const char* name,
    const LatencySummary& value,
    bool leading_comma = true) {
    if (leading_comma) output << ',';
    output << '"' << name << "\":{";
    output << "\"n\":" << value.count
           << ",\"mean\":" << value.mean
           << ",\"p50\":" << value.p50
           << ",\"p95\":" << value.p95
           << ",\"p99\":" << value.p99
           << ",\"max\":" << value.max << '}';
}

std::string summary_json(
    const PerfSummaryRecord& record,
    const PerfSummaryOptions& options) {
    const double controller_hz = safe_rate(
        record.controller_ticks, record.window_seconds);
    const double output_hz = safe_rate(
        record.output_delivered, record.window_seconds);
    const double vision_hz = safe_rate(record.vision_frames, record.window_seconds);
    const double active_vision_hz = safe_rate(
        record.active_vision_frames, record.aiming_seconds);
    const double idle_vision_hz = safe_rate(
        record.idle_vision_frames, record.idle_seconds);
    const double accumulated_mean = record.vision_frames > 0
        ? static_cast<double>(record.accumulated_frames) /
            static_cast<double>(record.vision_frames) : 0.0;
    const double active_accumulated_mean = record.active_vision_frames > 0
        ? static_cast<double>(record.active_accumulated_frames) /
            static_cast<double>(record.active_vision_frames) : 0.0;
    const double idle_accumulated_mean = record.idle_vision_frames > 0
        ? static_cast<double>(record.idle_accumulated_frames) /
            static_cast<double>(record.idle_vision_frames) : 0.0;

    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << "{\"schema_version\":2,\"type\":\"runtime_perf_summary\""
           << ",\"build_commit\":\"" << options.build_commit << '"'
           << ",\"config_sha256\":\"" << options.config_sha256 << '"'
           << ",\"engine_sha256\":\"" << options.engine_sha256 << '"'
           << ",\"histogram_bucket_ms\":" << kHistogramBucketWidthMs
           << ",\"window_start_steady_ns\":" << record.window_start_ns
           << ",\"window_end_steady_ns\":" << record.window_end_ns
           << ",\"window_ms\":" << record.window_seconds * 1000.0
           << ",\"controller_hz\":" << controller_hz
           << ",\"output_hz\":" << output_hz
           << ",\"aiming_ratio\":"
           << (record.window_seconds > 0.0
                   ? record.aiming_seconds / record.window_seconds : 0.0)
           << ",\"vision_hz\":" << vision_hz
           << ",\"vision_active_hz\":" << active_vision_hz
           << ",\"vision_idle_hz\":" << idle_vision_hz
           << ",\"vision_frames\":" << record.vision_frames
           << ",\"accumulated_frames_mean\":" << accumulated_mean
           << ",\"accumulated_gt1_pct\":"
           << safe_percent(record.accumulated_gt_one, record.vision_frames)
           << ",\"accumulated_active_mean\":" << active_accumulated_mean
           << ",\"accumulated_active_gt1_pct\":"
           << safe_percent(
                  record.active_accumulated_gt_one, record.active_vision_frames)
           << ",\"accumulated_idle_mean\":" << idle_accumulated_mean
           << ",\"accumulated_idle_gt1_pct\":"
           << safe_percent(
                  record.idle_accumulated_gt_one, record.idle_vision_frames)
           << ",\"writer_queue_dropped_total\":"
           << record.writer_queue_dropped_total
           << ",\"latency_ms\":{";
    write_latency_json(output, "controller_tick", record.controller_tick, false);
    write_latency_json(output, "controller_pipeline", record.controller_pipeline);
    write_latency_json(output, "vigem_update", record.vigem_update);
    write_latency_json(output, "capture_to_result", record.capture_to_result);
    write_latency_json(output, "copy_to_result", record.copy_to_result);
    write_latency_json(output, "source_present_to_result", record.source_present_to_result);
    write_latency_json(output, "result_to_controller", record.result_to_controller);
    write_latency_json(output, "result_to_vigem", record.result_to_vigem);
    write_latency_json(output, "vision_publish_to_vigem", record.vision_publish_to_vigem);
    write_latency_json(output, "controller_consume_to_vigem", record.controller_consume_to_vigem);
    write_latency_json(
        output,
        "controller_submit_to_final_output",
        record.controller_submit_to_final_output);
    write_latency_json(output, "final_output_to_vigem", record.final_output_to_vigem);
    write_latency_json(output, "source_present_to_vigem", record.source_present_to_vigem);
    write_latency_json(output, "cuda_map", record.cuda_map);
    write_latency_json(output, "preprocess", record.preprocess);
    write_latency_json(output, "infer", record.infer);
    write_latency_json(output, "gpu_total", record.gpu_total);
    write_latency_json(output, "output_copy_sync", record.output_copy_sync);
    write_latency_json(output, "output_copy", record.output_copy);
    write_latency_json(output, "output_wait", record.output_wait);
    write_latency_json(output, "sync_queue_residual", record.sync_queue_residual);
    write_latency_json(output, "color_copy", record.color_copy);
    write_latency_json(output, "cuda_unmap", record.cuda_unmap);
    write_latency_json(output, "ego_stage", record.ego_stage);
    write_latency_json(output, "ego_compute", record.ego_compute);
    output << "}}";
    return output.str();
}

std::string summary_console(const PerfSummaryRecord& record) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1)
           << "[PerfSummary] ctrl="
           << safe_rate(record.controller_ticks, record.window_seconds)
           << "Hz output="
           << safe_rate(record.output_delivered, record.window_seconds)
           << "Hz vision=" << safe_rate(record.vision_frames, record.window_seconds)
           << "Hz active="
           << safe_rate(record.active_vision_frames, record.aiming_seconds)
           << "Hz idle=" << safe_rate(record.idle_vision_frames, record.idle_seconds)
           << "Hz "
           << (record.active_vision_frames > 0 ? "active_accum>1=" : "idle_accum>1=")
           << (record.active_vision_frames > 0
                   ? safe_percent(
                         record.active_accumulated_gt_one,
                         record.active_vision_frames)
                   : safe_percent(
                         record.idle_accumulated_gt_one,
                         record.idle_vision_frames))
           << "% | copy->result " << record.copy_to_result.p50 << '/'
           << record.copy_to_result.p95
           << "ms present->vigem " << record.source_present_to_vigem.p50 << '/'
           << record.source_present_to_vigem.p95
           << "ms | wait " << record.output_wait.p50 << '/'
           << record.output_wait.p95 << "ms queue~ "
           << record.sync_queue_residual.p50 << '/'
           << record.sync_queue_residual.p95
           << "ms | W3 stage " << record.ego_stage.p50 << '/'
           << record.ego_stage.p95 << "ms compute " << record.ego_compute.p50
           << '/' << record.ego_compute.p95 << "ms";
    return output.str();
}

std::filesystem::path make_perf_summary_path(
    const std::filesystem::path& directory) {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return directory / ("runtime_perf_summary_" + std::to_string(stamp) + ".jsonl");
}

}  // namespace

double loop_fps_from_elapsed_ms(double elapsed_ms) {
    if (elapsed_ms <= 0.0) {
        return 0.0;
    }
    return 1000.0 / elapsed_ms;
}

PerfLogger::PerfLogger(bool enabled)
    : enabled_(enabled) {}

void PerfLogger::record_empty_sample() const {
    PerfSnapshot snapshot;
    record_sample(snapshot);
}

void PerfLogger::record_sample(const PerfSnapshot& snapshot) const {
    if (!enabled_) {
        return;
    }
    std::cout
        << "[Perf][CPP] loop=" << snapshot.loop_fps
        << " FPS | native=" << snapshot.native_ms
        << "ms | consume=" << snapshot.consume_ms
        << "ms | out_age=" << snapshot.out_age_ms
        << "ms | detail gpu_total=" << snapshot.gpu_total_ms
        << "ms sync_wait=" << snapshot.sync_wait_ms
        << "ms | tier " << snapshot.target_tier
        << " | fire req=" << snapshot.fire_requested
        << " ok=" << snapshot.fire_allowed
        << " block=" << snapshot.fire_blocked
        << " | box samples=" << snapshot.box_samples
        << " | ctrl_loop=" << snapshot.ctrl_loop_ms
        << "ms ctrl_pipeline=" << snapshot.ctrl_pipeline_ms
        << "ms vigem_update=" << snapshot.vigem_update_ms
        << "ms\n";
}

struct PerfSummaryLogger::Impl {
    explicit Impl(PerfSummaryOptions value)
        : options(std::move(value)),
          interval_ns(std::max<std::uint64_t>(
              1'000'000ull,
              static_cast<std::uint64_t>(options.interval_ms) * 1'000'000ull)) {
        if (!options.enabled) return;
        std::error_code error;
        std::filesystem::create_directories(options.directory, error);
        if (error) {
            std::cerr << "[PerfSummary][Warn] failed to create directory "
                      << options.directory.string() << ": " << error.message() << '\n';
            return;
        }
        path = make_perf_summary_path(options.directory);
        {
            std::ofstream probe(path, std::ios::binary | std::ios::trunc);
            if (!probe) {
                std::cerr << "[PerfSummary][Warn] failed to open "
                          << path.string() << '\n';
                path.clear();
                return;
            }
        }
        active = true;
        writer = std::thread(&Impl::writer_loop, this);
        std::cout << "[PerfSummary] lightweight window log=" << path.string()
                  << " interval_ms=" << options.interval_ms << '\n';
    }

    ~Impl() {
        finish();
    }

    void record_controller(const PerfControllerWindowSample& sample) noexcept {
        if (!active || stopped || sample.timestamp_ns == 0) return;
        if (window.start_ns == 0) {
            window.reset(sample.timestamp_ns, sample.aiming);
        } else {
            window.advance_clock(sample.timestamp_ns);
            if (sample.timestamp_ns >= window.start_ns + interval_ns) {
                enqueue(window.snapshot(sample.timestamp_ns));
                window.reset(sample.timestamp_ns, sample.aiming);
            }
        }
        window.last_aiming = sample.aiming;
        ++window.controller_ticks;
        if (sample.output_delivered) ++window.output_delivered;
        window.controller_tick.observe(sample.tick_ms);
        window.controller_pipeline.observe(sample.pipeline_ms);
        window.vigem_update.observe(sample.vigem_ms);
    }

    void record_vision(const PerfVisionWindowSample& sample) noexcept {
        if (!active || stopped || window.start_ns == 0) return;
        ++window.vision_frames;
        const std::uint64_t accumulated = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(sample.accumulated_frames));
        window.accumulated_frames += accumulated;
        if (accumulated > 1) ++window.accumulated_gt_one;
        if (sample.aiming) {
            ++window.active_vision_frames;
            window.active_accumulated_frames += accumulated;
            if (accumulated > 1) ++window.active_accumulated_gt_one;
        } else {
            ++window.idle_vision_frames;
            window.idle_accumulated_frames += accumulated;
            if (accumulated > 1) ++window.idle_accumulated_gt_one;
        }
        window.capture_to_result.observe(sample.capture_to_result_ms);
        window.copy_to_result.observe(sample.copy_to_result_ms);
        window.source_present_to_result.observe(sample.source_present_to_result_ms);
        window.result_to_controller.observe(sample.result_to_controller_ms);
        window.result_to_vigem.observe(sample.result_to_vigem_ms);
        window.vision_publish_to_vigem.observe(sample.vision_publish_to_vigem_ms);
        window.controller_consume_to_vigem.observe(sample.controller_consume_to_vigem_ms);
        window.controller_submit_to_final_output.observe(
            sample.controller_submit_to_final_output_ms);
        window.final_output_to_vigem.observe(sample.final_output_to_vigem_ms);
        window.source_present_to_vigem.observe(sample.source_present_to_vigem_ms);
        window.cuda_map.observe(sample.cuda_map_ms);
        window.preprocess.observe(sample.preprocess_ms);
        window.infer.observe(sample.infer_ms);
        window.gpu_total.observe(sample.gpu_total_ms);
        window.output_copy_sync.observe(sample.output_copy_sync_ms);
        window.output_copy.observe(sample.output_copy_ms);
        window.output_wait.observe(sample.output_wait_ms);
        window.sync_queue_residual.observe(sample.sync_queue_residual_ms);
        window.color_copy.observe(sample.color_copy_ms);
        window.cuda_unmap.observe(sample.cuda_unmap_ms);
        window.ego_stage.observe(sample.ego_stage_ms);
        window.ego_compute.observe(sample.ego_compute_ms);
    }

    void enqueue(PerfSummaryRecord record) noexcept {
        try {
            std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
            if (!lock.owns_lock()) {
                ++queue_dropped;
                return;
            }
            if (queue.size() >= kSummaryQueueCapacity) {
                queue.pop_front();
                ++queue_dropped;
            }
            record.writer_queue_dropped_total = queue_dropped;
            queue.push_back(std::move(record));
            queue_condition.notify_one();
        } catch (...) {
            ++queue_dropped;
        }
    }

    void finish() noexcept {
        if (stopped) return;
        stopped = true;
        if (active && window.start_ns != 0 && window.last_controller_ns > window.start_ns &&
            window.controller_ticks > 0) {
            enqueue(window.snapshot(window.last_controller_ns));
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop_requested = true;
        }
        queue_condition.notify_all();
        if (writer.joinable()) writer.join();
        active = false;
    }

    void writer_loop() noexcept {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output) return;
        for (;;) {
            PerfSummaryRecord record;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_condition.wait(lock, [this] {
                    return stop_requested || !queue.empty();
                });
                if (queue.empty()) {
                    if (stop_requested) return;
                    continue;
                }
                record = std::move(queue.front());
                queue.pop_front();
            }
            output << summary_json(record, options) << '\n';
            output.flush();
            if (options.stdout_enabled) {
                std::cout << summary_console(record) << '\n';
            }
        }
    }

    PerfSummaryOptions options;
    std::uint64_t interval_ns = 0;
    bool active = false;
    bool stopped = false;
    PerfSummaryWindow window;
    std::filesystem::path path;
    std::thread writer;
    std::mutex queue_mutex;
    std::condition_variable queue_condition;
    std::deque<PerfSummaryRecord> queue;
    bool stop_requested = false;
    std::uint64_t queue_dropped = 0;
};

PerfSummaryLogger::PerfSummaryLogger(PerfSummaryOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

PerfSummaryLogger::~PerfSummaryLogger() {
    stop();
}

bool PerfSummaryLogger::enabled() const noexcept {
    return impl_ != nullptr && impl_->active && !impl_->stopped;
}

void PerfSummaryLogger::record_controller(
    const PerfControllerWindowSample& sample) noexcept {
    if (impl_ != nullptr) impl_->record_controller(sample);
}

void PerfSummaryLogger::record_vision(
    const PerfVisionWindowSample& sample) noexcept {
    if (impl_ != nullptr) impl_->record_vision(sample);
}

void PerfSummaryLogger::stop() noexcept {
    if (impl_ != nullptr) impl_->finish();
}

const std::filesystem::path& PerfSummaryLogger::log_path() const noexcept {
    static const std::filesystem::path empty;
    return impl_ != nullptr ? impl_->path : empty;
}

}  // namespace runtime_app
