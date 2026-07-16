#pragma once

#include <cstdint>

namespace controller_native {

struct ControlResponseSample {
    float left_stick_axis = 0.0f;
    float isolated_response_px_per_second = 0.0f;
    bool clean_window = false;
    bool ambiguous = false;
};

struct ControlResponseEstimate {
    float scale_px_per_stick_second = 0.0f;
    float confidence = 0.0f;
    std::uint64_t ads_epoch = 0;
    std::uint32_t accepted_samples = 0;
};

struct ControlResponseEstimatorConfig {
    float minimum_excitation = 0.1f;
    float scale_alpha = 0.12f;
    float confidence_alpha = 0.05f;
    float frozen_confidence_decay = 0.999f;
    float epoch_confidence_scale = 0.2f;
    float max_scale_px_per_stick_second = 2000.0f;
};

class ControlResponseEstimator {
public:
    explicit ControlResponseEstimator(ControlResponseEstimatorConfig config = {});

    void begin_ads_epoch(std::uint64_t epoch) noexcept;
    bool update(const ControlResponseSample& sample) noexcept;
    ControlResponseEstimate estimate() const noexcept;
    void reset() noexcept;

private:
    ControlResponseEstimatorConfig config_{};
    ControlResponseEstimate estimate_{};
};

}  // namespace controller_native
