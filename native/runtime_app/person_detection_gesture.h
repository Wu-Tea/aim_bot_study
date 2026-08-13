#pragma once

#include "controller_native/aim_activation.h"
#include "pipeline_contract/target_plan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace runtime_app {

enum class PersonMarkBlockReason : unsigned char {
    None,
    NoPendingRequest,
    WaitingForFreshPlan,
    RequestExpired,
    NoTarget,
    NotDirectPerson,
    CueContinuation,
    NoCurrentEnemyCue,
    InvalidPlan,
    InvalidAimRegion,
    CrosshairOutsideAimRegion,
    AwaitingConfirmation,
    AlreadyMarkedGeneration,
};

inline const char* person_mark_block_reason_name(
    PersonMarkBlockReason reason) noexcept {
    switch (reason) {
    case PersonMarkBlockReason::None: return "none";
    case PersonMarkBlockReason::NoPendingRequest: return "no_request";
    case PersonMarkBlockReason::WaitingForFreshPlan: return "waiting_fresh_plan";
    case PersonMarkBlockReason::RequestExpired: return "request_expired";
    case PersonMarkBlockReason::NoTarget: return "no_target";
    case PersonMarkBlockReason::NotDirectPerson: return "not_direct_person";
    case PersonMarkBlockReason::CueContinuation: return "cue_continuation";
    case PersonMarkBlockReason::NoCurrentEnemyCue: return "no_current_enemy_cue";
    case PersonMarkBlockReason::InvalidPlan: return "invalid_plan";
    case PersonMarkBlockReason::InvalidAimRegion: return "invalid_aim_region";
    case PersonMarkBlockReason::CrosshairOutsideAimRegion:
        return "crosshair_outside_r";
    case PersonMarkBlockReason::AwaitingConfirmation:
        return "awaiting_confirmation";
    case PersonMarkBlockReason::AlreadyMarkedGeneration:
        return "already_marked_generation";
    }
    return "invalid";
}

struct PersonDetectionGestureStatus {
    bool request_pending = false;
    bool synthetic_pressed = false;
    bool fired_this_tick = false;
    bool canceled_this_tick = false;
    std::uint32_t confirmation_frames = 0;
    std::uint64_t evaluated_scope = 0;
    std::uint64_t evaluated_generation = 0;
    std::uint64_t last_marked_scope = 0;
    std::uint64_t last_marked_generation = 0;
    PersonMarkBlockReason block_reason =
        PersonMarkBlockReason::NoPendingRequest;
};

// L3/LT creates a short-lived request. The request is consumed only by two
// consecutive fresh final TargetPlans that prove the crosshair ray currently
// intersects a direct, cue-confirmed enemy person's R. This class never owns
// target identity, geometry, freshness or aim authority.
class PersonDetectionGesture {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr auto kPressDuration = std::chrono::milliseconds(50);
    // This is request lifetime, not evidence lifetime. Old Vision/TargetPlan
    // evidence can never actuate because only observe_fresh_plan advances the
    // gate.
    static constexpr auto kRequestMaxAge = std::chrono::milliseconds(250);
    // L3 and LT are both discrete user gestures and may not continuously
    // reopen mark requests. Each carries its own cooldown: L3 is a thumb-click,
    // LT is ADS trigger acquisition, and either may fire while the other is
    // still cooling down.
    static constexpr auto kDefaultL3RequestCooldown =
        std::chrono::milliseconds(1000);
    static constexpr auto kDefaultLtRequestCooldown =
        std::chrono::milliseconds(1000);
    static constexpr std::uint32_t kRequiredFreshConfirmations = 2;

    explicit PersonDetectionGesture(
        std::chrono::milliseconds l3_request_cooldown =
            kDefaultL3RequestCooldown,
        std::chrono::milliseconds lt_request_cooldown =
            kDefaultLtRequestCooldown) noexcept
        : l3_request_cooldown_(std::max(
              l3_request_cooldown,
              std::chrono::milliseconds::zero())),
          lt_request_cooldown_(std::max(
              lt_request_cooldown,
              std::chrono::milliseconds::zero())) {}

    void reset() noexcept {
        pending_request_ = false;
        request_created_at_ = {};
        l3_pressed_ = false;
        lt_pressed_ = false;
        has_l3_request_time_ = false;
        last_l3_request_at_ = {};
        has_lt_request_time_ = false;
        last_lt_request_at_ = {};
        confirmation_scope_ = 0;
        confirmation_generation_ = 0;
        confirmation_frames_ = 0;
        evaluated_scope_ = 0;
        evaluated_generation_ = 0;
        last_marked_scope_ = 0;
        last_marked_generation_ = 0;
        press_until_ = {};
        fired_this_tick_ = false;
        canceled_this_tick_ = false;
        block_reason_ = PersonMarkBlockReason::NoPendingRequest;
    }

    void update_activation(
        bool l3_pressed,
        float left_trigger,
        Clock::time_point now) noexcept {
        fired_this_tick_ = false;
        canceled_this_tick_ = false;
        expire_request(now);

        const bool lt_pressed = lt_pressed_
            ? left_trigger > controller_native::kAimLeftTriggerIdleThreshold
            : left_trigger > controller_native::kAimLeftTriggerPressThreshold;
        const bool l3_request_edge = l3_pressed && !l3_pressed_;
        const bool lt_request_edge = lt_pressed && !lt_pressed_;
        l3_pressed_ = l3_pressed;
        lt_pressed_ = lt_pressed;

        const bool l3_cooldown_elapsed = !has_l3_request_time_ ||
            now < last_l3_request_at_ ||
            now - last_l3_request_at_ >= l3_request_cooldown_;
        const bool accept_l3_request =
            l3_request_edge && l3_cooldown_elapsed;
        const bool lt_cooldown_elapsed = !has_lt_request_time_ ||
            now < last_lt_request_at_ ||
            now - last_lt_request_at_ >= lt_request_cooldown_;
        const bool accept_lt_request =
            lt_request_edge && lt_cooldown_elapsed;
        if (!pending_request_ &&
            (accept_l3_request || accept_lt_request)) {
            pending_request_ = true;
            request_created_at_ = now;
            if (accept_l3_request) {
                has_l3_request_time_ = true;
                last_l3_request_at_ = now;
            }
            if (accept_lt_request) {
                has_lt_request_time_ = true;
                last_lt_request_at_ = now;
            }
            reset_confirmation();
            block_reason_ = PersonMarkBlockReason::WaitingForFreshPlan;
        }
    }

    // Call exactly once for each newly accepted Vision frame, after Controller
    // has built the final plan for that frame.
    bool observe_fresh_plan(
        const pipeline_contract::TargetPlan& plan,
        Clock::time_point now,
        std::uint64_t target_scope = 1) noexcept {
        expire_request(now);
        evaluated_scope_ = target_scope;
        evaluated_generation_ = plan.selector_target_generation;
        if (!pending_request_) {
            reset_confirmation();
            if (block_reason_ != PersonMarkBlockReason::RequestExpired &&
                block_reason_ !=
                    PersonMarkBlockReason::AlreadyMarkedGeneration) {
                block_reason_ = PersonMarkBlockReason::NoPendingRequest;
            }
            return false;
        }

        const std::uint64_t generation = plan.selector_target_generation;
        if (generation != 0 && target_scope == last_marked_scope_ &&
            generation == last_marked_generation_) {
            cancel_request(PersonMarkBlockReason::AlreadyMarkedGeneration);
            return false;
        }

        const PersonMarkBlockReason eligibility = markability_failure(plan);
        if (eligibility != PersonMarkBlockReason::None) {
            reset_confirmation();
            block_reason_ = eligibility;
            return false;
        }

        if (confirmation_scope_ != target_scope ||
            confirmation_generation_ != generation) {
            confirmation_scope_ = target_scope;
            confirmation_generation_ = generation;
            confirmation_frames_ = 1;
        } else if (confirmation_frames_ < UINT32_MAX) {
            ++confirmation_frames_;
        }
        if (confirmation_frames_ < kRequiredFreshConfirmations) {
            block_reason_ = PersonMarkBlockReason::AwaitingConfirmation;
            return false;
        }

        press_until_ = now + kPressDuration;
        last_marked_scope_ = target_scope;
        last_marked_generation_ = generation;
        pending_request_ = false;
        fired_this_tick_ = true;
        block_reason_ = PersonMarkBlockReason::None;
        reset_confirmation();
        return true;
    }

    bool merge_dpad_up(
        bool physical_dpad_up,
        Clock::time_point now) const noexcept {
        return physical_dpad_up || now < press_until_;
    }

    PersonDetectionGestureStatus status(Clock::time_point now) const noexcept {
        PersonDetectionGestureStatus value;
        value.request_pending = pending_request_ &&
            now >= request_created_at_ &&
            now - request_created_at_ <= kRequestMaxAge;
        value.synthetic_pressed = now < press_until_;
        value.fired_this_tick = fired_this_tick_;
        value.canceled_this_tick = canceled_this_tick_;
        value.confirmation_frames = confirmation_frames_;
        value.evaluated_scope = evaluated_scope_;
        value.evaluated_generation = evaluated_generation_;
        value.last_marked_scope = last_marked_scope_;
        value.last_marked_generation = last_marked_generation_;
        value.block_reason = block_reason_;
        return value;
    }

private:
    static bool valid_region(const common_native::Box2f& region) noexcept {
        return std::isfinite(region.x) && std::isfinite(region.y) &&
            std::isfinite(region.w) && std::isfinite(region.h) &&
            region.w > 0.0f && region.h > 0.0f;
    }

    static bool contains(
        const common_native::Box2f& region,
        pipeline_contract::Vec2f point) noexcept {
        constexpr float kTolerancePx = 0.001f;
        return point.x >= region.x - kTolerancePx &&
            point.x <= region.x + region.w + kTolerancePx &&
            point.y >= region.y - kTolerancePx &&
            point.y <= region.y + region.h + kTolerancePx;
    }

    static PersonMarkBlockReason markability_failure(
        const pipeline_contract::TargetPlan& plan) noexcept {
        if (plan.target_id == 0 || plan.selector_target_generation == 0 ||
            plan.lifecycle == pipeline_contract::TargetLifecycle::None) {
            return PersonMarkBlockReason::NoTarget;
        }
        if (plan.cue_continuation ||
            plan.lifecycle ==
                pipeline_contract::TargetLifecycle::CueContinuation) {
            return PersonMarkBlockReason::CueContinuation;
        }
        if (!plan.direct_person_observation ||
            plan.source_observation_id == 0 || plan.source_frame_id == 0) {
            return PersonMarkBlockReason::NotDirectPerson;
        }
        if (!plan.enemy_cue_current) {
            return PersonMarkBlockReason::NoCurrentEnemyCue;
        }
        if (!pipeline_contract::valid(plan)) {
            return PersonMarkBlockReason::InvalidPlan;
        }
        if (!plan.has_aim_region || !valid_region(plan.aim_region_px)) {
            return PersonMarkBlockReason::InvalidAimRegion;
        }
        const pipeline_contract::Vec2f crosshair{
            plan.aim_px.x - plan.error_px.x,
            plan.aim_px.y - plan.error_px.y,
        };
        if (!pipeline_contract::finite(crosshair) ||
            !contains(plan.aim_region_px, crosshair)) {
            return PersonMarkBlockReason::CrosshairOutsideAimRegion;
        }
        return PersonMarkBlockReason::None;
    }

    void reset_confirmation() noexcept {
        confirmation_scope_ = 0;
        confirmation_generation_ = 0;
        confirmation_frames_ = 0;
    }

    void cancel_request(PersonMarkBlockReason reason) noexcept {
        pending_request_ = false;
        canceled_this_tick_ = true;
        reset_confirmation();
        block_reason_ = reason;
    }

    void expire_request(Clock::time_point now) noexcept {
        if (pending_request_ && now >= request_created_at_ &&
            now - request_created_at_ > kRequestMaxAge) {
            cancel_request(PersonMarkBlockReason::RequestExpired);
        }
    }

    bool pending_request_ = false;
    Clock::time_point request_created_at_{};
    bool l3_pressed_ = false;
    bool lt_pressed_ = false;
    bool has_l3_request_time_ = false;
    Clock::time_point last_l3_request_at_{};
    std::chrono::milliseconds l3_request_cooldown_ =
        kDefaultL3RequestCooldown;
    bool has_lt_request_time_ = false;
    Clock::time_point last_lt_request_at_{};
    std::chrono::milliseconds lt_request_cooldown_ =
        kDefaultLtRequestCooldown;
    std::uint64_t confirmation_scope_ = 0;
    std::uint64_t confirmation_generation_ = 0;
    std::uint32_t confirmation_frames_ = 0;
    std::uint64_t evaluated_scope_ = 0;
    std::uint64_t evaluated_generation_ = 0;
    std::uint64_t last_marked_scope_ = 0;
    std::uint64_t last_marked_generation_ = 0;
    Clock::time_point press_until_{};
    bool fired_this_tick_ = false;
    bool canceled_this_tick_ = false;
    PersonMarkBlockReason block_reason_ =
        PersonMarkBlockReason::NoPendingRequest;
};

}  // namespace runtime_app
