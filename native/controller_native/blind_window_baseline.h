#pragma once

#include "blind_window_benchmark.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace controller_native::blind_window {

struct BlindEpisodeSummary {
    std::uint32_t seed = 0;
    int vision_hz = 0;
    int phase_percent = 0;
    int result_latency_ms = 0;
    int response_delay_ms = 0;
    BlindWindowMetrics metrics{};
};

struct BlindBaselineSummary {
    bool baseline_discriminating = false;
    std::vector<BlindEpisodeSummary> episodes;
};

using BlindControllerFactory = std::function<BlindControllerStep()>;

BlindBaselineSummary run_k1_baseline_matrix();
BlindBaselineSummary run_k1_baseline_matrix(
    const BlindControllerFactory& controller_factory);

std::string serialize_k1_baseline_json(
    const BlindBaselineSummary& summary,
    const std::string& revision,
    bool dirty,
    const std::string& policy_identity =
        "stale_proportional_fixture_baseline",
    bool production_controller = false,
    const std::string& config_path = {},
    std::uint64_t config_fingerprint = 0);

}  // namespace controller_native::blind_window
