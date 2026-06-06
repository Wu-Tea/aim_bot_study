#include "perf_logger.h"

#include <iostream>

namespace runtime_app {

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

}  // namespace runtime_app
