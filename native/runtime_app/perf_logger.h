#pragma once

#include <cstdint>
#include <string>

namespace runtime_app {

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

}  // namespace runtime_app
