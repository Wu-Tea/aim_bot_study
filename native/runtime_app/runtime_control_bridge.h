#pragma once

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

struct RuntimeLearningRegion { float effective = 500, learned = 500, confidence = 0; std::uint32_t samples = 0; };
enum RuntimeControlStatus : std::uint32_t {
    Idle = 0, Pending = 1, Applied = 2, RestartRequired = 3, Rejected = 4
};

struct RuntimeControlSnapshot {
    std::uint32_t pid = 0;
    RuntimeControlStatus status = Idle;
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
