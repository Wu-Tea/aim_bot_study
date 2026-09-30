#pragma once

#include "aim_scope_reducer.h"
#include "pipeline_contract/assist_activation.h"
#include <cmath>
#include <algorithm>

namespace controller_native {

enum class ApplicationAssistState : unsigned char { Inactive, Granted };

// Owns application activation lifetime and the effective AI activation state.
// It never creates a physical ADS edge, target identity, or fire command.
class AssistActivationReducer {
public:
    void reset() noexcept {
        application_ = ApplicationAssistState::Inactive;
        activation_ = pipeline_contract::AssistActivation::Off;
        deadline_ = 0.0;
    }
    void request_until(double deadline, float max_ai_magnitude = 0.10f) noexcept {
        application_budget_ = std::isfinite(max_ai_magnitude) ? std::clamp(max_ai_magnitude, 0.0f, 1.0f) : 0.0f;
        application_ = std::isfinite(deadline) && deadline > 0
            ? ApplicationAssistState::Granted : ApplicationAssistState::Inactive;
        deadline_ = deadline;
    }
    void revoke() noexcept { application_ = ApplicationAssistState::Inactive; }
    pipeline_contract::AssistActivation reduce(const AimScopeSnapshot& scope, double now) noexcept {
        if (!std::isfinite(now) || now >= deadline_) revoke();
        // Physical scope takes precedence without pausing/extending an app lease.
        activation_ = scope.physical_ads_ready || scope.manual_fire_active
            ? pipeline_contract::AssistActivation::Engaged
            : scope.assist_active ? pipeline_contract::AssistActivation::Primed
            : application_ == ApplicationAssistState::Granted
                ? pipeline_contract::AssistActivation::Application
                : pipeline_contract::AssistActivation::Off;
        return activation_;
    }
    pipeline_contract::AssistActivation state() const noexcept { return activation_; }
    float application_budget() const noexcept { return application_budget_; }
    ApplicationAssistState application_state() const noexcept { return application_; }
private:
    pipeline_contract::AssistActivation activation_ = pipeline_contract::AssistActivation::Off;
    ApplicationAssistState application_ = ApplicationAssistState::Inactive;
    double deadline_ = 0;
    float application_budget_ = 0.10f;
};
}  // namespace controller_native
