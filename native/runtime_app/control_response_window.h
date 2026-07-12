#pragma once

#include "telemetry_schema.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace runtime_app {

enum class ResponseWindowReason : std::uint8_t {
    None,
    TargetChanged,
    IdentityWeak,
    GeometryChanged,
    SampleGap,
    TimingInvalid,
};

struct ResponseVisionFrame {
    std::uint64_t frame_id = 0;
    std::uint64_t captured_at_ns = 0;
    std::uint64_t inferred_at_ns = 0;
    std::uint64_t controller_consume_ns = 0;
    int frame_width = 0;
    int frame_height = 0;
    std::uint64_t target_track_id = 0;
    TargetIdentityQuality identity_quality = TargetIdentityQuality::None;
    bool live = false;
    float dx = 0.0f;
    float dy = 0.0f;
    float predicted_motion_x = 0.0f;
    float predicted_motion_y = 0.0f;
};

struct ResponseControllerSample {
    std::uint64_t sample_seq = 0;
    std::uint64_t output_sent_ns = 0;
    float physical_x = 0.0f, physical_y = 0.0f;
    float manual_x = 0.0f, manual_y = 0.0f;
    float ai_x = 0.0f, ai_y = 0.0f;
    float pre_recoil_x = 0.0f, pre_recoil_y = 0.0f;
    float recoil_x = 0.0f, recoil_y = 0.0f;
    float final_x = 0.0f, final_y = 0.0f;
};

struct ControlResponseWindow {
    std::uint64_t frame_id_before = 0;
    std::uint64_t frame_id_after = 0;
    std::uint64_t target_track_id = 0;
    float delta_error_x = 0.0f, delta_error_y = 0.0f;
    float residual_x = 0.0f, residual_y = 0.0f;
    float physical_x_integral = 0.0f, physical_y_integral = 0.0f;
    float manual_x_integral = 0.0f, manual_y_integral = 0.0f;
    float ai_x_integral = 0.0f, ai_y_integral = 0.0f;
    float pre_recoil_x_integral = 0.0f, pre_recoil_y_integral = 0.0f;
    float recoil_x_integral = 0.0f, recoil_y_integral = 0.0f;
    float final_x_integral = 0.0f, final_y_integral = 0.0f;
    TelemetryCompleteness completeness;
    TelemetryReadiness readiness = TelemetryReadiness::Diagnostic;
    ResponseWindowReason reason = ResponseWindowReason::None;
};

class ControlResponseWindowAssembler {
public:
    void observe_controller(const ResponseControllerSample& sample) noexcept;
    std::optional<ControlResponseWindow> observe_vision(
        const ResponseVisionFrame& frame) noexcept;
    void reset() noexcept;

private:
    static constexpr std::size_t kMaxCommands = 512;
    bool high_quality(TargetIdentityQuality quality) const noexcept;

    ResponseVisionFrame anchor_;
    bool has_anchor_ = false;
    std::array<ResponseControllerSample, kMaxCommands> commands_{};
    std::size_t command_count_ = 0;
    std::uint32_t overflow_ = 0;
};

} // namespace runtime_app
