#include "vision_service.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

#define REQUIRE(condition) require((condition), __LINE__)

void require(bool condition, int line) {
    if (!condition) {
        std::cerr << "require failed at line " << line << std::endl;
        std::abort();
    }
}

class FakeVisionPoller final : public runtime_app::IVisionServicePoller {
public:
    explicit FakeVisionPoller(std::vector<bool> updates)
        : updates_(std::move(updates)) {}

    void set_aiming(bool aiming) override {
        aiming_history.push_back(aiming);
    }

    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) override {
        last_intent = intent;
    }

    void set_viewport(const runtime_app::ViewportRequest& request) override {
        last_viewport = request;
        ++viewport_update_count;
    }

    vision_native::VisionResult poll_once() override {
        ++poll_count;
        vision_native::VisionResult result;
        result.frame_id = static_cast<std::uint64_t>(poll_count);
        result.result_at_ns = static_cast<std::uint64_t>(poll_count) * 1'000'000;
        result.frame_updated = next_update();
        if (result.frame_updated && authority_on_update) {
            result.has_target = true;
            result.aim_authority = true;
            result.fire_authority = true;
            result.auto_fire = true;
        }
        result.gpu_total_ms = result.frame_updated ? 6.5f : 0.0f;
        return result;
    }

    int poll_count = 0;
    bool authority_on_update = false;
    std::vector<bool> aiming_history;
    pipeline_contract::UserAimIntent last_intent;
    runtime_app::ViewportRequest last_viewport;
    int viewport_update_count = 0;

private:
    bool next_update() {
        if (updates_.empty()) {
            return true;
        }
        const bool updated = updates_.front();
        updates_.erase(updates_.begin());
        return updated;
    }

    std::vector<bool> updates_;
};

std::chrono::steady_clock::time_point at_ms(int ms) {
    return std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(ms);
}

void test_keepwarm_polls_while_idle_and_active() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, true, true});
    FakeVisionPoller* raw = poller.get();
    runtime_app::VisionServiceOptions options;
    options.active_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(0)));
    REQUIRE(!service.step_for_test(at_ms(10)));
    REQUIRE(service.step_for_test(at_ms(50)));

    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(60)));
    REQUIRE(raw->poll_count == 3);
    REQUIRE(raw->aiming_history[0]);
    REQUIRE(raw->aiming_history[1]);
    REQUIRE(raw->aiming_history[2]);
    const auto snapshot = service.latest_snapshot();
    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::Fresh);
    REQUIRE(snapshot.published_at_ns != 0);
    REQUIRE(snapshot.published_at_ns >= snapshot.result.result_at_ns);
    REQUIRE(std::string(snapshot.result.service_freshness) == "fresh");
    REQUIRE(std::string(snapshot.result.service_source_state) == "fresh_frame");
}

void test_no_update_reuses_last_snapshot_without_marking_fresh() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, false});
    runtime_app::VisionServiceOptions options;
    options.active_fps = 100.0;
    options.repeat_last_on_no_update = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(0)));
    const runtime_app::VisionServiceSnapshot first = service.latest_snapshot();
    REQUIRE(first.freshness == runtime_app::VisionSnapshotFreshness::Fresh);
    REQUIRE(first.published_at_ns != 0);

    REQUIRE(service.step_for_test(at_ms(10)));
    const runtime_app::VisionServiceSnapshot reused = service.latest_snapshot();
    REQUIRE(reused.freshness == runtime_app::VisionSnapshotFreshness::Reused);
    REQUIRE(reused.source_state == runtime_app::VisionSourceState::RepeatLastFrame);
    REQUIRE(std::string(reused.result.service_freshness) == "reused");
    REQUIRE(std::string(reused.result.service_source_state) == "repeat_last_frame");
    REQUIRE(reused.result.frame_id == first.result.frame_id);
    REQUIRE(!reused.result.frame_updated);
    REQUIRE(reused.result.aim_authority == false);
    REQUIRE(reused.result.fire_authority == false);
    REQUIRE(reused.published_at_ns >= first.published_at_ns);
}

void test_aim_release_clears_active_repeat_cache() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, false});
    runtime_app::VisionServiceOptions options;
    options.active_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;
    options.repeat_last_on_no_update = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(50)));

    const auto snapshot = service.latest_snapshot();
    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::NoUpdate);
    REQUIRE(snapshot.source_state == runtime_app::VisionSourceState::NoUpdate);
    REQUIRE(!snapshot.result.frame_updated);
}

void test_delivery_gate_accepts_each_recent_capture_once() {
    runtime_app::VisionDeliveryGate gate(50.0f);
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 10;
    result.captured_at_ns = 1'000'000'000ull;
    result.result_at_ns = 1'006'000'000ull;

    REQUIRE(gate.accept(result, 1'010'000'000ull));
    REQUIRE(!gate.accept(result, 1'011'000'000ull));

    auto backward = result;
    backward.frame_id = 9;
    backward.captured_at_ns = 1'020'000'000ull;
    backward.result_at_ns = 1'026'000'000ull;
    REQUIRE(!gate.accept(backward, 1'030'000'000ull));

    auto backward_time = result;
    backward_time.frame_id = 11;
    backward_time.captured_at_ns = 999'000'000ull;
    backward_time.result_at_ns = 1'005'000'000ull;
    REQUIRE(!gate.accept(backward_time, 1'010'000'000ull));

    auto stale = result;
    stale.frame_id = 11;
    stale.captured_at_ns = 1'020'000'000ull;
    stale.result_at_ns = 1'026'000'000ull;
    REQUIRE(!gate.accept(stale, 1'071'000'000ull));

    auto current = result;
    current.frame_id = 11;
    current.captured_at_ns = 1'030'000'000ull;
    current.result_at_ns = 1'036'000'000ull;
    REQUIRE(gate.accept(current, 1'040'000'000ull));
}

void test_viewport_request_is_forwarded_before_poll() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true});
    FakeVisionPoller* raw = poller.get();
    runtime_app::VisionServiceOptions options;
    runtime_app::VisionService service(std::move(poller), options);
    runtime_app::ViewportRequest request;
    request.level = runtime_app::ViewportLevel::Rescue;
    request.width = 600;
    request.height = 520;
    request.sequence = 3;
    request.source_frame_id = 42;
    service.set_viewport(request);
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(0)));
    REQUIRE(raw->viewport_update_count == 1);
    REQUIRE(raw->last_viewport.level == runtime_app::ViewportLevel::Rescue);
    REQUIRE(raw->last_viewport.width == 600);
    REQUIRE(raw->last_viewport.source_frame_id == 42);
}

void test_no_keepwarm_does_not_poll_idle() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true});
    FakeVisionPoller* raw = poller.get();
    runtime_app::VisionServiceOptions options;
    options.active_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = false;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(false);
    REQUIRE(!service.step_for_test(at_ms(0)));
    REQUIRE(raw->poll_count == 0);
}

void test_idle_keepwarm_does_not_publish_control_authority() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true});
    poller->authority_on_update = true;
    runtime_app::VisionServiceOptions options;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(0)));
    const runtime_app::VisionServiceSnapshot snapshot = service.latest_snapshot();

    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::Fresh);
    REQUIRE(!snapshot.controller_aiming);
    REQUIRE(snapshot.result.has_target);
    REQUIRE(!snapshot.result.aim_authority);
    REQUIRE(!snapshot.result.fire_authority);
    REQUIRE(!snapshot.result.auto_fire);
}

void test_idle_keepwarm_frame_does_not_seed_active_repeat_last() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, false});
    runtime_app::VisionServiceOptions options;
    options.active_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;
    options.repeat_last_on_no_update = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(0)));

    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(10)));
    const runtime_app::VisionServiceSnapshot snapshot = service.latest_snapshot();

    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::NoUpdate);
    REQUIRE(snapshot.source_state == runtime_app::VisionSourceState::NoUpdate);
}

void test_next_poll_due_uses_active_fps_interval() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true});
    runtime_app::VisionServiceOptions options;
    options.active_fps = 120.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(0)));

    const auto due = service.next_poll_due_for_test(at_ms(1));

    REQUIRE(due > at_ms(8));
    REQUIRE(due < at_ms(9));
}

void test_aim_transition_bypasses_idle_deadline() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, true});
    runtime_app::VisionServiceOptions options;
    options.active_fps = 160.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;
    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(1)));
    const auto snapshot = service.latest_snapshot();
    REQUIRE(snapshot.controller_aiming);
    REQUIRE(snapshot.aim_transition_sequence == 1);
    REQUIRE(snapshot.result.service_sequence == 2);
    REQUIRE(snapshot.result.requested_vision_fps == 160.0f);
    REQUIRE(snapshot.aim_wakeup_to_capture_ms >= snapshot.aim_wakeup_to_dispatch_ms);
    REQUIRE(snapshot.aim_wakeup_to_result_ms >= snapshot.aim_wakeup_to_capture_ms);
}

void test_one_hundred_aim_transitions_never_publish_pre_aim_authority() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    poller->authority_on_update = true;
    runtime_app::VisionServiceOptions options;
    options.active_fps = 160.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;
    runtime_app::VisionService service(std::move(poller), options);
    for (int transition = 0; transition < 100; ++transition) {
        const int base = transition * 100;
        service.set_aiming(false);
        REQUIRE(service.step_for_test(at_ms(base)));
        const auto idle = service.latest_snapshot();
        REQUIRE(!idle.result.aim_authority);
        REQUIRE(!idle.result.fire_authority);
        service.set_aiming(true);
        REQUIRE(service.step_for_test(at_ms(base + 1)));
        const auto active = service.latest_snapshot();
        REQUIRE(active.aim_transition_sequence == static_cast<std::uint64_t>(transition + 1));
        REQUIRE(active.result.aim_authority);
        REQUIRE(active.result.fire_authority);
    }
}

} // namespace

int main() {
    test_keepwarm_polls_while_idle_and_active();
    test_no_update_reuses_last_snapshot_without_marking_fresh();
    test_aim_release_clears_active_repeat_cache();
    test_delivery_gate_accepts_each_recent_capture_once();
    test_viewport_request_is_forwarded_before_poll();
    test_no_keepwarm_does_not_poll_idle();
    test_idle_keepwarm_does_not_publish_control_authority();
    test_idle_keepwarm_frame_does_not_seed_active_repeat_last();
    test_next_poll_due_uses_active_fps_interval();
    test_aim_transition_bypasses_idle_deadline();
    test_one_hundred_aim_transitions_never_publish_pre_aim_authority();
    return 0;
}
