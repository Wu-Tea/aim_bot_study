#include "ads_lifecycle_reducer.h"

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_ads_lifecycle_is_mutually_exclusive() {
    controller_native::AdsLifecycleReducer reducer;
    reducer.begin_epoch(7, 10.0);
    require(reducer.snapshot().state ==
                pipeline_contract::AdsAcquisitionState::ArmedWaitingForTarget,
            "epoch did not arm acquisition");
    reducer.admit_target(10.01);
    require(reducer.snapshot().target_admitted &&
                reducer.snapshot().state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringNominal,
            "target admission did not enter nominal acquisition");
    reducer.extend();
    require(reducer.snapshot().state ==
                pipeline_contract::AdsAcquisitionState::AcquiringExtended,
            "nominal acquisition did not extend");
    reducer.complete(
        pipeline_contract::AdsDecisionReason::Settled,
        10.12);
    require(reducer.snapshot().snap_consumed &&
                reducer.snapshot().terminal_reason ==
                    pipeline_contract::AdsDecisionReason::Settled,
            "completion did not atomically consume the ADS job");
    reducer.consume();
    require(reducer.snapshot().state ==
                pipeline_contract::AdsAcquisitionState::Consumed,
            "completed ADS job did not enter consumed state");
}

void test_projection_reports_one_state_snapshot() {
    controller_native::AdsLifecycleReducer reducer;
    reducer.begin_epoch(2, 1.0);
    reducer.admit_target(1.02);
    pipeline_contract::TargetPlan plan{};
    reducer.project(&plan, 1.05);
    require(plan.physical_ads_epoch == 2 &&
                plan.ads_acquisition_active &&
                plan.ads_acquisition_state ==
                    pipeline_contract::AdsAcquisitionState::AcquiringNominal,
            "TargetPlan projection mixed ADS states");
}

}  // namespace

int main() {
    test_ads_lifecycle_is_mutually_exclusive();
    test_projection_reports_one_state_snapshot();
    return 0;
}
