#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace mouse_native {
struct MouseRecoilConfig {
    bool enabled = false;
    double counts_per_second = 30.0;
    bool require_ads = true;
};
struct MouseRecoilSample {
    std::int32_t dy = 0;
    double requested_counts = 0, remainder = 0;
    bool active = false, clock_discontinuity = false;
};
class MouseRecoil {
public:
    explicit MouseRecoil(MouseRecoilConfig config = {}) : config_(config) {
        if (!std::isfinite(config.counts_per_second) || config.counts_per_second < 0 ||
            config.counts_per_second > 10000)
            throw std::invalid_argument("mouse recoil_counts_per_second must be 0..10000");
    }
    MouseRecoilSample tick(double now, bool ads, bool firing, bool allowed) noexcept {
        MouseRecoilSample out;
        out.active = config_.enabled && config_.counts_per_second > 0 && allowed &&
            firing && (!config_.require_ads || ads) && std::isfinite(now) && now > 0;
        if (!out.active) { reset(); return out; }
        const double dt = now - previous_;
        if (active_ && (dt <= 0 || dt > 0.05)) {
            // Stale firing time cannot be accumulated into a large pull.
            residual_ = 0;
            out.clock_discontinuity = true;
        } else if (active_) {
            out.requested_counts = dt * config_.counts_per_second;
            residual_ += out.requested_counts;
            out.dy = static_cast<std::int32_t>(std::floor(residual_ + 1e-9));
            residual_ = std::max(0.0, residual_ - out.dy);
        }
        previous_ = now; active_ = true;
        out.remainder = residual_;
        return out;
    }
    void reset() noexcept { previous_ = residual_ = 0; active_ = false; }
private:
    MouseRecoilConfig config_;
    double previous_ = 0, residual_ = 0;
    bool active_ = false;
};
}
