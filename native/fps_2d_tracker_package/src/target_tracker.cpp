#include "fps_tracker/target_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace fps {

TargetTracker::TargetTracker(TrackerConfig cfg)
    : cfg_(cfg),
      projection_(cfg_.projection),
      ego_(StickProjector(cfg_.stick), RecoilVisualModel(cfg_.recoil)),
      associator_(cfg_.association) {}

void TargetTracker::pushFinalControlSample(const ControlSample& sample) {
    ego_.push(sample);
    latestMode_ = sample.mode;
}

Mat2 TargetTracker::measurementNoise(const Detection& d) const {
    const double conf = clamp(d.confidence, 0.0, 1.0);
    const double lowConf = 1.0 - conf;

    // Smaller boxes tend to have noisier centers. Use pixel height through a mild penalty.
    const double h = std::max(d.bodyBoxPx.height(), 1.0);
    const double smallBoxPenalty = clamp(80.0 / h, 0.7, 3.0);

    const double sigma = cfg_.measurementSigmaBaseTrack *
        (1.0 + cfg_.lowConfidenceSigmaScale * lowConf) * smallBoxPenalty;
    return Mat2::diag(sigma * sigma, sigma * sigma);
}

double TargetTracker::adaptiveProcessNoise(const Track& tr) const {
    double q = cfg_.processNoiseBase;
    q *= 1.0 + 0.6 * static_cast<double>(std::max(0, tr.missStreak));
    q *= 1.0 + clamp(tr.innovationEwma / 0.05, 0.0, 3.0);
    return q;
}

void TargetTracker::ingestVisionFrame(const VisionFrame& frame) {
    latestUsableFrameSeq_ = frame.frameSeq;
    latestCaptureTime_ = frame.captureTime;
    latestMode_ = frame.mode;

    const Vec2 Ecap = ego_.cumulative(frame.captureTime);

    std::vector<AssociationMeasurement> measurements;
    measurements.reserve(frame.detections.size());
    for (std::size_t i = 0; i < frame.detections.size(); ++i) {
        const Detection& d = frame.detections[i];
        if (d.confidence < 0.01 || d.bodyBoxPx.area() <= 1.0) {
            continue;
        }

        const Vec2 bodyTrack = projection_.screenToTrack(d.bodyCenterPx, frame.mode);
        const Vec2 aimTrack = d.validAimPoint ? projection_.screenToTrack(d.aimPointPx, frame.mode) : bodyTrack;
        AssociationMeasurement measurement;
        measurement.detectionIndex = measurements.size();
        measurement.detection = d;
        measurement.bodyCompTrack = bodyTrack + Ecap;
        measurement.aimOffsetTrack = aimTrack - bodyTrack;
        measurement.R = measurementNoise(d);
        measurements.push_back(measurement);
    }

    for (Track& tr : tracks_) {
        tr.observedInLatestVisionFrame = false;
        tr.hasDirectObservationToken = false;
        tr.filter.predictTo(frame.captureTime, adaptiveProcessNoise(tr));
        tr.ageFrames += 1;
    }

    std::vector<AssociationTrackView> views;
    views.reserve(tracks_.size());
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        const Track& tr = tracks_[i];
        if (tr.life == TrackLife::Lost) {
            continue;
        }
        const Vec2 bodyScreenLikeTrack = tr.filter.position() - Ecap;
        AssociationTrackView view;
        view.trackIndex = i;
        view.trackId = tr.id;
        view.predictedBodyCompTrack = tr.filter.position();
        view.predictedPosCov = tr.filter.positionCovariance();
        view.predictedBoxPx = predictedBoxPx(tr, bodyScreenLikeTrack, frame.mode);
        view.boxSizePx = tr.boxSizePx;
        view.cls = tr.cls;
        view.tier = tr.tier;
        view.confirmed = tr.life == TrackLife::Confirmed || tr.life == TrackLife::Coasting;
        views.push_back(view);
    }

    const AssociationResult assoc = associator_.match(views, measurements);

    std::vector<bool> matchedTrack(tracks_.size(), false);
    std::vector<bool> matchedMeasurement(measurements.size(), false);

    for (const AssociationPair& pair : assoc.matches) {
        if (pair.trackIndex >= tracks_.size() || pair.measurementIndex >= measurements.size()) {
            continue;
        }
        Track& tr = tracks_[pair.trackIndex];
        const AssociationMeasurement& m = measurements[pair.measurementIndex];

        const Vec2 before = tr.filter.position();
        tr.filter.updatePosition(m.bodyCompTrack, m.R);
        const Vec2 innovation = m.bodyCompTrack - before;

        tr.lastInnovation = innovation;
        tr.lastMahalanobisD2 = pair.d2;
        tr.lastAssociationCost = pair.cost;
        tr.innovationEwma = 0.75 * tr.innovationEwma + 0.25 * innovation.length();
        tr.assocQualityEwma = 0.70 * tr.assocQualityEwma + 0.30 * (1.0 - clamp(pair.cost, 0.0, 1.0));
        tr.detConfEwma = (tr.totalHits == 0) ? m.detection.confidence
                                             : (1.0 - cfg_.confidenceAlpha) * tr.detConfEwma + cfg_.confidenceAlpha * m.detection.confidence;
        tr.confidence = computeConfidence(tr, frame.captureTime);

        tr.boxSizePx = lerp(tr.boxSizePx,
                            {m.detection.bodyBoxPx.width(), m.detection.bodyBoxPx.height()},
                            cfg_.boxSizeAlpha);
        tr.aimOffsetTrack = lerp(tr.aimOffsetTrack, m.aimOffsetTrack, cfg_.aimOffsetAlpha);
        tr.tier = m.detection.tier;
        tr.cls = m.detection.cls;

        tr.lastObsCaptureTime = frame.captureTime;
        tr.lastObsReadyTime = frame.readyTime;
        tr.lastObsFrameSeq = frame.frameSeq;
        tr.lastMatchedDetectionId = m.detection.id;
        tr.observedInLatestVisionFrame = true;
        tr.hasDirectObservationToken = true;
        tr.hitStreak += 1;
        tr.missStreak = 0;
        tr.totalHits += 1;
        tr.ambiguity = clamp(pair.cost, 0.0, 1.0);

        if (tr.totalHits >= cfg_.confirmHits) {
            tr.life = TrackLife::Confirmed;
        } else {
            tr.life = TrackLife::Tentative;
        }

        matchedTrack[pair.trackIndex] = true;
        matchedMeasurement[pair.measurementIndex] = true;
    }

    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        if (matchedTrack[i]) {
            continue;
        }
        Track& tr = tracks_[i];
        if (tr.life == TrackLife::Lost) {
            continue;
        }

        tr.missStreak += 1;
        tr.hitStreak = 0;
        tr.hasDirectObservationToken = false;
        tr.observedInLatestVisionFrame = false;
        tr.confidence = computeConfidence(tr, frame.captureTime);

        const double age = frame.captureTime - tr.lastObsCaptureTime;
        if (age <= cfg_.maxCoastSec && tr.totalHits >= cfg_.confirmHits) {
            tr.life = TrackLife::Coasting;
        } else if (age > cfg_.maxCoastSec) {
            tr.life = TrackLife::Lost;
        }
    }

    for (std::size_t i = 0; i < measurements.size(); ++i) {
        if (matchedMeasurement[i]) {
            continue;
        }
        if (measurements[i].detection.confidence >= cfg_.spawnMinConfidence) {
            spawnTrack(measurements[i], frame);
        }
    }

    eraseLostTracks(frame.captureTime);
}

void TargetTracker::spawnTrack(const AssociationMeasurement& m, const VisionFrame& frame) {
    Track tr;
    tr.id = nextTrackId_++;
    tr.life = TrackLife::Tentative;
    tr.ageFrames = 1;
    tr.hitStreak = 1;
    tr.missStreak = 0;
    tr.totalHits = 1;
    tr.filter.reset(m.bodyCompTrack, frame.captureTime,
                    cfg_.measurementSigmaBaseTrack * cfg_.measurementSigmaBaseTrack,
                    0.20);
    tr.boxSizePx = {m.detection.bodyBoxPx.width(), m.detection.bodyBoxPx.height()};
    tr.aimOffsetTrack = m.aimOffsetTrack;
    tr.tier = m.detection.tier;
    tr.cls = m.detection.cls;
    tr.detConfEwma = m.detection.confidence;
    tr.assocQualityEwma = 0.5;
    tr.innovationEwma = 0.0;
    tr.ambiguity = 0.5;
    tr.confidence = m.detection.confidence * 0.5;
    tr.lastObsCaptureTime = frame.captureTime;
    tr.lastObsReadyTime = frame.readyTime;
    tr.lastObsFrameSeq = frame.frameSeq;
    tr.lastMatchedDetectionId = m.detection.id;
    tr.observedInLatestVisionFrame = true;
    tr.hasDirectObservationToken = true;
    tracks_.push_back(tr);
}

void TargetTracker::eraseLostTracks(TimeSec now) {
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& tr) {
        return tr.life == TrackLife::Lost ||
               (tr.lastObsCaptureTime >= 0.0 && now - tr.lastObsCaptureTime > cfg_.maxCoastSec * 1.5);
    }), tracks_.end());
}

double TargetTracker::computeConfidence(const Track& tr, TimeSec queryTime) const {
    if (tr.lastObsCaptureTime < 0.0) {
        return 0.0;
    }
    const double obsAge = std::max(0.0, queryTime - tr.lastObsCaptureTime);
    const double freshness = std::exp(-obsAge / std::max(cfg_.missDecayTauSec, 1e-4));
    const double stability = std::exp(-clamp(tr.innovationEwma / 0.08, 0.0, 5.0));
    const double assoc = clamp(0.30 + tr.assocQualityEwma, 0.0, 1.0);
    const double ambiguityPenalty = 1.0 - 0.6 * clamp(tr.ambiguity, 0.0, 1.0);
    const double base = clamp(tr.detConfEwma, 0.0, 1.0);
    const double lifeMul = (tr.life == TrackLife::Tentative) ? 0.65 : 1.0;
    return clamp(base * freshness * (0.35 + 0.65 * stability) * assoc * ambiguityPenalty * lifeMul, 0.0, 1.0);
}

bool TargetTracker::isPredictedOnly(const Track& tr, TimeSec /*queryTime*/) const {
    if (!tr.hasDirectObservationToken) {
        return true;
    }
    if (tr.missStreak > 0) {
        return true;
    }
    if (tr.lastObsFrameSeq != latestUsableFrameSeq_) {
        return true;
    }
    if (tr.lastMatchedDetectionId == kInvalidDetectionId) {
        return true;
    }
    return false;
}

Box2 TargetTracker::predictedBoxPx(const Track& tr, Vec2 bodyTrackScreenLike, const ModeState& mode) const {
    return projection_.projectBox(bodyTrackScreenLike, tr.boxSizePx, mode);
}

AssistAuthority TargetTracker::computeAssistAuthority(const Track& tr, const TrackSnapshot& s) const {
    if (tr.life == TrackLife::Lost || s.confidence < cfg_.minAssistConfidence) {
        return AssistAuthority::None;
    }
    if (!s.predictedOnly && tr.life == TrackLife::Confirmed) {
        return AssistAuthority::AimObserved;
    }
    if (tr.life == TrackLife::Coasting && s.obsAgeMs * 0.001 <= cfg_.maxCoastSec) {
        return AssistAuthority::AimCoast;
    }
    return AssistAuthority::None;
}

FireAuthority TargetTracker::computeFireAuthority(const Track& tr, const TrackSnapshot& s) const {
    if (tr.life != TrackLife::Confirmed) return FireAuthority::None;
    if (s.predictedOnly) return FireAuthority::None;
    if (tr.missStreak > 0) return FireAuthority::None;
    if (!tr.hasDirectObservationToken) return FireAuthority::None;
    if (tr.lastMatchedDetectionId == kInvalidDetectionId) return FireAuthority::None;
    if (tr.lastObsFrameSeq != latestUsableFrameSeq_) return FireAuthority::None;
    if (s.confidence < cfg_.minFireConfidence) return FireAuthority::None;
    if (s.obsAgeMs * 0.001 > cfg_.fireMaxCaptureAgeSec) return FireAuthority::None;
    if (s.positionSigma > cfg_.fireMaxPositionSigmaTrack) return FireAuthority::None;
    if (s.ambiguity > cfg_.fireMaxAmbiguity) return FireAuthority::None;
    return FireAuthority::ObservedOnly;
}

TrackSnapshot TargetTracker::makeSnapshot(const Track& tr, TimeSec queryTime) const {
    KalmanCv2d pred = tr.filter;
    pred.predictTo(queryTime, adaptiveProcessNoise(tr));

    const ModeState mode = modeAt(queryTime);
    const Vec2 E = ego_.cumulative(queryTime);
    const Vec2 bodyTrackScreenLike = pred.position() - E;
    const Vec2 aimTrackScreenLike = bodyTrackScreenLike + tr.aimOffsetTrack;

    TrackSnapshot s;
    s.id = tr.id;
    s.life = tr.life;
    s.bodyErrorPx = projection_.trackToScreen(bodyTrackScreenLike, mode) - projection_.screenCenterPx();
    s.aimErrorPx = projection_.trackToScreen(aimTrackScreenLike, mode) - projection_.screenCenterPx();
    s.predictedBoxPx = predictedBoxPx(tr, bodyTrackScreenLike, mode);
    s.velocityTrackUnits = pred.velocity();
    s.confidence = computeConfidence(tr, queryTime);
    s.positionSigma = pred.positionSigma();
    s.obsAgeMs = tr.lastObsCaptureTime >= 0.0 ? std::max(0.0, queryTime - tr.lastObsCaptureTime) * 1000.0
                                               : std::numeric_limits<double>::infinity();
    s.ambiguity = tr.ambiguity;
    s.lastDetectorConfidence = tr.detConfEwma;
    s.observedRecent = tr.hasDirectObservationToken && tr.lastObsFrameSeq == latestUsableFrameSeq_;
    s.predictedOnly = isPredictedOnly(tr, queryTime);
    s.backingDetectionId = tr.lastMatchedDetectionId;
    s.backingFrameSeq = tr.lastObsFrameSeq;
    s.assistAuthority = computeAssistAuthority(tr, s);
    s.fireAuthority = computeFireAuthority(tr, s);
    return s;
}

TrackerOutput TargetTracker::query(TimeSec queryTime, const SelectionRequest& request) const {
    TrackerOutput out;
    out.candidates.reserve(tracks_.size());
    for (const Track& tr : tracks_) {
        if (tr.life == TrackLife::Lost) {
            continue;
        }
        TrackSnapshot s = makeSnapshot(tr, queryTime);
        if (s.assistAuthority == AssistAuthority::None && s.confidence < cfg_.minAssistConfidence * 0.5) {
            continue;
        }
        out.candidates.push_back(s);
    }

    double bestScore = -std::numeric_limits<double>::infinity();
    for (const TrackSnapshot& s : out.candidates) {
        const double aimErr = s.aimErrorPx.length();
        if (aimErr > request.maxAimErrorPx) {
            continue;
        }

        double authorityBonus = 0.0;
        if (s.assistAuthority == AssistAuthority::AimObserved) authorityBonus = 0.35;
        if (s.assistAuthority == AssistAuthority::AimCoast) authorityBonus = 0.08;

        const double preferredBonus = (request.preferredTrackId != kInvalidTrackId && s.id == request.preferredTrackId) ? 0.08 : 0.0;
        const double centerScore = 1.0 / (1.0 + aimErr / 120.0);
        const double score = s.confidence + authorityBonus + preferredBonus + 0.25 * centerScore - 0.25 * s.ambiguity;

        if (score > bestScore) {
            bestScore = score;
            out.selected = s;
            out.hasSelection = true;
        }
    }

    return out;
}

std::vector<TrackDebugInfo> TargetTracker::debugTracks() const {
    std::vector<TrackDebugInfo> dbg;
    dbg.reserve(tracks_.size());
    for (const Track& tr : tracks_) {
        TrackDebugInfo info;
        info.id = tr.id;
        info.life = tr.life;
        info.compensatedPosition = tr.filter.position();
        info.compensatedVelocity = tr.filter.velocity();
        info.lastInnovation = tr.lastInnovation;
        info.lastMahalanobisD2 = tr.lastMahalanobisD2;
        info.lastAssociationCost = tr.lastAssociationCost;
        info.confidence = tr.confidence;
        info.hitStreak = tr.hitStreak;
        info.missStreak = tr.missStreak;
        dbg.push_back(info);
    }
    return dbg;
}

} // namespace fps
