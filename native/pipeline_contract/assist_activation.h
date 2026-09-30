#pragma once

namespace pipeline_contract {

// Input scope, search demand and actuator authority are separate contracts.
// Primed wakes target search before the physical ADS ready threshold is met.
enum class AssistActivation : unsigned char { Off, Primed, Engaged, Application };
constexpr bool permits_assist(AssistActivation state) noexcept {
    return state == AssistActivation::Engaged || state == AssistActivation::Application;
}
constexpr bool owns_ads_task(AssistActivation state) noexcept {
    return state == AssistActivation::Engaged;
}
constexpr bool requests_target_search(AssistActivation state) noexcept {
    return state != AssistActivation::Off;
}
enum class VisionRequest : unsigned char { Idle, DetectionOnly, AssistSearch };
constexpr bool requests_detection(VisionRequest request) noexcept {
    return request != VisionRequest::Idle;
}
constexpr VisionRequest vision_request(AssistActivation activation, bool marker_requested) noexcept {
    return requests_target_search(activation) ? VisionRequest::AssistSearch :
        marker_requested ? VisionRequest::DetectionOnly : VisionRequest::Idle;
}
}  // namespace pipeline_contract
