#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace runtime_app {

double loop_fps_from_elapsed_ms(double elapsed_ms);

struct PerfSnapshot {
    double loop_fps = 0.0;
    double native_ms = 0.0;
    double consume_ms = 0.0;
    double out_age_ms = 0.0;
    double gpu_total_ms = 0.0;
    double sync_wait_ms = 0.0;
    double ctrl_loop_ms = 0.0;
    double ctrl_pipeline_ms = 0.0;
    double vigem_update_ms = 0.0;
    std::string target_tier = "none";
    std::uint64_t fire_requested = 0;
    std::uint64_t fire_allowed = 0;
    std::uint64_t fire_blocked = 0;
    double box_samples = 0.0;
};

class PerfLogger {
public:
    explicit PerfLogger(bool enabled);

    void record_empty_sample() const;
    void record_sample(const PerfSnapshot& snapshot) const;

private:
    bool enabled_ = false;
};

// Low-overhead performance observation is intentionally separate from the
// detailed telemetry stream. The controller thread only updates fixed
// counters/histograms; one compact window is serialized by a background writer.
struct PerfSummaryOptions {
    bool enabled = false;
    unsigned int interval_ms = 5000;
    std::filesystem::path directory = "runs/perf_summary";
    bool stdout_enabled = true;
    std::string build_commit = "unknown";
    std::string config_sha256;
    std::string engine_sha256;
};

struct PerfControllerWindowSample {
    std::uint64_t timestamp_ns = 0;
    bool aiming = false;
    bool output_delivered = false;
    double tick_ms = -1.0;
    double pipeline_ms = -1.0;
    double vigem_ms = -1.0;
};

struct PerfVisionWindowSample {
    bool aiming = false;
    std::uint32_t accumulated_frames = 1;
    double capture_to_result_ms = -1.0;
    double copy_to_result_ms = -1.0;
    double source_present_to_result_ms = -1.0;
    double result_to_controller_ms = -1.0;
    double result_to_vigem_ms = -1.0;
    double vision_publish_to_vigem_ms = -1.0;
    double controller_consume_to_vigem_ms = -1.0;
    double controller_submit_to_final_output_ms = -1.0;
    double final_output_to_vigem_ms = -1.0;
    double source_present_to_vigem_ms = -1.0;
    double cuda_map_ms = -1.0;
    double preprocess_ms = -1.0;
    double infer_ms = -1.0;
    double gpu_total_ms = -1.0;
    double output_copy_sync_ms = -1.0;
    double output_copy_ms = -1.0;
    double output_wait_ms = -1.0;
    // Diagnostic proxy only: the CPU sync wait that is not explained by the
    // measured preprocess+inference GPU interval. CPU and CUDA-event timing
    // boundaries differ, so this must not be treated as exact queue residency.
    double sync_queue_residual_ms = -1.0;
    double color_copy_ms = -1.0;
    double cuda_unmap_ms = -1.0;
};

class PerfSummaryLogger {
public:
    explicit PerfSummaryLogger(PerfSummaryOptions options = {});
    ~PerfSummaryLogger();

    PerfSummaryLogger(const PerfSummaryLogger&) = delete;
    PerfSummaryLogger& operator=(const PerfSummaryLogger&) = delete;

    bool enabled() const noexcept;
    void record_controller(const PerfControllerWindowSample& sample) noexcept;
    void record_vision(const PerfVisionWindowSample& sample) noexcept;
    void stop() noexcept;

    const std::filesystem::path& log_path() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace runtime_app
