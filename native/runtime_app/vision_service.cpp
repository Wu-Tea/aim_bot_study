#include "vision_service.h"

#include "runtime_timing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace runtime_app {
namespace {

std::chrono::steady_clock::duration interval_for_fps(double fps) {
    if (fps <= 0.0) {
        return std::chrono::steady_clock::duration::max();
    }
    const auto microseconds = static_cast<std::int64_t>(
        std::lround(1'000'000.0 / fps));
    return std::chrono::microseconds(std::max<std::int64_t>(1, microseconds));
}

void clear_reused_authority(vision_native::VisionResult& result) {
    result.aim_authority = false;
    result.fire_authority = false;
    result.auto_fire = false;
}

const char* freshness_name(VisionSnapshotFreshness freshness) {
    switch (freshness) {
    case VisionSnapshotFreshness::Fresh:
        return "fresh";
    case VisionSnapshotFreshness::Reused:
        return "reused";
    case VisionSnapshotFreshness::NoUpdate:
        return "no_update";
    case VisionSnapshotFreshness::None:
    default:
        return "none";
    }
}

const char* source_state_name(VisionSourceState state) {
    switch (state) {
    case VisionSourceState::FreshFrame:
        return "fresh_frame";
    case VisionSourceState::RepeatLastFrame:
        return "repeat_last_frame";
    case VisionSourceState::NoUpdate:
        return "no_update";
    case VisionSourceState::Unknown:
    default:
        return "unknown";
    }
}

void stamp_service_metadata(VisionServiceSnapshot& snapshot) {
    snapshot.result.service_freshness = freshness_name(snapshot.freshness);
    snapshot.result.service_source_state = source_state_name(snapshot.source_state);
    snapshot.result.service_sequence = snapshot.sequence;
    snapshot.result.service_controller_aiming = snapshot.controller_aiming;
    snapshot.result.service_engine_aiming = snapshot.engine_aiming;
}

} // namespace

VisionService::VisionService(std::unique_ptr<IVisionServicePoller> poller, VisionServiceOptions options)
    : poller_(std::move(poller)), options_(options) {
    if (poller_ == nullptr) {
        throw std::invalid_argument("VisionService requires a poller");
    }
}

VisionService::~VisionService() {
    stop();
}

void VisionService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread(&VisionService::run_loop, this);
}

void VisionService::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    wake_condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void VisionService::set_aiming(bool aiming) {
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wake = aiming && !controller_aiming_;
        controller_aiming_ = aiming;
        if (wake) {
            ++aim_transition_sequence_;
            immediate_poll_requested_ = true;
            has_last_fresh_result_ = false;
        }
    }
    if (wake) wake_condition_.notify_one();
}

void VisionService::set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_aim_intent_ = intent;
}

VisionServiceSnapshot VisionService::latest_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_snapshot_;
}

bool VisionService::step_for_test(std::chrono::steady_clock::time_point now) {
    return step(now);
}

std::chrono::steady_clock::time_point VisionService::next_poll_due_for_test(
    std::chrono::steady_clock::time_point now) const {
    return next_poll_due(now);
}

bool VisionService::step(std::chrono::steady_clock::time_point now) {
    bool controller_aiming = false;
    bool engine_aiming = false;
    pipeline_contract::UserAimIntent intent;
    std::uint64_t aim_transition_sequence = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controller_aiming = controller_aiming_;
        engine_aiming = controller_aiming_ || options_.keepwarm_when_idle;
        const double fps = controller_aiming_ ? options_.active_fps : options_.idle_fps;
        if (!engine_aiming || interval_for_fps(fps) == std::chrono::steady_clock::duration::max()) {
            return false;
        }
        if (!immediate_poll_requested_ && has_last_poll_ &&
            now - last_poll_at_ < interval_for_fps(fps)) {
            return false;
        }
        immediate_poll_requested_ = false;
        last_poll_at_ = now;
        has_last_poll_ = true;
        intent = user_aim_intent_;
        aim_transition_sequence = aim_transition_sequence_;
    }

    poller_->set_aiming(engine_aiming);
    poller_->set_user_aim_intent(intent);
    vision_native::VisionResult result = poller_->poll_once();

    VisionServiceSnapshot snapshot;
    snapshot.controller_aiming = controller_aiming;
    snapshot.engine_aiming = engine_aiming;
    snapshot.aim_transition_sequence = aim_transition_sequence;
    snapshot.result = result;

    std::lock_guard<std::mutex> lock(mutex_);
    if (controller_aiming && aim_transition_sequence != aim_transition_sequence_) {
        clear_reused_authority(snapshot.result);
    }
    snapshot.sequence = ++sequence_;
    if (result.frame_updated) {
        snapshot.freshness = VisionSnapshotFreshness::Fresh;
        snapshot.source_state = VisionSourceState::FreshFrame;
        if (!controller_aiming) {
            clear_reused_authority(snapshot.result);
        } else {
            last_fresh_result_ = result;
            has_last_fresh_result_ = true;
        }
    } else if (options_.repeat_last_on_no_update && has_last_fresh_result_) {
        snapshot.result = last_fresh_result_;
        snapshot.result.frame_updated = true;
        clear_reused_authority(snapshot.result);
        snapshot.freshness = VisionSnapshotFreshness::Reused;
        snapshot.source_state = VisionSourceState::RepeatLastFrame;
    } else {
        snapshot.freshness = VisionSnapshotFreshness::NoUpdate;
        snapshot.source_state = VisionSourceState::NoUpdate;
    }
    stamp_service_metadata(snapshot);
    latest_snapshot_ = snapshot;
    return true;
}

std::chrono::steady_clock::time_point VisionService::next_poll_due(
    std::chrono::steady_clock::time_point now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool engine_aiming = controller_aiming_ || options_.keepwarm_when_idle;
    const double fps = controller_aiming_ ? options_.active_fps : options_.idle_fps;
    const auto interval = interval_for_fps(fps);
    if (!engine_aiming || interval == std::chrono::steady_clock::duration::max()) {
        return now + std::chrono::milliseconds(5);
    }
    if (!has_last_poll_) {
        return now;
    }
    return std::max(now, last_poll_at_ + interval);
}

void VisionService::run_loop() {
    (void)set_current_thread_priority(RuntimeThreadPriority::AboveNormal);
    while (running_.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (step(now)) {
            continue;
        }
        const auto due = next_poll_due(now);
        std::unique_lock<std::mutex> lock(mutex_);
        wake_condition_.wait_until(lock, due, [this] {
            return !running_.load() || immediate_poll_requested_;
        });
    }
}

} // namespace runtime_app
