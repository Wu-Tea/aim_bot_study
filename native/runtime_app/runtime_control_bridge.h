#pragma once

#include "runtime_control_protocol.h"
#include "controller_native/runtime_config.h"
#include "controller_native/aim_response_estimator.h"
#include <array>
#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace runtime_app {

// OS/file work is owned by this background thread. The tick only consumes a
// prepared immutable candidate and offers fixed-size learning snapshots.
class RuntimeControlBridge {
public:
    RuntimeControlBridge(controller_native::RuntimeConfig initial,
                         std::function<controller_native::RuntimeConfig()> loader);
    // Join before RAII releases the mapping and events used by the worker.
    ~RuntimeControlBridge();
    RuntimeControlBridge(const RuntimeControlBridge&) = delete;
    RuntimeControlBridge& operator=(const RuntimeControlBridge&) = delete;

    std::shared_ptr<const controller_native::RuntimeConfig> take_prepared();
    void complete(const std::array<controller_native::AimResponseEstimate, 4>& values = {}, bool preserved = false) noexcept;
    void offer_learning(const std::array<controller_native::AimResponseEstimate, 4>& values) noexcept;
private:
    void write_bindings(const controller_native::RuntimeConfig& config);
    void publish() noexcept;
    void work() noexcept;
    void acknowledge_commit();
    void accept_reload_request();
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
