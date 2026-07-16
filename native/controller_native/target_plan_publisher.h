#pragma once

#include "pipeline_contract/target_plan.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

namespace controller_native {

class TargetPlanPublisher {
public:
    TargetPlanPublisher() noexcept = default;

    void publish(const pipeline_contract::TargetPlan& plan) noexcept {
        const std::size_t published = published_index_.load(std::memory_order_acquire);
        std::size_t candidate = (published + 1) % slots_.size();
        for (;;) {
            if (candidate != published &&
                reader_counts_[candidate].load(std::memory_order_acquire) == 0) {
                break;
            }
            candidate = (candidate + 1) % slots_.size();
            if (candidate == published) std::this_thread::yield();
        }
        slots_[candidate] = plan;
        published_index_.store(candidate, std::memory_order_release);
    }

    pipeline_contract::TargetPlan read() const noexcept {
        for (;;) {
            const std::size_t index = published_index_.load(std::memory_order_acquire);
            reader_counts_[index].fetch_add(1, std::memory_order_acq_rel);
            if (index != published_index_.load(std::memory_order_acquire)) {
                reader_counts_[index].fetch_sub(1, std::memory_order_release);
                continue;
            }
            const auto plan = slots_[index];
            reader_counts_[index].fetch_sub(1, std::memory_order_release);
            return plan;
        }
    }

private:
    std::array<pipeline_contract::TargetPlan, 3> slots_{};
    mutable std::array<std::atomic<unsigned int>, 3> reader_counts_{};
    std::atomic<std::size_t> published_index_{0};
};

}  // namespace controller_native
