#include "vision_service.h"
#include "live_pipeline_trace.h"
#include "async_console.h"
#include "manual_relay.h"
#include "controller_native/incident_fixture_support.h"
#include "vision_controller_adapter.h"
#include "test_support/native_test_registry.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <fstream>
#include <random>
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

    void set_request(pipeline_contract::VisionRequest request) override {
        const bool aiming = pipeline_contract::requests_detection(request);
        aiming_history.push_back(aiming);
    }

    void set_user_aim_intent(const pipeline_contract::UserAimIntent& intent) override {
        last_intent = intent;
    }

    void set_viewport(const runtime_app::ViewportRequest& request) override {
        last_viewport = request;
        ++viewport_update_count;
    }
    void set_detection_policy(const runtime_app::VisionDetectionPolicy& policy) override {
        last_policy = policy;
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
        if (on_poll) on_poll();
        return result;
    }

    int poll_count = 0;
    bool authority_on_update = false;
    std::vector<bool> aiming_history;
    pipeline_contract::UserAimIntent last_intent;
    runtime_app::ViewportRequest last_viewport;
    runtime_app::VisionDetectionPolicy last_policy;
    int viewport_update_count = 0;
    std::function<void()> on_poll;

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

void test_detection_only_to_assist_fences_inflight_result() {
    using pipeline_contract::VisionRequest;
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    auto* fake = poller.get();
    fake->authority_on_update = true;
    runtime_app::VisionService service(std::move(poller), {});
    service.set_request(VisionRequest::DetectionOnly, at_ms(1));
    REQUIRE(service.step_for_test(at_ms(1)));
    const auto detection = service.latest_snapshot();
    REQUIRE(detection.result.has_target);
    REQUIRE(!detection.result.aim_authority && !detection.result.fire_authority);
    REQUIRE(!detection.result.auto_fire);
    fake->on_poll = [&] { service.set_request(VisionRequest::AssistSearch, at_ms(21)); };
    REQUIRE(service.step_for_test(at_ms(20)));
    const auto stale = service.latest_snapshot();
    REQUIRE(!stale.result.frame_updated && !stale.result.aim_authority);
    fake->on_poll = {};
    REQUIRE(service.step_for_test(at_ms(21)));
    const auto assist = service.latest_snapshot();
    REQUIRE(assist.request == VisionRequest::AssistSearch && assist.result.aim_authority);
    REQUIRE(assist.aim_transition_sequence > detection.aim_transition_sequence);
}

void test_release_hold_has_full_cadence_without_authority_and_expires() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    poller->authority_on_update = true;
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.idle_fps = 20.0;
    options.aim_release_hold_ms = 1000;
    runtime_app::VisionService service(std::move(poller), options);
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(0));
    REQUIRE(service.step_for_test(at_ms(0)));
    REQUIRE(service.latest_snapshot().requested_vision_fps == 20.0f);
    service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(1));
    REQUIRE(service.step_for_test(at_ms(1)));
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(2));
    REQUIRE(service.next_poll_due_for_test(at_ms(2)) == at_ms(11));
    REQUIRE(!service.step_for_test(at_ms(10)));
    REQUIRE(service.step_for_test(at_ms(11)));
    const auto held = service.latest_snapshot();
    REQUIRE(!pipeline_contract::requests_detection(held.request) && held.engine_aiming);
    REQUIRE(held.requested_vision_fps == 100.0f);
    REQUIRE(held.result.has_target && held.result.frame_updated);
    REQUIRE(!held.result.aim_authority && !held.result.fire_authority);
    REQUIRE(!held.result.auto_fire);
    // Repeated inactive requests must not renew the release deadline.
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(990));
    REQUIRE(service.step_for_test(at_ms(992)));
    REQUIRE(service.latest_snapshot().requested_vision_fps == 100.0f);
    REQUIRE(service.next_poll_due_for_test(at_ms(1002)) == at_ms(1042));
    REQUIRE(!service.step_for_test(at_ms(1002)));
    REQUIRE(service.step_for_test(at_ms(1042)));
    REQUIRE(service.latest_snapshot().requested_vision_fps == 20.0f);
}

void test_release_hold_reaim_has_new_epoch_and_renews_on_next_release() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    poller->authority_on_update = true;
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.idle_fps = 20.0;
    options.aim_release_hold_ms = 1000;
    runtime_app::VisionService service(std::move(poller), options);
    service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(0));
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(1));
    REQUIRE(service.step_for_test(at_ms(10)));
    const auto held = service.latest_snapshot();
    REQUIRE(!held.result.aim_authority);
    const auto new_epoch = service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(11));
    REQUIRE(held.aim_transition_sequence != new_epoch);
    REQUIRE(service.step_for_test(at_ms(11))); // bypasses remaining 9 ms
    REQUIRE(service.latest_snapshot().aim_transition_sequence == new_epoch);
    REQUIRE(service.latest_snapshot().result.aim_authority);
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(500));
    REQUIRE(service.step_for_test(at_ms(1499)));
    REQUIRE(service.latest_snapshot().requested_vision_fps == 100.0f);
    REQUIRE(!service.latest_snapshot().result.aim_authority);
    REQUIRE(!service.step_for_test(at_ms(1509)));
    REQUIRE(service.step_for_test(at_ms(1549)));
    REQUIRE(service.latest_snapshot().requested_vision_fps == 20.0f);
}

void test_release_hold_expires_when_idle_keepwarm_disabled() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.keepwarm_when_idle = false;
    options.aim_release_hold_ms = 1000;
    runtime_app::VisionService service(std::move(poller), options);
    REQUIRE(!service.step_for_test(at_ms(0)));
    service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(1));
    REQUIRE(service.step_for_test(at_ms(1)));
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(2));
    REQUIRE(service.step_for_test(at_ms(11)));
    REQUIRE(service.latest_snapshot().request == pipeline_contract::VisionRequest::Idle);
    REQUIRE(service.step_for_test(at_ms(1001)));
    REQUIRE(!service.step_for_test(at_ms(1002)));
    REQUIRE(!service.step_for_test(at_ms(2000)));
}

void test_release_hold_inflight_frame_cannot_cross_reaim_epoch() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    auto* raw = poller.get();
    raw->authority_on_update = true;
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.aim_release_hold_ms = 1000;
    runtime_app::VisionService service(std::move(poller), options);
    service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(0));
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(1));
    raw->on_poll = [&] { service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(11)); };
    REQUIRE(service.step_for_test(at_ms(10)));
    const auto stale = service.latest_snapshot();
    REQUIRE(stale.freshness == runtime_app::VisionSnapshotFreshness::NoUpdate);
    REQUIRE(!stale.result.frame_updated);
    REQUIRE(!stale.result.aim_authority && !stale.result.fire_authority);
    REQUIRE(!stale.result.auto_fire);
    raw->on_poll = {};
    REQUIRE(service.step_for_test(at_ms(11)));
    REQUIRE(service.latest_snapshot().result.aim_authority);
    // Also revoke a poll started while aiming if release occurs in flight.
    raw->on_poll = [&] { service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(22)); };
    REQUIRE(service.step_for_test(at_ms(21)));
    REQUIRE(!service.latest_snapshot().result.aim_authority);
    REQUIRE(!service.latest_snapshot().result.fire_authority);
    REQUIRE(!service.latest_snapshot().result.frame_updated);
}

void test_zero_release_hold_retains_immediate_idle_cadence() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{});
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.idle_fps = 20.0;
    options.aim_release_hold_ms = 0;
    runtime_app::VisionService service(std::move(poller), options);
    service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(0));
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_request(pipeline_contract::VisionRequest::Idle, at_ms(1));
    REQUIRE(service.next_poll_due_for_test(at_ms(1)) == at_ms(50));
    REQUIRE(!service.step_for_test(at_ms(10)));
    REQUIRE(service.step_for_test(at_ms(50)));
    REQUIRE(service.latest_snapshot().requested_vision_fps == 20.0f);
}

void test_keepwarm_polls_while_idle_and_active() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, true, true});
    FakeVisionPoller* raw = poller.get();
    runtime_app::VisionServiceOptions options;
    options.capture_fps = 100.0;
    options.idle_fps = 20.0;
    options.keepwarm_when_idle = true;

    runtime_app::VisionService service(std::move(poller), options);
    service.set_request(pipeline_contract::VisionRequest::Idle);
    REQUIRE(service.step_for_test(at_ms(0)));
    REQUIRE(!service.step_for_test(at_ms(10)));
    REQUIRE(service.step_for_test(at_ms(50)));

    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_request(pipeline_contract::VisionRequest::Idle);
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
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    service.set_request(pipeline_contract::VisionRequest::Idle);
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
    service.set_request(pipeline_contract::VisionRequest::Idle);
    REQUIRE(service.step_for_test(at_ms(0)));
    const runtime_app::VisionServiceSnapshot snapshot = service.latest_snapshot();

    REQUIRE(snapshot.freshness == runtime_app::VisionSnapshotFreshness::Fresh);
    REQUIRE(!pipeline_contract::requests_detection(snapshot.request));
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
    service.set_request(pipeline_contract::VisionRequest::Idle);
    REQUIRE(service.step_for_test(at_ms(0)));

    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    service.set_request(pipeline_contract::VisionRequest::Idle);
    REQUIRE(service.step_for_test(at_ms(0)));
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
    REQUIRE(service.step_for_test(at_ms(1)));
    const auto snapshot = service.latest_snapshot();
    REQUIRE(pipeline_contract::requests_detection(snapshot.request));
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
        service.set_request(pipeline_contract::VisionRequest::Idle);
        REQUIRE(service.step_for_test(at_ms(base)));
        const auto idle = service.latest_snapshot();
        REQUIRE(!idle.result.aim_authority);
        REQUIRE(!idle.result.fire_authority);
        service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    void set_request(pipeline_contract::VisionRequest) override {}
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
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
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
    result.source_present_steady_ns = 961'000'000;
    result.source_present_calibration_uncertainty_ns = 2'000'000;
    REQUIRE(!gate.accept(result, 1'000'000'000));
    result.source_present_calibration_uncertainty_ns = 500'000;
    REQUIRE(gate.accept(result, 1'000'000'000));
}

}  // namespace

void test_hot_policy_cannot_relabel_inflight_frame() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true, true});
    auto* raw = poller.get();
    runtime_app::VisionService service(std::move(poller), {});
    raw->on_poll = [&] { service.set_detection_policy({false, .35f, .65f, 7}); };
    const auto now = std::chrono::steady_clock::now();
    REQUIRE(service.step_for_test(now));
    REQUIRE(service.latest_snapshot().policy_revision == 0);
    raw->on_poll = {};
    REQUIRE(service.step_for_test(now + std::chrono::milliseconds(20)));
    REQUIRE(service.latest_snapshot().policy_revision == 7);
    REQUIRE(raw->last_policy.revision == 7 && !raw->last_policy.friendly && raw->last_policy.height == .35f);
}

void test_blocked_inference_does_not_block_controller_mailbox() {
    auto poller = std::make_unique<FakeVisionPoller>(std::vector<bool>{true});
    auto* raw = poller.get();
    runtime_app::VisionService service(std::move(poller), {});
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    auto release_future = release.get_future();
    raw->on_poll = [&] {
        entered.set_value();
        release_future.wait();
    };
    service.set_request(pipeline_contract::VisionRequest::AssistSearch);
    auto producer = std::async(std::launch::async, [&] {
        return service.step_for_test(std::chrono::steady_clock::now());
    });
    const bool started = entered_future.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready;
    auto consumer = std::async(std::launch::async, [&] {
        // The real runtime performs these operations before sending output.
        // Releasing ADS must be accepted even while inference cannot finish.
        service.set_request(pipeline_contract::VisionRequest::Idle);
        service.set_user_aim_intent({});
        return service.latest_snapshot().sequence;
    });
    const bool independent = consumer.wait_for(std::chrono::milliseconds(100)) ==
        std::future_status::ready;
    // Always unblock and join before assertions, including the failure path.
    release.set_value();
    producer.get();
    const auto sequence = consumer.get();
    REQUIRE(started);
    REQUIRE(independent);
    REQUIRE(sequence == 0);
    const auto stale = service.latest_snapshot();
    REQUIRE(!stale.result.aim_authority && !stale.result.fire_authority);
}

void register_vision_service_tests(native_test::Registry& registry) {
    registry.add_case("BaseRuntimeFreshness", "trace_queue_counts_loss_and_preserves_fifo", [] {
        runtime_app::LiveTraceQueue<2> queue;
        runtime_app::LiveTraceRecord r;
        r.sequence=1; REQUIRE(queue.push(r));
        r.sequence=2; REQUIRE(queue.push(r));
        r.sequence=3; REQUIRE(!queue.push(r));
        REQUIRE(queue.dropped()==1);
        REQUIRE(queue.pop(r) && r.sequence==1);
        r.sequence=4; REQUIRE(queue.push(r));
        REQUIRE(queue.pop(r) && r.sequence==2);
        REQUIRE(queue.pop(r) && r.sequence==4);
        REQUIRE(!queue.pop(r));
    });
    registry.add_context_case("BaseRuntimeFreshness", "trace_records_empty_and_fenced_polls_without_changing_selection", [](const native_test::TestContext& context) {
        auto trace=std::make_unique<runtime_app::LivePipelineTrace>(context.artifact_directory);
        for (bool enabled : {false, true}) {
            runtime_app::VisionServiceOptions options;
            options.live_trace=enabled?trace.get():nullptr;
            auto poller=std::make_unique<FakeVisionPoller>(std::vector<bool>{true,false,true});
            auto* fake=poller.get();fake->authority_on_update=true;
            runtime_app::VisionService service(std::move(poller), options);
            service.set_request(pipeline_contract::VisionRequest::AssistSearch, at_ms(1));
            REQUIRE(service.step_for_test(at_ms(1)));
            const auto selected=service.latest_snapshot();
            REQUIRE(selected.result.has_target && selected.result.aim_authority);
            REQUIRE(service.step_for_test(at_ms(21)));
            REQUIRE(!service.latest_snapshot().result.frame_updated);
            fake->on_poll=[&]{service.set_request(pipeline_contract::VisionRequest::Idle,at_ms(42));};
            REQUIRE(service.step_for_test(at_ms(41)));
            REQUIRE(!service.latest_snapshot().result.frame_updated);
            REQUIRE(!service.latest_snapshot().result.aim_authority);
        }
        trace->stop();
        std::ifstream csv(trace->directory()/"pipeline.csv");std::string line;
        REQUIRE(bool(std::getline(csv,line)));
        const auto columns=std::count(line.begin(),line.end(),',');
        unsigned rows=0;
        while(std::getline(csv,line)){REQUIRE(std::count(line.begin(),line.end(),',')==columns);++rows;}
        REQUIRE(rows==3);
        std::ifstream done(trace->directory()/"completion.json");
        std::getline(done,line);
        REQUIRE(line.find("\"complete\":true")!=std::string::npos);
        REQUIRE(line.find("\"vision_dropped\":0")!=std::string::npos);
    });
    registry.add_context_case("BaseRuntimeFreshness", "trace_drains_two_producers_on_shutdown", [](const native_test::TestContext& context) {
        auto trace=std::make_unique<runtime_app::LivePipelineTrace>(context.artifact_directory);
        std::thread vision([&]{for(unsigned i=1;i<=1000;++i){runtime_app::LiveTraceRecord r;r.sequence=i;trace->vision(r);}});
        std::thread control([&]{for(unsigned i=1;i<=1000;++i){runtime_app::LiveTraceRecord r;r.lane='C';r.tick_id=i;trace->controller(r);}});
        vision.join();control.join();trace->stop();
        std::ifstream csv(trace->directory()/"pipeline.csv");std::string line;std::getline(csv,line);
        unsigned v=0,c=0;while(std::getline(csv,line)){if(line[0]=='V')++v;if(line[0]=='C')++c;}
        REQUIRE(v==1000 && c==1000);
    });
    registry.add_case("BaseRuntimeFreshness", "relay_trace_times_actual_write_and_rejects_expired_visual", [] {
        using namespace controller_native;
        runtime_app::ManualRelay relay([]{return incident_fixture::ads_input();},
            [](const controller_native::GamepadOutputState&){controller_native::VirtualGamepadUpdateResult r;r.delivered=true;return r;},
            10, {}, {}, true);
        relay.start();
        const auto sample=relay.latest();
        const auto delivered=relay.submit({},sample,0,{});
        const auto returned=runtime_app::trace_now_ns();
        const auto expired=relay.submit({},relay.latest(),1.0,{});
        relay.stop();
        REQUIRE(delivered.request_accepted && delivered.output.delivered);
        REQUIRE(delivered.dequeued_ns>0 && delivered.dequeued_ns<=delivered.device_begin_ns);
        REQUIRE(delivered.device_begin_ns<=delivered.device_end_ns && delivered.device_end_ns<=returned);
        REQUIRE(!expired.request_accepted && !expired.visual_fresh && expired.device_begin_ns==0);
    });
    registry.add_case("BaseRuntimeFreshness", "native_controller_deliveries_cross_relay_with_stalls", [] {
        using namespace controller_native;
        std::atomic<unsigned> writes{0};
        runtime_app::ManualRelay relay([] { return incident_fixture::ads_input(); },
            [&](const GamepadOutputState& output) {
                ++writes;
                VirtualGamepadUpdateResult result;
                result.delivered = true;
                result.submitted_at_seconds = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                result.delivered_right_x = output.right_x;
                result.delivered_right_y = output.right_y;
                return result;
            }, 1000, {});
        relay.start();
        auto config = incident_fixture::base_config(40.f, 200.f);
        config.ai_aim.normalized_assist_parameters = true;
        double now = 0;
        NativeGamepadController controller(config, &now);
        incident_fixture::TargetSpec target;
        target.observation_id = target.selector_generation = 1;
        target.has_enemy_cue = true;
        std::uint64_t after = 0, continuity = 0;
        unsigned accepted = 0, rejected = 0;
        for (unsigned i = 1; i <= 240; ++i) {
            const auto sample = relay.wait_next(after);
            after = sample.sequence;
            if (relay.discontinuity_sequence() != continuity) {
                controller.observe_delivered_output({}, false, 0);
                continuity = relay.discontinuity_sequence();
            }
            now = std::chrono::duration<double>(sample.read_at.time_since_epoch()).count();
            if (i % 5 == 1) controller.submit_vision_snapshot(
                incident_fixture::observed_snapshot(target, i, now, 20.f + 5.f * std::sin(float(i) * .02f), 0));
            controller.begin_tick(sample.physical);
            auto frame = controller.resolve_control_frame();
            if (i % 80 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(45));
            now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const bool expired = controller.expire_visual_output(frame, now, 40);
            OutputComposer composer;
            REQUIRE(composer.compose(frame) == OutputComposeStatus::Ok);
            const auto output = *composer.finalized_output();
            controller.observe_composed_output(output);
            const auto receipt = relay.submit(output, sample,
                expired ? 0 : controller.visual_output_deadline_seconds(40), {});
            if (receipt.continuity_broken) controller.observe_delivered_output({}, false, 0);
            controller.observe_delivered_output(output, receipt.output.delivered, receipt.output.submitted_at_seconds);
            if (receipt.output.delivered) {
                ++accepted;
                REQUIRE(receipt.output.delivered_right_x == output.right_x);
            } else ++rejected;
        }
        relay.stop();
        REQUIRE(accepted > 0 && rejected >= 3 && writes.load() > 240);
    });
    registry.add_context_case("BaseRuntimeFreshness", "manual_relay_randomized_short_long_input_streams", [](const native_test::TestContext& context) {
        std::ofstream report(context.artifact_path("manual-relay-randomized.csv"));
        REQUIRE(report.good());
        report << "seed,scenario,algorithm_steps,device_writes,accepted,rejected,mismatches\n";
        for (unsigned seed : {10091u, 904831u, 71827u, 271009u}) {
            std::mt19937 random(seed);
            controller_native::PhysicalGamepadState current;
            std::atomic<unsigned> mismatches{0}, writes{0};
            runtime_app::ManualRelay relay([&] {
                current = {};
                current.connected = true;
                current.right_x = (int(random() % 65535) - 32767) / 32767.f;
                current.right_y = (int(random() % 65535) - 32767) / 32767.f;
                current.left_x = (int(random() % 65535) - 32767) / 32767.f;
                current.left_trigger = random() % 2 ? 1.f : 0.f;
                current.rb = random() % 2 != 0;
                current.a = random() % 2 != 0;
                return current;
            }, [&](const controller_native::GamepadOutputState& output) {
                if (output.right_x != current.right_x || output.right_y != current.right_y ||
                    output.left_x != current.left_x || output.left_trigger != current.left_trigger ||
                    output.rb != current.rb || output.a != current.a) ++mismatches;
                ++writes;
                controller_native::VirtualGamepadUpdateResult result;
                result.delivered = true;
                result.delivered_right_x = output.right_x;
                result.delivered_right_y = output.right_y;
                return result;
            }, 1000, {});
            relay.start();
            std::uint64_t after = 0;
            unsigned accepted = 0, rejected = 0;
            for (int scenario = 0; scenario < 16; ++scenario) {
                const auto initial_writes = writes.load();
                const auto initial_accepted = accepted, initial_rejected = rejected;
                const int steps = scenario % 2 ? 48 : 8;
                for (int i = 0; i < steps; ++i) {
                    const auto sample = relay.wait_next(after);
                    after = sample.sequence;
                    controller_native::OutputComposer composer;
                    const auto frame = controller_native::ControlFrame::begin(sample.physical,
                        pipeline_contract::ControllerTickId::from(after),
                        pipeline_contract::EventSequence::from(after));
                    REQUIRE(composer.compose(frame) == controller_native::OutputComposeStatus::Ok);
                    if (i == 3) std::this_thread::sleep_for(std::chrono::milliseconds(scenario % 4 ? 4 : 40));
                    const auto receipt = relay.submit(*composer.finalized_output(), sample, 0, {});
                    if (receipt.output.delivered) ++accepted;
                    else ++rejected;
                }
                report << seed << ',' << scenario << ',' << steps << ',' << writes.load() - initial_writes
                    << ',' << accepted - initial_accepted << ',' << rejected - initial_rejected << ',' << mismatches.load() << '\n';
            }
            relay.stop();
            REQUIRE(writes.load() > 448 && mismatches.load() == 0 && accepted > 0 && rejected > 0);
        }
    });
    registry.add_case("BaseRuntimeFreshness", "manual_relay_survives_algorithm_stall_and_rejects_late_output", [] {
        std::atomic<float> manual_x{.12f};
        std::atomic<bool> ads{true};
        std::atomic<unsigned> writes{0};
        std::mutex output_mutex;
        controller_native::GamepadOutputState last;
        runtime_app::ManualRelay relay([&] {
            controller_native::PhysicalGamepadState p;
            p.connected = true; p.right_x = manual_x.load();
            p.left_trigger = ads.load() ? 1.f : 0.f;
            return p;
        }, [&](const controller_native::GamepadOutputState& output) {
            { std::lock_guard<std::mutex> lock(output_mutex); last = output; }
            ++writes;
            controller_native::VirtualGamepadUpdateResult result;
            result.delivered = true;
            result.submitted_at_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            result.delivered_right_x = output.right_x;
            result.delivered_right_y = output.right_y;
            return result;
        }, 1000, {});
        relay.start();
        auto sample = relay.latest();
        controller_native::GamepadOutputState ai;
        ai.right_x = .9f; ai.rb = true; ai.left_trigger = 1;
        bool delivered = false;
        for (int i = 0; i < 20 && !delivered; ++i) {
            sample = relay.wait_next(sample.sequence);
            delivered = relay.submit(ai, sample, 0, {}).output.delivered;
        }
        REQUIRE(delivered);
        const auto before = writes.load();
        // No further algorithm deliveries for 60 ms; manual sampling and
        // sending must continue, including release of synthetic fire.
        manual_x.store(-.37f); ads.store(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        controller_native::GamepadOutputState observed;
        { std::lock_guard<std::mutex> lock(output_mutex); observed = last; }
        REQUIRE(writes.load() >= before + 10);
        REQUIRE(observed.right_x == -.37f && observed.left_trigger == 0 && !observed.rb);
        const auto late = relay.submit(ai, sample, 0, {});
        REQUIRE(!late.output.delivered && late.continuity_broken);
        auto current = relay.latest();
        const double expired = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count() - .001;
        REQUIRE(!relay.submit(ai, current, expired, {}).output.delivered);
        { std::lock_guard<std::mutex> lock(output_mutex); observed = last; }
        REQUIRE(observed.right_x == -.37f && !observed.rb);
        relay.stop();
    });
    registry.add_case("BaseRuntimeFreshness", "blocked_console_cannot_block_producer", [] {
        std::promise<void> entered, release;
        auto entered_future = entered.get_future();
        auto release_future = release.get_future().share();
        std::atomic<bool> first{true};
        runtime_app::AsyncConsole console([&](const std::string&) {
            if (first.exchange(false)) entered.set_value();
            release_future.wait();
        });
        console.offer("block writer");
        const bool started = entered_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        auto producer = std::async(std::launch::async, [&] {
            for (int i = 0; i < 1000; ++i) console.offer("diagnostic");
        });
        const bool independent = producer.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
        const auto dropped = console.dropped();
        release.set_value();
        producer.get();
        REQUIRE(started && independent && dropped > 0);
    });
    registry.add_case("BaseRuntimeFreshness", "live_config_caps_cached_visual_authority", [] {
        controller_native::RuntimeConfig config;
        config.vision.gpu_service_enabled = false;
        config.gamepad.tracker.max_observation_age_ms = 250.f;
        config.gamepad.auto_fire.max_source_age_ms = 250.f;
        auto live = runtime_app::live_control_config(config);
        REQUIRE(live.vision.gpu_service_enabled);
        REQUIRE(live.gamepad.tracker.max_observation_age_ms == 40.f);
        REQUIRE(live.gamepad.auto_fire.max_source_age_ms == 40.f);
        config.gamepad.tracker.max_observation_age_ms = 20.f;
        REQUIRE(runtime_app::live_control_config(config).gamepad.tracker.max_observation_age_ms == 20.f);
    });
    registry.add_case("BaseRuntimeFreshness", "vision_delivery_has_strict_40ms_ceiling", [] {
        for (const auto age_ns : {39'999'999ull, 40'000'000ull, 40'000'001ull, 80'000'000ull}) {
            runtime_app::VisionDeliveryGate gate(250.f);
            vision_native::VisionResult result;
            result.frame_updated = true;
            result.frame_id = 1;
            result.captured_at_ns = 1'000'000'000;
            result.result_at_ns = result.captured_at_ns + 1'000'000;
            REQUIRE(gate.accept(result, result.captured_at_ns + age_ns) == (age_ns < 40'000'000));
        }
    });
    registry.add_case("BaseRuntimeFreshness", "blocked_inference_keeps_mailbox_responsive", test_blocked_inference_does_not_block_controller_mailbox);
    registry.add_case("BaseRuntimeFreshness", "hot_policy_inflight_frame_revision", test_hot_policy_cannot_relabel_inflight_frame);
    registry.add_case("BaseRuntimeFreshness", "detection_to_assist_epoch_fence", test_detection_only_to_assist_fences_inflight_result);
    registry.add_case("BaseRuntimeFreshness", "release_hold_cadence_authority_and_expiry", test_release_hold_has_full_cadence_without_authority_and_expires);
    registry.add_case("BaseRuntimeFreshness", "release_hold_reaim_epoch_and_renewal", test_release_hold_reaim_has_new_epoch_and_renews_on_next_release);
    registry.add_case("BaseRuntimeFreshness", "release_hold_without_idle_keepwarm", test_release_hold_expires_when_idle_keepwarm_disabled);
    registry.add_case("BaseRuntimeFreshness", "release_hold_inflight_epoch_fence", test_release_hold_inflight_frame_cannot_cross_reaim_epoch);
    registry.add_case("BaseRuntimeFreshness", "zero_release_hold_restores_idle_cadence", test_zero_release_hold_retains_immediate_idle_cadence);
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
