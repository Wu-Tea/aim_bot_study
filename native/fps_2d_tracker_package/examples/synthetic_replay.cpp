#include <fps_tracker/ego_motion_buffer.hpp>
#include <fps_tracker/projection_model.hpp>
#include <fps_tracker/target_tracker.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

namespace {

struct QueuedFrame {
    fps::VisionFrame frame;
};

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double idx = p * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(idx));
    const auto hi = static_cast<std::size_t>(std::ceil(idx));
    if (lo == hi) return values[lo];
    const double t = idx - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}

} // namespace

int main() {
    fps::TrackerConfig cfg;
    cfg.projection.screenSizePx = {1920.0, 1080.0};
    cfg.projection.baseFocalPx = {960.0, 540.0};
    cfg.stick.deadzone = 0.03;
    cfg.stick.hipfireRateTrackUnitsPerSec = {2.2, 1.65};
    cfg.measurementSigmaBaseTrack = 0.0045;
    cfg.processNoiseBase = 0.10;
    cfg.fireMaxCaptureAgeSec = 0.020;

    fps::TargetTracker tracker(cfg);
    fps::ProjectionModel projection(cfg.projection);
    fps::EgoMotionBuffer truthEgo(fps::StickProjector(cfg.stick), fps::RecoilVisualModel(cfg.recoil));

    std::mt19937 rng(7);
    std::normal_distribution<double> pixNoise(0.0, 2.0);
    std::bernoulli_distribution missRandom(0.08);

    const double simDuration = 3.0;
    const double controlHz = 160.0;
    const double visionHz = 100.0;
    const double dtControl = 1.0 / controlHz;
    const double dtVision = 1.0 / visionHz;
    const double detectorLatency = 0.016;

    std::deque<QueuedFrame> pending;
    std::vector<double> errorsPx;
    int firePredictedOnlyViolations = 0;
    int framesIngested = 0;
    int framesMissed = 0;

    std::uint64_t frameSeq = 1;
    fps::DetectionId detId = 1;
    double nextVisionCapture = 0.0;

    fps::ModeState mode;
    mode.ads = true;
    mode.zoom = 1.25;
    mode.sensitivity = 1.0;

    auto trueCompensatedTarget = [](double t) {
        // Constant-velocity target in compensated 2D track coordinate.
        const double strafe = 0.055 * std::sin(t * 2.3);
        return fps::Vec2{0.13 + strafe + 0.035 * t, -0.04 + 0.025 * std::sin(t * 1.7)};
    };

    for (double now = 0.0; now <= simDuration; now += dtControl) {
        fps::ControlSample cs;
        cs.sendTime = now;
        cs.applyTime = now;
        cs.mode = mode;
        // Synthetic final stick sweep. This is the already saturated/clamped final output.
        cs.finalRightStick = {0.18 * std::sin(now * 2.0), 0.06 * std::cos(now * 1.5)};
        tracker.pushFinalControlSample(cs);
        truthEgo.push(cs);

        while (nextVisionCapture <= now + 1e-9) {
            const bool missing = missRandom(rng) || (nextVisionCapture > 1.20 && nextVisionCapture < 1.24);
            fps::VisionFrame vf;
            vf.frameSeq = frameSeq++;
            vf.captureTime = nextVisionCapture;
            vf.readyTime = nextVisionCapture + detectorLatency;
            vf.mode = mode;

            if (!missing) {
                const fps::Vec2 Ecap = truthEgo.cumulative(vf.captureTime);
                const fps::Vec2 pTrack = trueCompensatedTarget(vf.captureTime) - Ecap;
                fps::Vec2 bodyPx = projection.trackToScreen(pTrack, mode);
                bodyPx.x += pixNoise(rng);
                bodyPx.y += pixNoise(rng);

                fps::Detection d;
                d.id = detId++;
                d.cls = fps::TargetClass::Player;
                d.tier = fps::TargetTier::UpperBody;
                d.confidence = 0.82;
                d.bodyCenterPx = bodyPx;
                d.bodyBoxPx = fps::Box2::fromCenterSize(bodyPx, {78.0, 165.0});
                d.aimPointPx = {bodyPx.x, bodyPx.y - 42.0};
                d.validAimPoint = true;
                vf.detections.push_back(d);
            } else {
                framesMissed += 1;
            }

            pending.push_back({vf});
            nextVisionCapture += dtVision;
        }

        while (!pending.empty() && pending.front().frame.readyTime <= now + 1e-9) {
            tracker.ingestVisionFrame(pending.front().frame);
            pending.pop_front();
            framesIngested += 1;
        }

        const fps::TrackerOutput out = tracker.query(now);
        if (out.hasSelection) {
            const fps::Vec2 Enow = truthEgo.cumulative(now);
            const fps::Vec2 trueScreenTrack = trueCompensatedTarget(now) - Enow;
            const fps::Vec2 trueAimPx = projection.trackToScreen(trueScreenTrack, mode) - projection.screenCenterPx();
            const double err = (out.selected.bodyErrorPx - trueAimPx).length();
            errorsPx.push_back(err);

            if (out.selected.predictedOnly && out.selected.fireAuthority != fps::FireAuthority::None) {
                firePredictedOnlyViolations += 1;
            }
        }
    }

    std::cout << "synthetic_replay results\n";
    std::cout << "frames_ingested=" << framesIngested << " frames_missing=" << framesMissed << "\n";
    std::cout << "samples=" << errorsPx.size()
              << " p50_error_px=" << percentile(errorsPx, 0.50)
              << " p95_error_px=" << percentile(errorsPx, 0.95)
              << " p99_error_px=" << percentile(errorsPx, 0.99) << "\n";
    std::cout << "predicted_only_fire_violations=" << firePredictedOnlyViolations << "\n";

    return firePredictedOnlyViolations == 0 ? 0 : 2;
}
