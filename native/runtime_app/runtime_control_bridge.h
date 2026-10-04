#pragma once

#include "runtime_reload_policy.h"
#include "controller_native/aim_response_estimator.h"
#include <algorithm>
#include <array>
#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace runtime_app {

struct RuntimeLearningRegion { float effective = 500, learned = 500, confidence = 0; std::uint32_t samples = 0; };
struct RuntimeControlSnapshot {
    std::uint32_t pid = 0, status = 0; // 0 idle, 1 pending, 2 applied, 3 restart required, 4 rejected
    std::uint64_t created = 0, request_id = 0, completed_id = 0, revision = 0, sampled_at_ms = 0;
    RuntimeLearningRegion regions[4]{};
    char fire_output[8]{}, manual_fire_input[8]{}, message[512]{};
};
struct RuntimeControlMemory {
    volatile LONG sequence = 0;
    std::uint32_t protocol = 1;
    volatile LONG64 requested_id = 0;
    RuntimeControlSnapshot snapshot;
};
static_assert(offsetof(RuntimeControlMemory, snapshot) == 16);
static_assert(sizeof(RuntimeControlSnapshot) == 640);

// OS/file work is owned by this background thread. The tick only consumes a
// prepared immutable candidate and offers fixed-size learning snapshots.
class RuntimeControlBridge {
public:
    RuntimeControlBridge(controller_native::RuntimeConfig initial,
                         std::function<controller_native::RuntimeConfig()> loader)
        : current_(std::make_shared<controller_native::RuntimeConfig>(std::move(initial))), loader_(std::move(loader)) {
        const auto suffix = std::to_wstring(GetCurrentProcessId());
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(RuntimeControlMemory),
            (L"Local\\cod_native_control_" + suffix).c_str()));
        request_.reset(CreateEventW(nullptr, FALSE, FALSE, (L"Local\\cod_native_reload_" + suffix).c_str()));
        finished_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        exit_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (mapping_) memory_.reset(static_cast<RuntimeControlMemory*>(MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RuntimeControlMemory))));
        if (!memory_ || !request_ || !finished_ || !exit_) throw std::runtime_error("cannot open runtime control channel");
        *memory_ = RuntimeControlMemory{};
        state_.pid = GetCurrentProcessId();
        FILETIME created, end, kernel, user;
        GetProcessTimes(GetCurrentProcess(), &created, &end, &kernel, &user);
        state_.created = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        write_bindings(*current_);
        publish();
        worker_ = std::thread([this] { work(); });
    }
    // Join before RAII releases the mapping and events used by the worker.
    ~RuntimeControlBridge() { SetEvent(exit_.get()); if (worker_.joinable()) worker_.join(); }
    RuntimeControlBridge(const RuntimeControlBridge&) = delete;
    RuntimeControlBridge& operator=(const RuntimeControlBridge&) = delete;

    std::shared_ptr<const controller_native::RuntimeConfig> take_prepared() {
        if (!prepared_.load(std::memory_order_acquire)) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        prepared_.store(false, std::memory_order_release);
        return candidate_;
    }
    void complete(const std::array<controller_native::AimResponseEstimate, 4>& values = {}, bool preserved = false) noexcept {
        for (int i = 0; i < 4; ++i) cleared_learning_[i] = {values[i].scale_px_per_stick_second,
            values[i].learned_scale, values[i].confidence, values[i].accepted_samples};
        learning_preserved_ = preserved;
        completed_.store(true, std::memory_order_release);
        SetEvent(finished_.get());
    }
    void offer_learning(const std::array<controller_native::AimResponseEstimate, 4>& values) noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return;
        for (int i = 0; i < 4; ++i) learning_[i] = {values[i].scale_px_per_stick_second,
            values[i].learned_scale, values[i].confidence, values[i].accepted_samples};
        learning_at_ = GetTickCount64();
    }
private:
    void write_bindings(const controller_native::RuntimeConfig& config) {
        strncpy_s(state_.fire_output, config.gamepad.auto_fire.fire_output.c_str(), _TRUNCATE);
        strncpy_s(state_.manual_fire_input, config.gamepad.auto_fire.manual_fire_input.c_str(), _TRUNCATE);
    }
    void publish() noexcept {
        { std::lock_guard<std::mutex> lock(mutex_); std::copy(std::begin(learning_), std::end(learning_), state_.regions); state_.sampled_at_ms = learning_at_; }
        InterlockedIncrement(&memory_->sequence);
        memory_->snapshot = state_;
        MemoryBarrier();
        InterlockedIncrement(&memory_->sequence);
    }
    void work() noexcept {
        HANDLE events[] = {exit_.get(), request_.get(), finished_.get()};
        while (true) {
            const auto result = WaitForMultipleObjects(3, events, FALSE, 500);
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) return;
            if (completed_.exchange(false, std::memory_order_acq_rel)) {
                { std::lock_guard<std::mutex> lock(mutex_); std::copy(std::begin(cleared_learning_), std::end(cleared_learning_), learning_); learning_at_ = GetTickCount64(); }
                current_ = candidate_;
                candidate_.reset();
                ++state_.revision;
                state_.status = 2;
                state_.completed_id = state_.request_id;
                strcpy_s(state_.message, learning_preserved_ ? "applied; response learning preserved" : "applied; response learning cleared");
                write_bindings(*current_);
                const auto& value = *current_;
                std::cout << "[RuntimeControl] applied revision=" << state_.revision << " learning_cleared=" << !learning_preserved_ << " game=" << value.game
                    << " hipfire_ai_multiplier=" << value.gamepad.ai_aim.hipfire_multiplier
                    << " response_learning_enabled=" << value.gamepad.ai_aim.aim_response_learning_enabled
                    << " ads_x=" << value.ads.strength_scale << " ads_y=" << value.ads.vertical_strength_scale
                    << " body_x=" << value.gamepad.ai_aim.body_lock_max_ai_force << " body_y=" << value.gamepad.ai_aim.body_lock_max_ai_force_y
                    << " recoil_enabled=" << value.gamepad.recoil.enabled << " recoil_amount=" << value.gamepad.recoil.feedback_amount
                    << " hipfire_multiplier=" << value.gamepad.recoil.hipfire_multiplier
                    << " friendly_filter=" << value.vision.friendly_filter_enabled << " target_ratio=" << value.vision.target_height_ratio
                    << " fire_output=" << value.gamepad.auto_fire.fire_output << " fire_input=" << value.gamepad.auto_fire.manual_fire_input
                    << '\n' << std::flush;
            }
            if (result == WAIT_OBJECT_0 + 1 && state_.status != 1) {
                state_.request_id = static_cast<std::uint64_t>(InterlockedCompareExchange64(&memory_->requested_id, 0, 0));
                try {
                    auto next = std::make_shared<controller_native::RuntimeConfig>(loader_());
                    const auto restart = hot_reload_restrictions(*current_, *next);
                    if (!restart.empty()) {
                        state_.status = 3; state_.completed_id = state_.request_id;
                        strncpy_s(state_.message, ("restart required: " + restart).c_str(), _TRUNCATE);
                    } else {
                        { std::lock_guard<std::mutex> lock(mutex_); candidate_ = std::move(next); }
                        state_.status = 1;
                        strcpy_s(state_.message, "waiting for tick / fresh vision policy boundary");
                        prepared_.store(true, std::memory_order_release);
                    }
                } catch (const std::exception& error) {
                    state_.status = 4; state_.completed_id = state_.request_id;
                    strncpy_s(state_.message, error.what(), _TRUNCATE);
                }
            }
            publish();
        }
    }
    struct HandleCloser {
        void operator()(void* handle) const noexcept { CloseHandle(handle); }
    };
    struct ViewUnmapper {
        void operator()(RuntimeControlMemory* view) const noexcept { UnmapViewOfFile(view); }
    };
    // unique_ptr also releases partially constructed channels if allocation or
    // thread creation throws. Declare the view after handles so it unmaps first.
    std::unique_ptr<void, HandleCloser> mapping_, request_, finished_, exit_;
    std::unique_ptr<RuntimeControlMemory, ViewUnmapper> memory_;
    RuntimeControlSnapshot state_{};
    RuntimeLearningRegion learning_[4]{};
    RuntimeLearningRegion cleared_learning_[4]{};
    bool learning_preserved_ = false;
    std::uint64_t learning_at_ = 0;
    std::shared_ptr<const controller_native::RuntimeConfig> current_, candidate_;
    std::function<controller_native::RuntimeConfig()> loader_;
    std::mutex mutex_;
    std::atomic<bool> prepared_{false}, completed_{false};
    std::thread worker_;
};
} // namespace runtime_app
