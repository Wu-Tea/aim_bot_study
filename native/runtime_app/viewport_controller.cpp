#include "viewport_controller.h"

#include <algorithm>
#include <cmath>

namespace runtime_app {
namespace {

std::uint64_t milliseconds_to_ns(float value) noexcept {
    return static_cast<std::uint64_t>(
        std::max(0.0f, value) * 1'000'000.0f);
}

float seconds_between(std::uint64_t older, std::uint64_t newer) noexcept {
    if (older == 0 || newer <= older) return 0.0f;
    return static_cast<float>(newer - older) / 1'000'000'000.0f;
}

float blend(float previous, float sample, float alpha) noexcept {
    return previous + std::clamp(alpha, 0.0f, 1.0f) * (sample - previous);
}

int level_value(ViewportLevel level) noexcept {
    return static_cast<int>(level);
}

ViewportLevel one_level_smaller(ViewportLevel level) noexcept {
    switch (level) {
    case ViewportLevel::Rescue:
        return ViewportLevel::Normal;
    case ViewportLevel::Normal:
    case ViewportLevel::Precision:
    default:
        return ViewportLevel::Precision;
    }
}

}  // namespace

const char* viewport_level_name(ViewportLevel level) noexcept {
    switch (level) {
    case ViewportLevel::Precision:
        return "precision";
    case ViewportLevel::Rescue:
        return "rescue";
    case ViewportLevel::Normal:
    default:
        return "normal";
    }
}

ViewportController::ViewportController(ViewportControllerConfig config)
    : config_(config) {
    current_.level = config_.enabled ? ViewportLevel::Precision : ViewportLevel::Normal;
    const auto size = dimensions(current_.level);
    current_.width = size.width;
    current_.height = size.height;
}

ViewportRequest ViewportController::update(
    const pipeline_contract::TargetPlan& plan,
    const pipeline_contract::CommittedCaptureObservation* observation,
    std::uint64_t now_ns) noexcept {
    current_.changed = false;
    if (!config_.enabled) return current_;

    const bool fresh =
        observation != nullptr &&
        pipeline_contract::valid(*observation) &&
        observation->fresh_observed &&
        observation->persistent_target_id == plan.target_id &&
        observation->source_frame_id != last_source_frame_id_;

    if (plan.target_id == 0 || plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
        target_id_ = 0;
        has_geometry_ = false;
        edge_pressure_ = false;
        expand_evidence_frames_ = 0;
        shrink_candidate_since_ns_ = 0;
        if (current_.level != ViewportLevel::Precision) {
            switch_to(ViewportLevel::Precision, now_ns, 0);
        }
        return current_;
    }

    if (target_id_ != plan.target_id) {
        target_id_ = plan.target_id;
        last_source_frame_id_ = 0;
        last_observed_ns_ = 0;
        has_geometry_ = false;
        edge_pressure_ = false;
        center_velocity_x_ = 0.0f;
        center_velocity_y_ = 0.0f;
        half_width_velocity_ = 0.0f;
        half_height_velocity_ = 0.0f;
        expand_evidence_frames_ = 0;
        shrink_candidate_since_ns_ = 0;
    }

    if (fresh) {
        const float width = std::max(1.0f, observation->stable_body_size_px.x);
        const float height = std::max(1.0f, observation->stable_body_size_px.y);
        const float next_center_x = observation->stable_error_px.x;
        const float next_center_y =
            observation->stable_error_px.y +
            height * (0.5f - std::clamp(config_.aim_height_ratio, 0.0f, 1.0f));
        const float next_half_width = width * 0.5f;
        const float next_half_height = height * 0.5f;
        const float dt = seconds_between(last_observed_ns_, observation->captured_at_ns);
        if (has_geometry_ && dt > 0.001f && dt < 0.250f) {
            const float max_center_velocity =
                std::max(0.0f, config_.max_center_velocity_px_per_sec);
            const float max_size_velocity =
                std::max(0.0f, config_.max_half_size_velocity_px_per_sec);
            center_velocity_x_ = blend(
                center_velocity_x_,
                std::clamp(
                    (next_center_x - center_x_) / dt,
                    -max_center_velocity,
                    max_center_velocity),
                config_.center_velocity_alpha);
            center_velocity_y_ = blend(
                center_velocity_y_,
                std::clamp(
                    (next_center_y - center_y_) / dt,
                    -max_center_velocity,
                    max_center_velocity),
                config_.center_velocity_alpha);
            half_width_velocity_ = blend(
                half_width_velocity_,
                std::clamp(
                    (next_half_width - half_width_) / dt,
                    -max_size_velocity,
                    max_size_velocity),
                config_.size_velocity_alpha);
            half_height_velocity_ = blend(
                half_height_velocity_,
                std::clamp(
                    (next_half_height - half_height_) / dt,
                    -max_size_velocity,
                    max_size_velocity),
                config_.size_velocity_alpha);
        }
        center_x_ = next_center_x;
        center_y_ = next_center_y;
        half_width_ = next_half_width;
        half_height_ = next_half_height;
        has_geometry_ = true;
        last_source_frame_id_ = observation->source_frame_id;
        last_observed_ns_ = observation->captured_at_ns;
    }

    if (!has_geometry_) return current_;

    const float horizon = std::max(0.0f, config_.prediction_seconds);
    const float predicted_center_x = center_x_ + center_velocity_x_ * horizon;
    const float predicted_center_y = center_y_ + center_velocity_y_ * horizon;
    const float predicted_half_width =
        std::max(1.0f, half_width_ + std::max(0.0f, half_width_velocity_) * horizon);
    const float predicted_half_height =
        std::max(1.0f, half_height_ + std::max(0.0f, half_height_velocity_) * horizon);
    ViewportLevel required = required_level(
        predicted_center_x,
        predicted_center_y,
        predicted_half_width,
        predicted_half_height);

    if (fresh) {
        edge_pressure_ = !fits_level(
            current_.level,
            predicted_center_x,
            predicted_center_y,
            predicted_half_width,
            predicted_half_height,
            config_.shrink_extra_margin_px);
    }
    const bool missing_after_edge_pressure =
        !fresh &&
        edge_pressure_ &&
        plan.lifecycle == pipeline_contract::TargetLifecycle::CueContinuation &&
        now_ns <= last_observed_ns_ +
            milliseconds_to_ns(config_.edge_loss_rescue_hold_ms);
    if (missing_after_edge_pressure) {
        required = ViewportLevel::Rescue;
    }

    if (level_value(required) > level_value(current_.level)) {
        shrink_candidate_since_ns_ = 0;
        const bool already_outside_current = !fits_level(
            current_.level,
            predicted_center_x,
            predicted_center_y,
            predicted_half_width,
            predicted_half_height,
            -std::min(config_.safety_margin_x_px, config_.safety_margin_y_px));
        if (already_outside_current || missing_after_edge_pressure) {
            switch_to(required, now_ns, last_source_frame_id_);
            expand_evidence_frames_ = 0;
        } else if (fresh) {
            ++expand_evidence_frames_;
            if (expand_evidence_frames_ >=
                std::max<std::uint32_t>(1, config_.expand_confirm_frames)) {
                switch_to(required, now_ns, last_source_frame_id_);
                expand_evidence_frames_ = 0;
            }
        }
        return current_;
    }
    expand_evidence_frames_ = 0;

    if (level_value(required) < level_value(current_.level)) {
        const ViewportLevel next_smaller = one_level_smaller(current_.level);
        const bool safely_inside = fits_level(
            next_smaller,
            predicted_center_x,
            predicted_center_y,
            predicted_half_width,
            predicted_half_height,
            config_.shrink_extra_margin_px);
        if (!safely_inside) {
            shrink_candidate_since_ns_ = 0;
            return current_;
        }
        if (shrink_candidate_since_ns_ == 0) shrink_candidate_since_ns_ = now_ns;
        const float dwell_ms = current_.level == ViewportLevel::Rescue
            ? config_.rescue_min_dwell_ms : config_.normal_min_dwell_ms;
        if (now_ns >= state_entered_ns_ + milliseconds_to_ns(dwell_ms) &&
            now_ns >= shrink_candidate_since_ns_ +
                milliseconds_to_ns(config_.shrink_stable_ms)) {
            switch_to(next_smaller, now_ns, last_source_frame_id_);
            shrink_candidate_since_ns_ = 0;
        }
    } else {
        shrink_candidate_since_ns_ = 0;
    }
    return current_;
}

ViewportRequest ViewportController::reset(std::uint64_t now_ns) noexcept {
    target_id_ = 0;
    last_source_frame_id_ = 0;
    last_observed_ns_ = 0;
    state_entered_ns_ = now_ns;
    shrink_candidate_since_ns_ = 0;
    expand_evidence_frames_ = 0;
    has_geometry_ = false;
    edge_pressure_ = false;
    center_velocity_x_ = 0.0f;
    center_velocity_y_ = 0.0f;
    half_width_velocity_ = 0.0f;
    half_height_velocity_ = 0.0f;
    switch_to(
        config_.enabled ? ViewportLevel::Precision : ViewportLevel::Normal,
        now_ns,
        0);
    return current_;
}

const ViewportRequest& ViewportController::current() const noexcept {
    return current_;
}

ViewportDimensions ViewportController::dimensions(ViewportLevel level) const noexcept {
    switch (level) {
    case ViewportLevel::Precision:
        return config_.precision;
    case ViewportLevel::Rescue:
        return config_.rescue;
    case ViewportLevel::Normal:
    default:
        return config_.normal;
    }
}

ViewportLevel ViewportController::required_level(
    float center_x,
    float center_y,
    float half_width,
    float half_height,
    float extra_margin) const noexcept {
    if (fits_level(
            ViewportLevel::Precision,
            center_x, center_y, half_width, half_height, extra_margin)) {
        return ViewportLevel::Precision;
    }
    if (fits_level(
            ViewportLevel::Normal,
            center_x, center_y, half_width, half_height, extra_margin)) {
        return ViewportLevel::Normal;
    }
    return ViewportLevel::Rescue;
}

bool ViewportController::fits_level(
    ViewportLevel level,
    float center_x,
    float center_y,
    float half_width,
    float half_height,
    float extra_margin) const noexcept {
    const auto size = dimensions(level);
    const float margin_x = std::max(0.0f, config_.safety_margin_x_px + extra_margin);
    const float margin_y = std::max(0.0f, config_.safety_margin_y_px + extra_margin);
    return std::fabs(center_x) + half_width + margin_x <=
            static_cast<float>(size.width) * 0.5f &&
        std::fabs(center_y) + half_height + margin_y <=
            static_cast<float>(size.height) * 0.5f;
}

void ViewportController::switch_to(
    ViewportLevel level,
    std::uint64_t now_ns,
    std::uint64_t source_frame_id) noexcept {
    const bool changed = current_.level != level;
    current_.level = level;
    const auto size = dimensions(level);
    current_.width = size.width;
    current_.height = size.height;
    current_.source_frame_id = source_frame_id;
    current_.changed = changed;
    if (changed) {
        ++current_.sequence;
        state_entered_ns_ = now_ns;
    }
}

}  // namespace runtime_app
