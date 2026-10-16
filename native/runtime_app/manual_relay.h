#pragma once

#include "controller_native/control_frame.h"
#include "controller_native/output_composer.h"
#include "runtime_timing.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace runtime_app {

// One owner for physical reads and device writes. The algorithm may wait for
// a delivery receipt; this lane never waits for the algorithm. On a missed
// two-period delivery budget it sends fresh physical input, not cached AI.
class ManualRelay {
public:
    using Clock = std::chrono::steady_clock;
    struct Sample {
        controller_native::PhysicalGamepadState physical{};
        std::uint64_t sequence = 0;
        std::uint64_t scope_revision = 0;
        Clock::time_point read_at{};
    };
    struct Receipt {
        controller_native::VirtualGamepadUpdateResult output{};
        bool continuity_broken = false;
        std::uint64_t dequeued_ns = 0, device_begin_ns = 0, device_end_ns = 0;
        bool request_accepted = false, input_current = false, visual_fresh = false;
    };
    using Reader = std::function<controller_native::PhysicalGamepadState()>;
    using Writer = std::function<controller_native::VirtualGamepadUpdateResult(
        const controller_native::GamepadOutputState&)>;

    ManualRelay(Reader read, Writer write, int hz,
                controller_native::GameStickTransferConfig transfer,
                std::function<void()> close = {}, bool trace_timing = false)
        : read_(std::move(read)), write_(std::move(write)), transfer_(transfer),
          period_(std::chrono::nanoseconds(1'000'000'000ll / std::max(1, hz))),
          close_(std::move(close)), trace_timing_(trace_timing) {}
    ~ManualRelay() { stop(); }
    void start() {
        worker_ = std::thread([this] { run(); });
        std::unique_lock<std::mutex> lock(mutex_);
        finished_.wait(lock, [&] { return sample_.sequence != 0 || failure_; });
        if (failure_) std::rethrow_exception(failure_);
    }
    void stop() noexcept {
        stop_.store(true);
        wake_.notify();
        finished_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    Sample latest() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_) std::rethrow_exception(failure_);
        return sample_;
    }
    Sample wait_next(std::uint64_t after_sequence) {
        std::unique_lock<std::mutex> lock(mutex_);
        finished_.wait(lock, [&] { return sample_.sequence > after_sequence || failure_ || stop_; });
        if (failure_) std::rethrow_exception(failure_);
        return sample_;
    }
    std::uint64_t discontinuity_sequence() const noexcept {
        return discontinuity_sequence_.load(std::memory_order_acquire);
    }
    Receipt submit(const controller_native::GamepadOutputState& output,
                   const Sample& sample, double visual_deadline_seconds,
                   controller_native::GameStickTransferConfig transfer) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (failure_) std::rethrow_exception(failure_);
        if (stop_) return {};
        const auto id = ++request_id_;
        pending_ = Pending{output, sample.sequence, id, visual_deadline_seconds, transfer};
        lock.unlock();
        wake_.notify();
        lock.lock();
        finished_.wait(lock, [&] { return completed_id_ == id || failure_ || stop_; });
        if (failure_) std::rethrow_exception(failure_);
        return completed_id_ == id ? receipt_ : Receipt{};
    }
private:
    struct Pending {
        controller_native::GamepadOutputState output;
        std::uint64_t sample_sequence, id;
        double visual_deadline_seconds;
        controller_native::GameStickTransferConfig transfer;
    };
    void send_manual(const Sample& sample) {
        controller_native::OutputComposer composer(transfer_);
        const auto physical = sample.physical.connected
            ? sample.physical : controller_native::PhysicalGamepadState{};
        const auto frame = controller_native::ControlFrame::begin(physical,
            pipeline_contract::ControllerTickId::from(sample.sequence),
            pipeline_contract::EventSequence::from(sample.sequence));
        if (composer.compose(frame) != controller_native::OutputComposeStatus::Ok)
            throw std::runtime_error("manual relay input is invalid");
        (void)write_(*composer.finalized_output());
        continuity_broken_ = true;
        discontinuity_sequence_.fetch_add(1, std::memory_order_release);
    }
    void run() noexcept {
        (void)set_current_thread_priority(RuntimeThreadPriority::Highest);
        auto next_read = Clock::now();
        auto last_delivery = Clock::time_point{};
        double active_visual_deadline = 0;
        Sample current;
        try {
            while (!stop_.load()) {
                auto now = Clock::now();
                const bool visual_due = active_visual_deadline != 0 &&
                    std::chrono::duration<double>(now.time_since_epoch()).count() >= active_visual_deadline;
                bool read_due = now >= next_read || visual_due;
                bool manual_sent_this_read = false;
                if (read_due) {
                    const auto previous = current.physical;
                    current.physical = read_();
                    current.read_at = Clock::now();
                    ++current.sequence;
                    const bool scope_changed = previous.connected != current.physical.connected ||
                        previous.left_trigger != current.physical.left_trigger ||
                        previous.right_trigger != current.physical.right_trigger ||
                        previous.rb != current.physical.rb || previous.lb != current.physical.lb ||
                        previous.touchpad != current.physical.touchpad ||
                        previous.touchpad_fingers[0].active != current.physical.touchpad_fingers[0].active ||
                        previous.touchpad_fingers[1].active != current.physical.touchpad_fingers[1].active;
                    if (scope_changed) ++current.scope_revision;
                    if (next_read <= current.read_at) {
                        const auto elapsed_slots = (current.read_at - next_read) / period_ + 1;
                        next_read += period_ * elapsed_slots;
                    }
                    // Physical activation/release edges revoke cached digital
                    // actions immediately, even while the algorithm is stuck.
                    const bool delivery_overdue = last_delivery == Clock::time_point{} ||
                        current.read_at - last_delivery >= period_ * 2;
                    if (scope_changed || delivery_overdue || visual_due) {
                        send_manual(current);
                        manual_sent_this_read = true;
                        last_delivery = {};
                        active_visual_deadline = 0;
                    }
                    // Publish only after any fallback receipt boundary, so
                    // resumed learning cannot cross unobserved manual work.
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        sample_ = current;
                    }
                    finished_.notify_all();
                }
                std::optional<Pending> request;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    request = std::move(pending_);
                    pending_.reset();
                }
                now = Clock::now();
                const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
                if (request) {
                    transfer_ = request->transfer;
                    Receipt receipt;
                    if (trace_timing_) receipt.dequeued_ns = steady_time_point_ns(now);
                    receipt.continuity_broken = continuity_broken_;
                    const bool current_input = current.physical.connected && current.sequence != 0 &&
                        request->sample_sequence == current.sequence &&
                        now - current.read_at < period_ * 2;
                    const bool fresh_visual = request->visual_deadline_seconds == 0 ||
                        (std::isfinite(request->visual_deadline_seconds) &&
                         seconds < request->visual_deadline_seconds);
                    receipt.input_current = current_input;
                    receipt.visual_fresh = fresh_visual;
                    receipt.request_accepted = current_input && fresh_visual;
                    if (receipt.request_accepted) {
                        if (trace_timing_) receipt.device_begin_ns = steady_time_point_ns(Clock::now());
                        receipt.output = write_(request->output);
                        if (trace_timing_) receipt.device_end_ns = steady_time_point_ns(Clock::now());
                        last_delivery = Clock::now();
                        active_visual_deadline = request->visual_deadline_seconds;
                        continuity_broken_ = false;
                    } else {
                        // A computation for superseded input cannot restore
                        // an old button hold or an expired AI instruction.
                        send_manual(current);
                        active_visual_deadline = 0;
                        receipt.continuity_broken = true;
                    }
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        receipt_ = receipt;
                        completed_id_ = request->id;
                    }
                    finished_.notify_one();
                }
                if (!request && read_due && !manual_sent_this_read &&
                    (last_delivery == Clock::time_point{} || now - last_delivery >= period_ * 2 ||
                     (active_visual_deadline != 0 && seconds >= active_visual_deadline))) {
                    send_manual(current);
                    active_visual_deadline = 0;
                }
                auto due = next_read;
                if (active_visual_deadline != 0) {
                    const auto expiry = Clock::time_point(std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(active_visual_deadline)));
                    due = std::min(due, expiry);
                }
                wake_.wait_until(due);
            }
        } catch (...) {
            const auto failure = std::current_exception();
            try { (void)write_({}); } catch (...) {}
            std::lock_guard<std::mutex> lock(mutex_);
            failure_ = failure;
        }
        if (close_) {
            try { close_(); }
            catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!failure_) failure_ = std::current_exception();
            }
        }
        finished_.notify_all();
    }
    Reader read_;
    Writer write_;
    controller_native::GameStickTransferConfig transfer_;
    Clock::duration period_;
    std::function<void()> close_;
    bool trace_timing_ = false;
    std::mutex mutex_;
    std::condition_variable finished_;
    Sample sample_;
    std::optional<Pending> pending_;
    Receipt receipt_;
    std::uint64_t request_id_ = 0, completed_id_ = 0;
    bool continuity_broken_ = false;
    std::exception_ptr failure_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> discontinuity_sequence_{0};
    InterruptibleDeadlineWait wake_;
    std::thread worker_;
};

} // namespace runtime_app
