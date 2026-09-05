#include "vision_service.h"
#include "vision_controller_adapter.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>

namespace {

#define REQUIRE(condition) require((condition), __LINE__)

void require(bool condition, int line) {
    if (!condition) throw std::runtime_error(
        "vision service assertion failed at line " + std::to_string(line));
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
    options.capture_fps = 100.0;
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

void test_no_update_does_not_replay_last_snapshot() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, false});
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(0)));
    const runtime_app::VisionServiceSnapshot first = service.latest_snapshot();
    REQUIRE(first.freshness == runtime_app::VisionSnapshotFreshness::Fresh);
    REQUIRE(first.published_at_ns != 0);

    REQUIRE(service.step_for_test(at_ms(10)));
    const runtime_app::VisionServiceSnapshot no_update = service.latest_snapshot();
    REQUIRE(no_update.freshness == runtime_app::VisionSnapshotFreshness::NoUpdate);
    REQUIRE(no_update.source_state == runtime_app::VisionSourceState::NoUpdate);
    REQUIRE(std::string(no_update.result.service_freshness) == "no_update");
    REQUIRE(std::string(no_update.result.service_source_state) == "no_update");
    REQUIRE(no_update.result.frame_id != first.result.frame_id);
    REQUIRE(!no_update.result.frame_updated);
    REQUIRE(no_update.result.aim_authority == false);
    REQUIRE(no_update.result.fire_authority == false);
    REQUIRE(no_update.published_at_ns >= first.published_at_ns);
}

void test_aim_release_no_update_has_no_authority() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, false});
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(50)));

    const auto snapshot = service.latest_snapshot();
    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::NoUpdate);
    REQUIRE(snapshot.source_state == runtime_app::VisionSourceState::NoUpdate);
    REQUIRE(!snapshot.result.frame_updated);
    REQUIRE(!snapshot.result.aim_authority);
    REQUIRE(!snapshot.result.fire_authority);
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
    options.capture_fps = 100.0;
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

void test_idle_keepwarm_frame_is_not_replayed_after_aim_transition() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, false});
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_aiming(false);
    REQUIRE(service.step_for_test(at_ms(0)));

    service.set_aiming(true);
    REQUIRE(service.step_for_test(at_ms(10)));
    const runtime_app::VisionServiceSnapshot snapshot = service.latest_snapshot();

    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::NoUpdate);
    REQUIRE(snapshot.source_state == runtime_app::VisionSourceState::NoUpdate);
}

void test_next_poll_due_uses_capture_fps_interval() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true});
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 120.0;
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
    options.capture_fps = 160.0;
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
    options.capture_fps = 160.0;
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

namespace {

class ThrowingVisionPoller final : public runtime_app::IVisionServicePoller {
public:
    std::atomic<bool> fail{true};
    void set_aiming(bool) override {}
    void set_user_aim_intent(const pipeline_contract::UserAimIntent&) override {}
    vision_native::VisionResult poll_once() override {
        if (fail.load()) throw std::runtime_error("injected vision worker failure");
        vision_native::VisionResult result;
        result.frame_id = ++frame_id_;
        result.frame_updated = true;
        result.aim_authority = true;
        result.fire_authority = true;
        result.auto_fire = true;
        return result;
    }
private:
    std::uint64_t frame_id_ = 0;
};

void test_worker_failure_is_transferred_and_stop_joins() {
    runtime_app::VisionService service(std::make_unique<ThrowingVisionPoller>(), {});
    service.start();
    bool received = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        try {
            (void)service.latest_snapshot();
        } catch (const std::runtime_error& error) {
            received = std::string(error.what()) == "injected vision worker failure";
        }
        std::this_thread::yield();
    }
    service.stop();
    service.stop();
    REQUIRE(received);
    bool restart_rejected = false;
    try { service.start(); }
    catch (const std::runtime_error&) { restart_rejected = true; }
    REQUIRE(restart_rejected);
}

void test_mailbox_does_not_copy_an_already_consumed_sequence() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, true});
    runtime_app::VisionService service(std::move(poller), {});
    REQUIRE(service.step_for_test(at_ms(0)));
    const auto initial = service.latest_snapshot();
    REQUIRE(initial.sequence != 0);
    const auto unchanged = service.latest_snapshot(initial.sequence);
    REQUIRE(unchanged.sequence == 0);
    REQUIRE(unchanged.result.detections.empty());
    REQUIRE(service.step_for_test(at_ms(50)));
    REQUIRE(service.latest_snapshot(initial.sequence).sequence > initial.sequence);
}

void test_worker_failure_revokes_a_previously_authoritative_mailbox() {
    auto poller = std::make_unique<ThrowingVisionPoller>();
    auto* raw = poller.get();
    raw->fail.store(false);
    runtime_app::VisionService service(std::move(poller), {});
    service.set_aiming(true);
    service.start();
    runtime_app::VisionServiceSnapshot published;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (published.sequence == 0 && std::chrono::steady_clock::now() < deadline) {
        published = service.latest_snapshot();
        std::this_thread::yield();
    }
    raw->fail.store(true);
    bool revoked = false;
    while (!revoked && std::chrono::steady_clock::now() < deadline) {
        try { (void)service.latest_snapshot(published.sequence); }
        catch (const std::runtime_error&) { revoked = true; }
        std::this_thread::yield();
    }
    service.stop();
    REQUIRE(published.result.aim_authority && published.result.fire_authority);
    REQUIRE(revoked);
}

void test_delivery_gate_uses_source_image_age_and_identity() {
    runtime_app::VisionDeliveryGate gate(50.0f);
    vision_native::VisionResult result;
    result.frame_updated = true;
    result.frame_id = 1;
    result.captured_at_ns = 995'000'000;
    result.result_at_ns = 999'000'000;
    result.source_present_available = true;
    result.source_present_qpc = 800'000;
    result.source_present_qpc_frequency = 1'000'000;
    result.source_present_steady_available = true;
    result.source_present_steady_ns = 800'000'000;
    result.accumulated_frames = 1;
    REQUIRE(!gate.accept(result, 1'000'000'000));
    result.source_present_steady_ns = 990'000'000;
    result.source_present_qpc = 990'000;
    REQUIRE(gate.accept(result, 1'000'000'000));
    ++result.frame_id;
    result.captured_at_ns += 1'000'000;
    REQUIRE(!gate.accept(result, 1'000'000'000));
    result.source_present_steady_ns += 1'000'000;
    // Calibration jitter must not turn the same QPC image into a new one.
    REQUIRE(!gate.accept(result, 1'000'000'000));
    ++result.source_present_qpc;
    REQUIRE(gate.accept(result, 1'000'000'000));
    const auto snapshot = runtime_app::adapt_vision_result(result);
    REQUIRE(std::fabs(snapshot.capture_time_seconds - 0.991) < 1e-9);
    REQUIRE(result.captured_at_ns == 996'000'000); // Copy telemetry is preserved.
    ++result.frame_id;
    ++result.captured_at_ns;
    result.source_present_steady_ns = 1'010'000'000;
    REQUIRE(!gate.accept(result, 1'000'000'000));
    result.source_present_steady_available = false;
    REQUIRE(!gate.accept(result, 1'000'000'000));

    gate.reset();
    result.source_present_steady_available = true;
    result.source_present_steady_ns = 951'000'000;
    result.source_present_calibration_uncertainty_ns = 2'000'000;
    REQUIRE(!gate.accept(result, 1'000'000'000));
    result.source_present_calibration_uncertainty_ns = 500'000;
    REQUIRE(gate.accept(result, 1'000'000'000));
}

}  // namespace

void register_vision_service_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "worker_failure_transferred_and_joined", test_worker_failure_is_transferred_and_stop_joins);
    registry.add_case("BaseRuntimeFreshness", "worker_failure_revokes_authority", test_worker_failure_revokes_a_previously_authoritative_mailbox);
    registry.add_case("BaseRuntimeFreshness", "mailbox_skips_consumed_sequence", test_mailbox_does_not_copy_an_already_consumed_sequence);
    registry.add_case("BaseRuntimeFreshness", "delivery_uses_source_image_age_and_identity", test_delivery_gate_uses_source_image_age_and_identity);
    registry.add_case("BaseRuntimeFreshness", "keepwarm_polls_idle_and_active", test_keepwarm_polls_while_idle_and_active);
    registry.add_case("BaseRuntimeFreshness", "no_update_does_not_replay_snapshot", test_no_update_does_not_replay_last_snapshot);
    registry.add_case("BaseRuntimeFreshness", "aim_release_no_update_has_no_authority", test_aim_release_no_update_has_no_authority);
    registry.add_case("BaseRuntimeFreshness", "delivery_gate_accepts_recent_capture_once", test_delivery_gate_accepts_each_recent_capture_once);
    registry.add_case("BaseRuntimeFreshness", "viewport_request_forwarded_before_poll", test_viewport_request_is_forwarded_before_poll);
    registry.add_case("BaseRuntimeFreshness", "disabled_keepwarm_does_not_poll_idle", test_no_keepwarm_does_not_poll_idle);
    registry.add_case("BaseRuntimeFreshness", "idle_keepwarm_has_no_control_authority", test_idle_keepwarm_does_not_publish_control_authority);
    registry.add_case("BaseRuntimeFreshness", "idle_frame_not_replayed_after_aim", test_idle_keepwarm_frame_is_not_replayed_after_aim_transition);
    registry.add_case("BaseRuntimeFreshness", "next_poll_due_uses_capture_interval", test_next_poll_due_uses_capture_fps_interval);
    registry.add_case("BaseRuntimeFreshness", "aim_transition_bypasses_idle_deadline", test_aim_transition_bypasses_idle_deadline);
    registry.add_case("BaseRuntimeFreshness", "aim_transitions_never_publish_pre_aim_authority", test_one_hundred_aim_transitions_never_publish_pre_aim_authority);
}
