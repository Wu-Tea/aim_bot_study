#include "bodylock_lifecycle.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

controller_native::BodylockLifecycleInput observed(std::uint64_t track_id) {
    controller_native::BodylockLifecycleInput input;
    input.aiming = true;
    input.bodylock_available = true;
    input.selected_track_id = track_id;
    input.authority = pipeline_contract::AssistAuthorityState::ObservedStrong;
    input.authority_reason = pipeline_contract::AssistAuthorityReason::StrongObserved;
    input.now_seconds = 10.0;
    return input;
}

void test_tracking_coasts_and_resumes_without_reset() {
    controller_native::BodylockLifecycle lifecycle;
    auto decision = lifecycle.update(observed(42));
    require(decision.state == pipeline_contract::BodylockLifecycleState::Warm,
            "first observed tick must warm bodylock");
    decision = lifecycle.update(observed(42));
    require(decision.state == pipeline_contract::BodylockLifecycleState::Tracking,
            "second observed tick must track");

    auto gap = observed(42);
    gap.bodylock_available = false;
    gap.authority = pipeline_contract::AssistAuthorityState::Continuity;
    gap.authority_reason = pipeline_contract::AssistAuthorityReason::ShortEvidenceGap;
    gap.now_seconds = 10.010;
    decision = lifecycle.update(gap);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Coast,
            "same-track continuity must coast");
    require(!decision.reset_assist_history,
            "same-track continuity must preserve assist history");

    decision = lifecycle.update(observed(42));
    require(decision.state == pipeline_contract::BodylockLifecycleState::Tracking,
            "fresh same-track evidence must resume tracking");
    require(!decision.reset_assist_history,
            "coast-to-tracking must preserve assist history");
}

void test_track_switch_and_yield_are_immediate() {
    controller_native::BodylockLifecycle lifecycle;
    (void)lifecycle.update(observed(7));
    (void)lifecycle.update(observed(7));
    auto decision = lifecycle.update(observed(8));
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "current strong selected-track switch must release the old track first");
    require(!decision.reset_assist_history,
            "track switch must release through the envelope instead of hard reset");

    decision = lifecycle.update(observed(8));
    require(decision.state == pipeline_contract::BodylockLifecycleState::Warm,
            "next strong tick may warm the newly selected track");

    auto rejected = observed(8);
    rejected.authority = pipeline_contract::AssistAuthorityState::Reject;
    rejected.authority_reason = pipeline_contract::AssistAuthorityReason::UserYield;
    decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "new-track warm state must yield immediately on no authority");
    require(!decision.reset_assist_history,
            "identity rejection must request envelope release, not generic reset");

    decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Inactive,
            "yield must settle to inactive on the following no-authority tick");
}

void test_expired_or_opposing_authority_yields() {
    controller_native::BodylockLifecycle lifecycle;
    (void)lifecycle.update(observed(9));
    (void)lifecycle.update(observed(9));
    auto rejected = observed(9);
    rejected.authority = pipeline_contract::AssistAuthorityState::Reject;
    rejected.authority_reason = pipeline_contract::AssistAuthorityReason::Stale;
    auto decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "expired continuity must yield");
    require(!decision.reset_assist_history,
            "expired identity must preserve delivered history for bounded release");

    lifecycle.reset();
    (void)lifecycle.update(observed(9));
    (void)lifecycle.update(observed(9));
    rejected.authority_reason = pipeline_contract::AssistAuthorityReason::UserYield;
    decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "opposing manual authority decision must yield");
    require(decision.reason == controller_native::BodylockTransitionReason::UserYield,
            "yield reason must remain observable");
}

void test_invalid_geometry_coasts_briefly_then_yields_without_reset() {
    controller_native::BodylockLifecycle lifecycle;
    auto tracked = observed(55);
    tracked.now_seconds = 20.0;
    (void)lifecycle.update(tracked);
    tracked.now_seconds = 20.001;
    (void)lifecycle.update(tracked);

    auto invalid_geometry = tracked;
    invalid_geometry.bodylock_available = false;
    invalid_geometry.now_seconds = 20.010;
    auto decision = lifecycle.update(invalid_geometry);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Coast,
            "same-track invalid geometry may use the bounded geometry grace window");
    require(!decision.reset_assist_history,
            "geometry grace must retain the delivered envelope");

    invalid_geometry.now_seconds = 20.051;
    decision = lifecycle.update(invalid_geometry);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "invalid geometry beyond 40 ms must release authority");
    require(!decision.reset_assist_history,
            "expired geometry grace must decay instead of hard resetting");
}

void test_same_track_continuity_expires_after_96_ms() {
    controller_native::BodylockLifecycle lifecycle;
    auto tracked = observed(66);
    tracked.now_seconds = 30.0;
    (void)lifecycle.update(tracked);
    tracked.now_seconds = 30.001;
    (void)lifecycle.update(tracked);

    auto gap = tracked;
    gap.bodylock_available = false;
    gap.authority = pipeline_contract::AssistAuthorityState::Continuity;
    gap.authority_reason = pipeline_contract::AssistAuthorityReason::ShortEvidenceGap;
    gap.now_seconds = 30.010;
    require(lifecycle.update(gap).state == pipeline_contract::BodylockLifecycleState::Coast,
            "same-track continuity should enter coast");

    gap.now_seconds = 30.107;
    const auto decision = lifecycle.update(gap);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "same-track continuity must not coast beyond 96 ms");
    require(!decision.reset_assist_history,
            "coast expiry must release through the envelope");
}

}  // namespace

int main() {
    try {
        test_tracking_coasts_and_resumes_without_reset();
        test_track_switch_and_yield_are_immediate();
        test_expired_or_opposing_authority_yields();
        test_invalid_geometry_coasts_briefly_then_yields_without_reset();
        test_same_track_continuity_expires_after_96_ms();
        std::cout << "bodylock lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bodylock lifecycle tests failed: " << error.what() << '\n';
        return 1;
    }
}
