#pragma once

#include "pipeline_contract/vision_observation.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace pipeline_contract {

inline constexpr std::size_t kMaxPlanHorizonSamples = 8;

enum class TargetLifecycle : unsigned char {
    None,
    Observed,
    Coasting,
    Reacquiring,
};

enum class TargetMotion : unsigned char {
    Ambiguous,
    Steady,
    Strafe,
    Jump,
    Fall,
};

enum class ControlMode : unsigned char {
    Manual,
    AdsAcquire,
    BodyLockFollow,
};

enum class FireSuppressionReason : unsigned char {
    None,
    NoTarget,
    Stale,
    Ambiguous,
    LargeError,
    HighVelocity,
    ManualReject,
    Cadence,
    AimOnly,
};

struct PlanHorizonSample {
    float time_seconds = 0.0f;
    Vec2f error_px{};
};

struct TargetPlan {
    std::uint64_t generation = 0;
    std::uint64_t source_frame_id = 0;
    std::uint64_t target_id = 0;
    TargetLifecycle lifecycle = TargetLifecycle::None;
    TargetMotion motion = TargetMotion::Ambiguous;
    ControlMode mode = ControlMode::Manual;
    Vec2f aim_px{};
    Vec2f predicted_aim_px{};
    Vec2f error_px{};
    Vec2f error_rate_px_per_sec{};
    Vec2f velocity_px_per_sec{};
    Vec2f acceleration_px_per_sec2{};
    float observation_age_ms = 0.0f;
    float confidence = 0.0f;
    float reliability = 0.0f;
    float normalized_size = 0.0f;
    float occlusion_budget_ms = 0.0f;
    float ads_demand = 0.0f;
    float bodylock_demand = 0.0f;
    float aim_authority = 0.0f;
    float response_scale = 0.0f;
    float response_confidence = 0.0f;
    std::uint32_t horizon_count = 0;
    std::array<PlanHorizonSample, kMaxPlanHorizonSamples> horizon{};
    bool fire_authority = false;
    bool fire_requested = false;
    FireSuppressionReason fire_suppression = FireSuppressionReason::NoTarget;
};

inline bool finite(Vec2f value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline bool unit_interval(float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

inline bool valid(const TargetPlan& plan) noexcept {
    return finite(plan.aim_px) && finite(plan.predicted_aim_px) &&
           finite(plan.error_px) && finite(plan.error_rate_px_per_sec) &&
           finite(plan.velocity_px_per_sec) && finite(plan.acceleration_px_per_sec2) &&
           unit_interval(plan.confidence) && unit_interval(plan.reliability) &&
           unit_interval(plan.normalized_size) && unit_interval(plan.ads_demand) &&
           unit_interval(plan.bodylock_demand) && unit_interval(plan.aim_authority) &&
           unit_interval(plan.response_confidence) &&
           std::isfinite(plan.response_scale) &&
           plan.horizon_count <= kMaxPlanHorizonSamples;
}

}  // namespace pipeline_contract
