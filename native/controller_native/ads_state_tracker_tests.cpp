#include "ads_state_tracker.h"

#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_false(bool condition, const char* message) {
    if (condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    const float delta = actual > expected ? actual - expected : expected - actual;
    if (delta > tolerance) {
        throw std::runtime_error(message);
    }
}

void test_ads_tracker_reports_transitions_and_snap_window() {
    controller_native::AdsStateTracker tracker;

    controller_native::AdsStateTransition transition = tracker.update(true, 10.0);
    require_true(transition.started, "ADS transition should report start");
    require_false(transition.stopped, "ADS start should not report stop");
    require_true(tracker.active(), "ADS should be active after aiming starts");
    require_near(
        tracker.snap_progress_ratio(120, 10.060),
        0.5f,
        0.001f,
        "ADS snap progress should use start time and window");
    require_near(
        tracker.snap_remaining_seconds(120, 10.060),
        0.060f,
        0.001f,
        "ADS snap remaining should use start time and window");
    require_true(
        tracker.snap_window_active(120, 10.120),
        "ADS snap should be active at the inclusive window edge");
    require_false(
        tracker.snap_window_active(120, 10.121),
        "ADS snap should be inactive after the window edge");

    transition = tracker.update(true, 10.130);
    require_false(transition.started, "continuing ADS should not report another start");
    require_false(transition.stopped, "continuing ADS should not report stop");

    transition = tracker.update(false, 10.140);
    require_false(transition.started, "ADS stop should not report start");
    require_true(transition.stopped, "ADS transition should report stop");
    require_false(tracker.active(), "ADS should be inactive after aiming stops");
    require_near(
        tracker.snap_progress_ratio(120, 10.150),
        0.0f,
        0.001f,
        "inactive ADS should have zero snap progress");
    require_near(
        tracker.snap_remaining_seconds(120, 10.150),
        0.0f,
        0.001f,
        "inactive ADS should have zero snap remaining");
}

}  // namespace

int main() {
    test_ads_tracker_reports_transitions_and_snap_window();
    return 0;
}
