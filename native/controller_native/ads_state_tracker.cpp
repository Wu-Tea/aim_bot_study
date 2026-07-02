#include "ads_state_tracker.h"

#include <algorithm>

namespace controller_native {

void AdsStateTracker::reset() {
    active_ = false;
    started_at_seconds_ = 0.0;
}

AdsStateTransition AdsStateTracker::update(bool aiming, double now_seconds) {
    AdsStateTransition transition;
    if (aiming) {
        if (!active_) {
            active_ = true;
            started_at_seconds_ = now_seconds;
            transition.started = true;
        }
        return transition;
    }

    if (active_ || started_at_seconds_ > 0.0) {
        transition.stopped = true;
    }
    active_ = false;
    started_at_seconds_ = 0.0;
    return transition;
}

bool AdsStateTracker::active() const {
    return active_;
}

double AdsStateTracker::started_at_seconds() const {
    return started_at_seconds_;
}

bool AdsStateTracker::snap_window_active(int window_ms, double now_seconds) const {
    if (!active_ || started_at_seconds_ <= 0.0) {
        return false;
    }
    const double window_seconds =
        static_cast<double>(std::max(0, window_ms)) / 1000.0;
    return now_seconds - started_at_seconds_ <= window_seconds;
}

float AdsStateTracker::snap_progress_ratio(int window_ms, double now_seconds) const {
    if (!active_ || started_at_seconds_ <= 0.0) {
        return 0.0f;
    }
    const double window_seconds =
        static_cast<double>(std::max(1, window_ms)) / 1000.0;
    const double elapsed = std::max(0.0, now_seconds - started_at_seconds_);
    return static_cast<float>(std::max(0.0, std::min(1.0, elapsed / window_seconds)));
}

float AdsStateTracker::snap_remaining_seconds(int window_ms, double now_seconds) const {
    if (!active_ || started_at_seconds_ <= 0.0) {
        return 0.0f;
    }
    const double window_seconds =
        static_cast<double>(std::max(1, window_ms)) / 1000.0;
    const double elapsed = std::max(0.0, now_seconds - started_at_seconds_);
    return static_cast<float>(std::max(0.0, window_seconds - elapsed));
}

bool AdsStateTracker::min_ads_elapsed(float min_ads_ms, double now_seconds) const {
    const double min_ads_seconds =
        static_cast<double>(std::max(0.0f, min_ads_ms)) / 1000.0;
    return active_ &&
        started_at_seconds_ > 0.0 &&
        now_seconds - started_at_seconds_ >= min_ads_seconds;
}

}  // namespace controller_native
