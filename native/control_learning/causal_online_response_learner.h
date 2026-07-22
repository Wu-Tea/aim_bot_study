#pragma once

#include "control_learning/causal_response_types.h"
#include "control_learning/control_history.h"
#include "control_learning/robust_ew_rls.h"
#include "pipeline_contract/committed_capture_observation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace control_learning {

struct CausalResponseEstimate {
    ResponseMatrix2d right_fast{};
    ResponseMatrix2d right_stable{};
    ResponseMatrix2d left_fast{};
    ResponseMatrix2d left_stable{};
    float best_delay_ms = 45.0f;
    float selected_delay_ms = 45.0f;
    float selected_delay_confidence = 0.0f;
    float right_confidence = 0.0f;
    float left_confidence = 0.0f;
    float joint_confidence = 0.0f;
    float excitation = 0.0f;
    float residual = 0.0f;
    float normalized_target_size = 0.0f;
    bool delay_switch_pending = false;
    bool finite = true;
};

class CausalOnlineResponseLearner {
public:
    SampleAssessment observe_vision(
        const pipeline_contract::CommittedCaptureObservation& observation,
        const ControlHistory<1024>& history) noexcept;
    CausalResponseEstimate estimate() const noexcept;
    void reset_session() noexcept;
    bool pairing_active() const noexcept { return has_anchor_; }

private:
    static constexpr std::size_t kDelayCount = 19;
    static constexpr int kMinimumDelayMs = 10;
    static constexpr int kDelayStepMs = 5;
    using Rls2 = RobustEwRls<2>;

    struct Candidate {
        Rls2 right_fast_x;
        Rls2 right_fast_y;
        Rls2 right_stable_x;
        Rls2 right_stable_y;
        Rls2 left_fast_x;
        Rls2 left_fast_y;
        Rls2 left_stable_x;
        Rls2 left_stable_y;
        double residual_score = 1.0e6;
        double excitation = 0.0;
        std::uint64_t accepted = 0;
    };

    static std::uint64_t delay_ns(std::size_t index) noexcept;
    static bool finite_matrix(const ResponseMatrix2d& matrix) noexcept;
    static ResponseMatrix2d response_from(
        const Rls2& x, const Rls2& y) noexcept;
    void clear_interval_pairing() noexcept;
    void reset_fast() noexcept;
    void reset_left() noexcept;
    void update_selection() noexcept;

    std::array<Candidate, kDelayCount> candidates_{};
    pipeline_contract::CommittedCaptureObservation anchor_{};
    bool has_anchor_ = false;
    std::size_t selected_index_ = 7;
    std::size_t best_index_ = 7;
    std::size_t pending_index_ = 7;
    std::uint8_t pending_wins_ = 0;
    bool switch_pending_ = false;
    double right_prior_confidence_scale_ = 1.0;
    float normalized_target_size_ = 0.0f;
};

}  // namespace control_learning
