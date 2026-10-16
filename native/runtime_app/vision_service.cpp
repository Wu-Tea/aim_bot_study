#include "vision_service.h"
#include "live_pipeline_trace.h"

#include "runtime_timing.h"
#include "vision_native/observation_time.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <optional>

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

void stamp_service_metadata(VisionServiceSnapshot& snapshot) {
    // Keep both diagnostic spellings, derived from one publication state.
    switch (snapshot.freshness) {
    case VisionSnapshotFreshness::Fresh:
        snapshot.result.service_freshness = "fresh";
        snapshot.result.service_source_state = "fresh_frame";
        break;
    case VisionSnapshotFreshness::NoUpdate:
        snapshot.result.service_freshness = "no_update";
        snapshot.result.service_source_state = "no_update";
        break;
    case VisionSnapshotFreshness::None:
    default:
        snapshot.result.service_freshness = "none";
        snapshot.result.service_source_state = "unknown";
        break;
    }
    snapshot.result.service_sequence = snapshot.sequence;
    snapshot.result.service_controller_aiming = pipeline_contract::requests_detection(snapshot.request);
    snapshot.result.service_engine_aiming = snapshot.engine_aiming;
    snapshot.result.aim_wakeup_to_dispatch_ms = snapshot.aim_wakeup_to_dispatch_ms;
    snapshot.result.aim_wakeup_to_capture_ms = snapshot.aim_wakeup_to_capture_ms;
    snapshot.result.aim_wakeup_to_result_ms = snapshot.aim_wakeup_to_result_ms;
    snapshot.result.requested_vision_fps = snapshot.requested_vision_fps;
}

} // namespace

VisionDeliveryGate::VisionDeliveryGate(float max_source_age_ms) noexcept
    : max_source_age_ms_(live_visual_lifetime_ms(max_source_age_ms)) {}

bool VisionDeliveryGate::accept(
    const vision_native::VisionResult& result,
    std::uint64_t controller_consume_ns) noexcept {
    const std::uint64_t capture_ns = result.captured_at_ns != 0
        ? result.captured_at_ns
        : result.result_at_ns;
    if (!result.frame_updated || result.frame_id == 0 || capture_ns == 0 ||
        controller_consume_ns < capture_ns ||
        (result.result_at_ns != 0 && (result.result_at_ns < capture_ns ||
                                    result.result_at_ns > controller_consume_ns))) {
        return false;
    }
    const auto source_ns = vision_native::observation_time_ns(result);
    if (source_ns == 0 || source_ns > capture_ns) return false;
    const double age_ms = (
        static_cast<double>(controller_consume_ns - source_ns) +
        (result.source_present_steady_available
            ? static_cast<double>(result.source_present_calibration_uncertainty_ns)
            : 0.0)) / 1'000'000.0;
    if (age_ms >= static_cast<double>(max_source_age_ms_)) {
        return false;
    }
    if (result.source_present_available &&
        (result.source_present_qpc == 0 || result.source_present_qpc_frequency == 0 ||
         (last_present_qpc_ != 0 &&
          (result.source_present_qpc_frequency != last_present_frequency_ ||
           result.source_present_qpc <= last_present_qpc_)))) {
        return false;
    }
    // Valid identities/timestamps are positive; zero watermarks admit the
    // first delivery after construction/reset without a separate flag.
    if (result.frame_id <= last_frame_id_ || capture_ns <= last_capture_ns_ ||
        source_ns <= last_source_ns_) {
        return false;
    }
    last_frame_id_ = result.frame_id;
    last_capture_ns_ = capture_ns;
    last_source_ns_ = source_ns;
    if (result.source_present_available) {
        last_present_qpc_ = result.source_present_qpc;
        last_present_frequency_ = result.source_present_qpc_frequency;
    }
    return true;
}

void VisionDeliveryGate::reset() noexcept {
    last_frame_id_ = 0;
    last_capture_ns_ = 0;
    last_source_ns_ = 0;
    last_present_qpc_ = 0;
    last_present_frequency_ = 0;
}

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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_failure_) std::rethrow_exception(worker_failure_);
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    try {
        worker_ = std::thread(&VisionService::run_loop, this);
    } catch (...) {
        running_.store(false);
        throw;
    }
}

void VisionService::stop() {
    running_.store(false);
    wake_deadline_.notify();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::uint64_t VisionService::set_request(
    pipeline_contract::VisionRequest request, std::chrono::steady_clock::time_point now) {
    bool state_changed = false;
    std::uint64_t transition_sequence = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_changed = request != request_;
        const bool wake = pipeline_contract::requests_detection(request) && state_changed;
        if (state_changed) {
            release_hold_until_ = !pipeline_contract::requests_detection(request) && options_.aim_release_hold_ms > 0
                ? now + std::chrono::milliseconds(options_.aim_release_hold_ms)
                : std::chrono::steady_clock::time_point{};
        }
        request_ = request;
        if (wake) {
            ++aim_transition_sequence_;
            immediate_poll_requested_ = true;
            aim_transition_requested_at_ = now;
        }
        transition_sequence = aim_transition_sequence_;
    }
    if (state_changed) wake_deadline_.notify();
    return transition_sequence;
}

void VisionService::set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_aim_intent_ = intent;
}

void VisionService::set_viewport(const ViewportRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    viewport_request_ = request;
}

void VisionService::set_detection_policy(VisionDetectionPolicy policy) {
    { std::lock_guard<std::mutex> lock(mutex_); policy_ = policy; immediate_poll_requested_ = true; }
    wake_deadline_.notify();
}

VisionServiceSnapshot VisionService::latest_snapshot(std::uint64_t after_sequence) const {
    std::shared_ptr<const VisionServiceSnapshot> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_failure_) std::rethrow_exception(worker_failure_);
        if (!latest_snapshot_ || latest_snapshot_->sequence == after_sequence) return {};
        snapshot = latest_snapshot_;
    }
    // Detection vectors may allocate/copy here. They must not hold the mutex
    // used by input activation and the producer's next dispatch.
    return *snapshot;
}

bool VisionService::step_for_test(std::chrono::steady_clock::time_point now) {
    return step(now);
}

std::chrono::steady_clock::time_point VisionService::next_poll_due_for_test(
    std::chrono::steady_clock::time_point now) const {
    return next_poll_due(now);
}

bool VisionService::full_rate_requested(std::chrono::steady_clock::time_point now) const {
    return pipeline_contract::requests_detection(request_) || now < release_hold_until_;
}

bool VisionService::step(std::chrono::steady_clock::time_point now) {
    std::optional<LiveTraceRecord> trace;
    if (options_.live_trace) {
        trace.emplace();
        trace->poll_enter_ns = trace_now_ns();
    }
    auto request = pipeline_contract::VisionRequest::Idle;
    bool engine_aiming = false;
    pipeline_contract::UserAimIntent intent;
    ViewportRequest viewport;
    VisionDetectionPolicy policy;
    std::uint64_t aim_transition_sequence = 0;
    std::chrono::steady_clock::time_point aim_transition_requested_at{};
    double requested_fps = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request = request_;
        const bool full_rate = full_rate_requested(now);
        engine_aiming = full_rate || options_.keepwarm_when_idle;
        const double fps = full_rate ? options_.capture_fps : options_.idle_fps;
        requested_fps = fps;
        if (!engine_aiming || interval_for_fps(fps) == std::chrono::steady_clock::duration::max()) {
            return false;
        }
        if (!immediate_poll_requested_ && has_last_poll_ &&
            now - last_poll_at_ < interval_for_fps(fps)) {
            return false;
        }
        if (trace) trace->poll_due_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                (immediate_poll_requested_ || !has_last_poll_ ? now : last_poll_at_ + interval_for_fps(fps)).time_since_epoch()).count());
        immediate_poll_requested_ = false;
        last_poll_at_ = now;
        has_last_poll_ = true;
        intent = user_aim_intent_;
        viewport = viewport_request_;
        policy = policy_;
        aim_transition_sequence = aim_transition_sequence_;
        aim_transition_requested_at = aim_transition_requested_at_;
    }

    const auto dispatch_at = std::chrono::steady_clock::now();
    if (policy.revision != applied_policy_revision_) {
        poller_->set_detection_policy(policy);
        applied_policy_revision_ = policy.revision;
    }
    poller_->set_request(pipeline_contract::requests_detection(request) ? request :
        engine_aiming ? pipeline_contract::VisionRequest::DetectionOnly : pipeline_contract::VisionRequest::Idle);
    poller_->set_user_aim_intent(intent);
    poller_->set_viewport(viewport);
    const auto capture_at = std::chrono::steady_clock::now();
    vision_native::VisionResult result = poller_->poll_once();
    const auto result_at = std::chrono::steady_clock::now();
    if (trace) {
        trace_vision_fields(*trace, result);
        trace->dispatch_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(dispatch_at.time_since_epoch()).count());
        trace->poll_return_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(result_at.time_since_epoch()).count());
        trace->producer_updated = result.frame_updated;
        trace->request = static_cast<std::uint64_t>(request);
        trace->epoch = aim_transition_sequence;
        trace->policy_revision = applied_policy_revision_;
        trace->requested_fps = requested_fps;
    }

    VisionServiceSnapshot snapshot;
    snapshot.request = request;
    snapshot.engine_aiming = engine_aiming;
    snapshot.aim_transition_sequence = aim_transition_sequence;
    if (pipeline_contract::requests_detection(request) && aim_transition_requested_at != std::chrono::steady_clock::time_point{}) {
        auto elapsed = [aim_transition_requested_at](auto end) {
            return static_cast<float>(std::max(0.0, std::chrono::duration<double, std::milli>(
                end - aim_transition_requested_at).count()));
        };
        snapshot.aim_wakeup_to_dispatch_ms = elapsed(dispatch_at);
        snapshot.aim_wakeup_to_capture_ms = elapsed(capture_at);
        snapshot.aim_wakeup_to_result_ms = elapsed(result_at);
    }
    snapshot.requested_vision_fps = static_cast<float>(requested_fps);
    snapshot.policy_revision = applied_policy_revision_;
    snapshot.result = std::move(result);

    auto published = std::make_shared<VisionServiceSnapshot>(std::move(snapshot));
    auto& final_snapshot = *published;
    std::shared_ptr<const VisionServiceSnapshot> retired;
    if (trace) trace->publish_wait_ns = trace_now_ns();
    std::unique_lock<std::mutex> lock(mutex_);
    if (trace) trace->publish_lock_ns = trace_now_ns();
    const bool control_epoch_current =
        request == request_ &&
        aim_transition_sequence == aim_transition_sequence_;
    if (!control_epoch_current) {
        final_snapshot.result.frame_updated = false;
    }
    final_snapshot.sequence = ++sequence_;
    final_snapshot.freshness = final_snapshot.result.frame_updated
        ? VisionSnapshotFreshness::Fresh : VisionSnapshotFreshness::NoUpdate;
    if (!final_snapshot.result.frame_updated || request != pipeline_contract::VisionRequest::AssistSearch) {
        clear_reused_authority(final_snapshot.result);
    }
    stamp_service_metadata(final_snapshot);
    final_snapshot.published_at_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    retired = std::move(latest_snapshot_);
    latest_snapshot_ = std::move(published);
    if (trace) {
        trace->sequence = final_snapshot.sequence;
        trace->epoch_current = control_epoch_current;
        trace->status = !control_epoch_current ? 3 : trace->producer_updated ? 1 : 2;
        trace->published_ns = final_snapshot.published_at_ns;
    }
    lock.unlock(); // Release any old detection allocation outside the mutex.
    if (trace) {
        trace->publish_unlock_ns = trace_now_ns();
        options_.live_trace->vision(*trace, final_snapshot.result.detections);
    }
    return true;
}

std::chrono::steady_clock::time_point VisionService::next_poll_due(
    std::chrono::steady_clock::time_point now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool full_rate = full_rate_requested(now);
    const bool engine_aiming = full_rate || options_.keepwarm_when_idle;
    const double fps = full_rate ? options_.capture_fps : options_.idle_fps;
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
    try {
        while (running_.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (step(now)) {
                continue;
            }
            const auto due = next_poll_due(now);
            wake_deadline_.wait_until(due);
        }
    } catch (...) {
        // This is the thread boundary: revoke the mailbox and transfer the
        // original error to RuntimeLoop for neutral output and orderly exit.
        std::lock_guard<std::mutex> lock(mutex_);
        worker_failure_ = std::current_exception();
        latest_snapshot_ = {};
        running_.store(false);
    }
}

} // namespace runtime_app
