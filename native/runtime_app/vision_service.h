#pragma once

#include "pipeline_contract/target_snapshot.h"
#include "viewport_controller.h"
#include "vision_native/types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace runtime_app {

enum class VisionSnapshotFreshness {
    None,
    Fresh,
    Reused,
    NoUpdate,
};

enum class VisionSourceState {
    Unknown,
    FreshFrame,
    RepeatLastFrame,
    NoUpdate,
};

struct VisionServiceOptions {
    double active_fps = 100.0;
    double idle_fps = 20.0;
    bool keepwarm_when_idle = true;
    bool repeat_last_on_no_update = true;
};

struct VisionServiceSnapshot {
    vision_native::VisionResult result;
    VisionSnapshotFreshness freshness = VisionSnapshotFreshness::None;
    VisionSourceState source_state = VisionSourceState::Unknown;
    std::uint64_t sequence = 0;
    std::uint64_t aim_transition_sequence = 0;
    bool controller_aiming = false;
    bool engine_aiming = false;
    float aim_wakeup_to_dispatch_ms = 0.0f;
    float aim_wakeup_to_capture_ms = 0.0f;
    float aim_wakeup_to_result_ms = 0.0f;
    float requested_vision_fps = 0.0f;
};

class IVisionServicePoller {
public:
    virtual ~IVisionServicePoller() = default;

    virtual void set_aiming(bool aiming) = 0;
    virtual void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) = 0;
    virtual void set_viewport(const ViewportRequest&) {}
    virtual vision_native::VisionResult poll_once() = 0;
};

class VisionService {
public:
    VisionService(std::unique_ptr<IVisionServicePoller> poller, VisionServiceOptions options);
    ~VisionService();

    VisionService(const VisionService&) = delete;
    VisionService& operator=(const VisionService&) = delete;

    void start();
    void stop();

    void set_aiming(bool aiming);
    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent);
    void set_viewport(const ViewportRequest& request);

    VisionServiceSnapshot latest_snapshot() const;
    bool step_for_test(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point next_poll_due_for_test(
        std::chrono::steady_clock::time_point now) const;

private:
    bool step(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point next_poll_due(
        std::chrono::steady_clock::time_point now) const;
    void run_loop();

    std::unique_ptr<IVisionServicePoller> poller_;
    VisionServiceOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable wake_condition_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    bool controller_aiming_ = false;
    pipeline_contract::UserAimIntent user_aim_intent_;
    ViewportRequest viewport_request_;
    std::chrono::steady_clock::time_point last_poll_at_{};
    bool has_last_poll_ = false;
    VisionServiceSnapshot latest_snapshot_;
    vision_native::VisionResult last_fresh_result_;
    bool has_last_fresh_result_ = false;
    std::uint64_t sequence_ = 0;
    std::uint64_t aim_transition_sequence_ = 0;
    bool immediate_poll_requested_ = false;
    std::chrono::steady_clock::time_point aim_transition_requested_at_{};
};

} // namespace runtime_app
