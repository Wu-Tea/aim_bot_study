#include "target_plan_publisher.h"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_default_read_is_safe() {
    controller_native::TargetPlanPublisher publisher;
    const auto plan = publisher.read();
    require_true(plan.lifecycle == pipeline_contract::TargetLifecycle::None,
                 "publisher must default to no-target plan");
}

void test_publish_is_coherent_under_readers() {
    controller_native::TargetPlanPublisher publisher;
    std::atomic<bool> running{true};
    std::atomic<bool> torn{false};
    std::vector<std::thread> readers;
    for (int reader = 0; reader < 4; ++reader) {
        readers.emplace_back([&] {
            while (running.load(std::memory_order_relaxed)) {
                const auto plan = publisher.read();
                if (plan.target_id != 0 &&
                    (plan.error_px.x != static_cast<float>(plan.target_id) ||
                     plan.error_px.y != -static_cast<float>(plan.target_id))) {
                    torn.store(true, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::uint64_t generation = 1; generation <= 50000; ++generation) {
        pipeline_contract::TargetPlan plan{};
        plan.generation = generation;
        plan.target_id = generation;
        plan.error_px = {static_cast<float>(generation), -static_cast<float>(generation)};
        publisher.publish(plan);
    }
    running.store(false, std::memory_order_relaxed);
    for (auto& reader : readers) reader.join();
    require_true(!torn.load(), "readers must never observe a torn plan");
    require_true(publisher.read().generation == 50000,
                 "publisher must expose latest generation");
}

}  // namespace

int main() {
    try {
        test_default_read_is_safe();
        test_publish_is_coherent_under_readers();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[TargetPlanPublisherTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
