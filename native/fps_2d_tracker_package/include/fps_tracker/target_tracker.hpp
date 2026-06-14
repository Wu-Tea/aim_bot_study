#pragma once

#include <vector>

#include "fps_tracker/association.hpp"
#include "fps_tracker/ego_motion_buffer.hpp"
#include "fps_tracker/projection_model.hpp"

namespace fps {

struct TrackDebugInfo {
    TrackId id = kInvalidTrackId;
    TrackLife life = TrackLife::Lost;
    Vec2 compensatedPosition{};
    Vec2 compensatedVelocity{};
    Vec2 lastInnovation{};
    double lastMahalanobisD2 = 0.0;
    double lastAssociationCost = 0.0;
    double confidence = 0.0;
    int hitStreak = 0;
    int missStreak = 0;
};

namespace detail {

struct TrackRecord {
    TrackId id = kInvalidTrackId;
    TrackLife life = TrackLife::Tentative;
    int ageFrames = 0;
    int hitStreak = 0;
    int missStreak = 0;
    int totalHits = 0;

    KalmanCv2d filter{};

    TimeSec lastObsCaptureTime = -1.0;
    TimeSec lastObsReadyTime = -1.0;
    std::uint64_t lastObsFrameSeq = 0;
    DetectionId lastMatchedDetectionId = kInvalidDetectionId;

    Vec2 boxSizePx{40.0, 80.0};
    Vec2 aimOffsetTrack{0.0, -0.05};
    TargetTier tier = TargetTier::Unknown;
    TargetClass cls = TargetClass::Player;

    double detConfEwma = 0.0;
    double assocQualityEwma = 0.0;
    double innovationEwma = 0.0;
    double ambiguity = 1.0;
    double switchRisk = 0.0;
    double confidence = 0.0;

    bool observedInLatestVisionFrame = false;
    bool hasDirectObservationToken = false;

    Vec2 lastInnovation{};
    double lastMahalanobisD2 = 0.0;
    double lastAssociationCost = 0.0;
};

} // namespace detail

class TargetTracker {
public:
    explicit TargetTracker(TrackerConfig cfg = {});

    void pushFinalControlSample(const ControlSample& sample);
    void ingestVisionFrame(const VisionFrame& frame);

    [[nodiscard]] TrackerOutput query(TimeSec queryTime,
                                       const SelectionRequest& request = {}) const;

    [[nodiscard]] std::vector<TrackDebugInfo> debugTracks() const;
    [[nodiscard]] const TrackerConfig& config() const { return cfg_; }

private:
    using Track = detail::TrackRecord;

    [[nodiscard]] Mat2 measurementNoise(const Detection& d) const;
    [[nodiscard]] double adaptiveProcessNoise(const Track& tr) const;
    [[nodiscard]] TrackSnapshot makeSnapshot(const Track& tr, TimeSec queryTime) const;
    [[nodiscard]] AssistAuthority computeAssistAuthority(const Track& tr,
                                                         const TrackSnapshot& s) const;
    [[nodiscard]] FireAuthority computeFireAuthority(const Track& tr,
                                                     const TrackSnapshot& s) const;
    [[nodiscard]] double computeConfidence(const Track& tr, TimeSec queryTime) const;
    [[nodiscard]] bool isPredictedOnly(const Track& tr, TimeSec queryTime) const;
    [[nodiscard]] Box2 predictedBoxPx(const Track& tr, Vec2 bodyTrackScreenLike,
                                      const ModeState& mode) const;

    void spawnTrack(const AssociationMeasurement& m, const VisionFrame& frame);
    void eraseLostTracks(TimeSec now);
    [[nodiscard]] ModeState modeAt(TimeSec /*time*/) const { return latestMode_; }

    TrackerConfig cfg_{};
    ProjectionModel projection_;
    EgoMotionBuffer ego_;
    Associator associator_;

    std::vector<Track> tracks_;
    TrackId nextTrackId_ = 1;
    std::uint64_t latestUsableFrameSeq_ = 0;
    TimeSec latestCaptureTime_ = 0.0;
    ModeState latestMode_{};
};

} // namespace fps
