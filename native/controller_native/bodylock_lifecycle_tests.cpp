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
    gap.authority = pipeline_contract::AssistAuthorityState::Continuity;
    gap.authority_reason = pipeline_contract::AssistAuthorityReason::ShortEvidenceGap;
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
    lifecycle.update(observed(7));
    lifecycle.update(observed(7));
    auto decision = lifecycle.update(observed(8));
    require(decision.state == pipeline_contract::BodylockLifecycleState::Warm,
            "current strong selected-track switch must warm the new track");
    require(decision.reset_assist_history,
            "track switch must clear old-target assist history");

    auto rejected = observed(8);
    rejected.authority = pipeline_contract::AssistAuthorityState::Reject;
    rejected.authority_reason = pipeline_contract::AssistAuthorityReason::UserYield;
    decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "new-track warm state must yield immediately on no authority");

    decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Inactive,
            "yield must settle to inactive on the following no-authority tick");
}

void test_expired_or_opposing_authority_yields() {
    controller_native::BodylockLifecycle lifecycle;
    lifecycle.update(observed(9));
    lifecycle.update(observed(9));
    auto rejected = observed(9);
    rejected.authority = pipeline_contract::AssistAuthorityState::Reject;
    rejected.authority_reason = pipeline_contract::AssistAuthorityReason::Stale;
    auto decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "expired continuity must yield");

    lifecycle.reset();
    lifecycle.update(observed(9));
    lifecycle.update(observed(9));
    rejected.authority_reason = pipeline_contract::AssistAuthorityReason::UserYield;
    decision = lifecycle.update(rejected);
    require(decision.state == pipeline_contract::BodylockLifecycleState::Yield,
            "opposing manual authority decision must yield");
    require(decision.reason == controller_native::BodylockTransitionReason::UserYield,
            "yield reason must remain observable");
}

}  // namespace

int main() {
    try {
        test_tracking_coasts_and_resumes_without_reset();
        test_track_switch_and_yield_are_immediate();
        test_expired_or_opposing_authority_yields();
        std::cout << "bodylock lifecycle tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bodylock lifecycle tests failed: " << error.what() << '\n';
        return 1;
    }
}
