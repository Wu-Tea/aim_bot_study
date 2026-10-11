#pragma once
#include "detection_haptics_policy.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace runtime_app {
// Dedicated WASAPI owner. The input owner publishes bindings; the controller
// publishes only a scalar event. Neither opens audio devices nor waits for them.
class DseHaptics {
public:
    DseHaptics();
    ~DseHaptics();
    DseHaptics(const DseHaptics&) = delete;
    DseHaptics& operator=(const DseHaptics&) = delete;
    void configure(bool enabled, float strength) noexcept;
    void bind_device(const std::string& physical_hid_path);
    void notify(HapticDirection direction) noexcept;
    bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }
    void stop() noexcept;
private:
    void run() noexcept;
    std::atomic<unsigned> settings_{0};
    std::atomic<std::uint64_t> pending_{0};
    std::atomic<bool> stopping_{false}, ready_{false};
    std::shared_ptr<const std::string> device_path_;
    std::string input_owner_path_;
    void* wake_ = nullptr;
    std::thread worker_;
};
} // namespace runtime_app
