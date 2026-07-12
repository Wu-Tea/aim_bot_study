#pragma once

#include "telemetry_schema.h"

#include <cstdint>

namespace runtime_app {

struct TargetIdentityObservation {
    std::uint64_t frame_id = 0;
    int frame_width = 0;
    int frame_height = 0;
    bool live = false;
    bool projected = false;
    bool explicit_switch = false;
    bool production_associated = false;
    bool association_ambiguous = false;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
};

struct TargetIdentityResult {
    std::uint64_t track_id = 0;
    std::uint64_t previous_track_id = 0;
    TargetIdentityQuality quality = TargetIdentityQuality::None;
    TargetEventKind event = TargetEventKind::None;

    bool model_eligible() const noexcept {
        return quality == TargetIdentityQuality::ProductionAssociated ||
            quality == TargetIdentityQuality::StrongGeometricMatch;
    }
};

class TelemetryTargetIdentity {
public:
    TargetIdentityResult observe(const TargetIdentityObservation& observation) noexcept;
    void reset() noexcept;

private:
    std::uint64_t issue_id() noexcept;
    bool geometry_compatible(const TargetIdentityObservation& observation) const noexcept;
    bool strong_match(const TargetIdentityObservation& observation) const noexcept;

    std::uint64_t next_id_ = 1;
    std::uint64_t active_id_ = 0;
    TargetIdentityObservation last_live_;
    bool has_last_live_ = false;
    bool lost_ = false;
};

} // namespace runtime_app
