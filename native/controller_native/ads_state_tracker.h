#pragma once

namespace controller_native {

struct AdsStateTransition {
    bool started = false;
    bool stopped = false;
};

class AdsStateTracker {
public:
    void reset();
    AdsStateTransition update(bool aiming, double now_seconds);

    bool active() const;
    double started_at_seconds() const;
    bool snap_window_active(int window_ms, double now_seconds) const;
    float snap_progress_ratio(int window_ms, double now_seconds) const;
    float snap_remaining_seconds(int window_ms, double now_seconds) const;
    bool min_ads_elapsed(float min_ads_ms, double now_seconds) const;

private:
    bool active_ = false;
    double started_at_seconds_ = 0.0;
};

}  // namespace controller_native
