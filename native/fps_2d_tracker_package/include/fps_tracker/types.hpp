#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "fps_tracker/math.hpp"

namespace fps {

using TrackId = std::uint64_t;
using DetectionId = std::uint64_t;
using WeaponId = std::uint32_t;

inline constexpr TrackId kInvalidTrackId = 0;
inline constexpr DetectionId kInvalidDetectionId = 0;

struct ModeState {
    bool ads = false;
    double zoom = 1.0;           // 1.0 hipfire; >1.0 scoped/zoomed.
    double sensitivity = 1.0;    // User/game sensitivity multiplier.
    double fovScaleX = 1.0;      // Optional calibrated projection scale.
    double fovScaleY = 1.0;
};

struct RoiTransform {
    Vec2 originPx{0.0, 0.0};
    Vec2 sizePx{0.0, 0.0};
};

enum class TargetClass : std::uint8_t {
    Unknown = 0,
    Player = 1,
    Body = 2,
    Head = 3,
    Vehicle = 4,
};

enum class TargetTier : std::uint8_t {
    Unknown = 0,
    Low = 1,
    Body = 2,
    UpperBody = 3,
    Head = 4,
};

enum class TrackLife : std::uint8_t {
    Tentative = 0,
    Confirmed = 1,
    Coasting = 2,
    Lost = 3,
};

enum class AssistAuthority : std::uint8_t {
    None = 0,
    AimObserved = 1,
    AimCoast = 2,
};

enum class FireAuthority : std::uint8_t {
    None = 0,
    ObservedOnly = 1,
};

struct Detection {
    DetectionId id = kInvalidDetectionId;
    TargetClass cls = TargetClass::Player;
    TargetTier tier = TargetTier::Unknown;
    double confidence = 0.0;

    Box2 bodyBoxPx{};
    Vec2 bodyCenterPx{};
    Vec2 aimPointPx{};
    bool validAimPoint = false;
};

struct VisionFrame {
    std::uint64_t frameSeq = 0;
    TimeSec captureTime = 0.0;
    TimeSec readyTime = 0.0;
    RoiTransform roi{};
    ModeState mode{};
    std::vector<Detection> detections;
};

struct ControlSample {
    TimeSec sendTime = 0.0;
    TimeSec applyTime = 0.0;
    Vec2 finalRightStick{}; // after clamp/mix/deadzone/saturation.
    ModeState mode{};

    bool fireButton = false;
    WeaponId weapon = 0;
    std::uint32_t shotIndex = 0;
};

struct SelectionRequest {
    // Optional current target hysteresis. Association also has bounded hysteresis,
    // but selector can use this to avoid flicker.
    TrackId preferredTrackId = kInvalidTrackId;
    Vec2 desiredAimPx{0.0, 0.0};
    double maxAimErrorPx = std::numeric_limits<double>::infinity();
};

struct TrackSnapshot {
    TrackId id = kInvalidTrackId;
    TrackLife life = TrackLife::Lost;

    Vec2 bodyErrorPx{};
    Vec2 aimErrorPx{};
    Box2 predictedBoxPx{};
    Vec2 velocityTrackUnits{};

    double confidence = 0.0;
    double positionSigma = 0.0;
    TimeSec lastObservedCaptureTime = -1.0;
    double obsAgeMs = std::numeric_limits<double>::infinity();
    double ambiguity = 1.0;
    double associationQuality = 0.0;
    double lastDetectorConfidence = 0.0;

    bool observedRecent = false;
    bool predictedOnly = true;

    AssistAuthority assistAuthority = AssistAuthority::None;
    FireAuthority fireAuthority = FireAuthority::None;

    DetectionId backingDetectionId = kInvalidDetectionId;
    std::uint64_t backingFrameSeq = 0;
};

struct TrackerOutput {
    bool hasSelection = false;
    TrackSnapshot selected{};
    std::vector<TrackSnapshot> candidates;
};

struct ProjectionConfig {
    Vec2 screenSizePx{1920.0, 1080.0};
    // Focal-like denominator. Larger focal means one pixel is a smaller track unit.
    Vec2 baseFocalPx{960.0, 540.0};
};

struct StickProjectorConfig {
    double deadzone = 0.05;
    double antiDeadzone = 0.0;
    double responseExponent = 1.0;
    Vec2 hipfireRateTrackUnitsPerSec{2.4, 1.8};
    double adsRateMultiplier = 0.55;
    double zoomRatePower = -1.0; // rate *= pow(zoom, zoomRatePower)
};

struct RecoilVisualConfig {
    bool enabled = false;
    Vec2 baseKickTrackUnits{0.0, 0.0};
    double shotIndexScale = 0.0;
    double riseTimeSec = 0.010;
    double decayTimeSec = 0.090;
    double eventTtlSec = 0.350;
};

struct AssociationConfig {
    double gateD2 = 9.21;              // approx chi-square 2D 99%.
    double maxPixelDistance = 260.0;
    double wPos = 0.55;
    double wIou = 0.25;
    double wSize = 0.12;
    double wAspect = 0.05;
    double wTier = 0.04;
    double wConfidence = 0.08;
    double matchCostThreshold = 0.95;
};

struct TrackerConfig {
    ProjectionConfig projection{};
    StickProjectorConfig stick{};
    RecoilVisualConfig recoil{};
    AssociationConfig association{};

    int confirmHits = 2;
    double spawnMinConfidence = 0.25;
    double processNoiseBase = 0.15;
    double measurementSigmaBaseTrack = 0.006;
    double lowConfidenceSigmaScale = 3.5;
    double boxSizeAlpha = 0.30;
    double aimOffsetAlpha = 0.35;
    double confidenceAlpha = 0.35;
    double missDecayTauSec = 0.060;
    double maxCoastSec = 0.160;

    double minAssistConfidence = 0.18;
    double minFireConfidence = 0.45;
    double fireMaxCaptureAgeSec = 0.030;
    double fireMaxPositionSigmaTrack = 0.035;
    double fireMaxAmbiguity = 0.45;
};

} // namespace fps
