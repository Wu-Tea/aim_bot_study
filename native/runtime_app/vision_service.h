#pragma once
#include "runtime_timing.h"

#include "pipeline_contract/target_snapshot.h"
#include "pipeline_contract/assist_activation.h"
#include "viewport_controller.h"
#include "vision_native/types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

namespace runtime_app {

enum class VisionSnapshotFreshness {
    None,
    Fresh,
    NoUpdate,
};

enum class VisionSourceState {
    Unknown,
    FreshFrame,
    NoUpdate,
};

struct VisionServiceOptions {
    double capture_fps = 160.0;
    double idle_fps = 60.0;
    bool keepwarm_when_idle = true;
    // Cadence only: inactive snapshots never gain aim/fire authority.
    int aim_release_hold_ms = 0;
};

struct VisionServiceSnapshot {
    vision_native::VisionResult result;
    VisionSnapshotFreshness freshness = VisionSnapshotFreshness::None;
    VisionSourceState source_state = VisionSourceState::Unknown;
    std::uint64_t sequence = 0;
    std::uint64_t aim_transition_sequence = 0;
    pipeline_contract::VisionRequest request = pipeline_contract::VisionRequest::Idle;
    bool engine_aiming = false;
    float aim_wakeup_to_dispatch_ms = 0.0f;
    float aim_wakeup_to_capture_ms = 0.0f;
    float aim_wakeup_to_result_ms = 0.0f;
    float requested_vision_fps = 0.0f;
    // Monotonic timestamp taken when this snapshot is committed to the
    // latest-only mailbox. It is distinct from result/capture timestamps.
    std::uint64_t published_at_ns = 0;
    std::uint64_t policy_revision = 0;
};

struct VisionDetectionPolicy {
    bool friendly = true;
    float height = .4f;
    float wide_height = .65f;
    std::uint64_t revision = 0;
};

// Final delivery fence between Vision and the controller. The service is a
// latest-only mailbox, while this gate guarantees that a published result is
// a unique, forward-moving, recent capture before it can become control input.
class VisionDeliveryGate {
public:
    explicit VisionDeliveryGate(float max_source_age_ms = 50.0f) noexcept;

    bool accept(
        const vision_native::VisionResult& result,
        std::uint64_t controller_consume_ns) noexcept;
    void reset() noexcept;

private:
    float max_source_age_ms_ = 50.0f;
    std::uint64_t last_frame_id_ = 0;
    std::uint64_t last_capture_ns_ = 0;
    std::uint64_t last_source_ns_ = 0;
    std::uint64_t last_present_qpc_ = 0;
    std::uint64_t last_present_frequency_ = 0;
    bool has_delivery_ = false;
};

class IVisionServicePoller {
public:
    virtual ~IVisionServicePoller() = default;

    virtual void set_request(pipeline_contract::VisionRequest request) = 0;
    virtual void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) = 0;
    virtual void set_viewport(const ViewportRequest&) {}
    virtual void set_detection_policy(const VisionDetectionPolicy&) {}
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

    std::uint64_t set_request(
        pipeline_contract::VisionRequest request,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent);
    void set_viewport(const ViewportRequest& request);
    void set_detection_policy(VisionDetectionPolicy policy);

    // Returns an empty snapshot when the consumer already has this sequence.
    // Worker failures are rethrown on the consuming runtime thread.
    VisionServiceSnapshot latest_snapshot(std::uint64_t after_sequence = 0) const;
    bool step_for_test(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point next_poll_due_for_test(
        std::chrono::steady_clock::time_point now) const;

private:
    // Caller holds mutex_. This is independent of controller authority.
    bool full_rate_requested(std::chrono::steady_clock::time_point now) const;
    bool step(std::chrono::steady_clock::time_point now);
    std::chrono::steady_clock::time_point next_poll_due(
        std::chrono::steady_clock::time_point now) const;
    void run_loop();

    std::unique_ptr<IVisionServicePoller> poller_;
    VisionServiceOptions options_;
    mutable std::mutex mutex_;
    InterruptibleDeadlineWait wake_deadline_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    pipeline_contract::VisionRequest request_ = pipeline_contract::VisionRequest::Idle;
    std::chrono::steady_clock::time_point release_hold_until_{};
    pipeline_contract::UserAimIntent user_aim_intent_;
    ViewportRequest viewport_request_;
    VisionDetectionPolicy policy_{};
    std::uint64_t applied_policy_revision_ = 0;
    std::chrono::steady_clock::time_point last_poll_at_{};
    bool has_last_poll_ = false;
    VisionServiceSnapshot latest_snapshot_;
    std::exception_ptr worker_failure_;
    std::uint64_t sequence_ = 0;
    std::uint64_t aim_transition_sequence_ = 0;
    bool immediate_poll_requested_ = false;
    std::chrono::steady_clock::time_point aim_transition_requested_at_{};
};

} // namespace runtime_app
