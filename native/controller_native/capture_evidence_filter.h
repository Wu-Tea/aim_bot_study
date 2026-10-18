#pragma once

#include "pipeline_contract/target_plan.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace controller_native {

// Noise belongs to capture evidence, not to actuator amplitude. Fit the recent
// trajectory after removing the camera displacement actually delivered over
// each capture interval. The fit is evaluated AT the newest capture: constant
// offsets and constant target velocity pass without a dead band or time shift.
// Source anatomy remains immutable; only control measurement offsets are owned
// here. No queued output, minimum force or sample-count admission is introduced.
class CaptureEvidenceFilter {
public:
    using Vec = pipeline_contract::Vec2f;
    struct Result {
        Vec position_offset{}, position_rate_offset{};
        bool rate_available = false;
    };

    void reset() noexcept { *this = {}; }
    void align_interval(double begin) noexcept {
        if(count_ && std::fabs(begin-capture_)>1e-6) reset();
    }
    Result update(std::uint64_t target, bool cue, bool pixel_anchor,
                  double capture, Vec position,
                  Vec camera_delta, float response, float noise_fraction) noexcept {
        if (!target || !std::isfinite(capture) || !pipeline_contract::finite(position) ||
            !pipeline_contract::finite(camera_delta) || !std::isfinite(response) ||
            !std::isfinite(noise_fraction)) {
            reset(); return {};
        }
        const double dt = capture - capture_;
        if (target != target_ || cue != cue_ || pixel_anchor != pixel_anchor_ ||
            dt <= 0 || dt > .032) {
            reset(); target_=target;cue_=cue;pixel_anchor_=pixel_anchor;
        } else {
            // Transport the short history into the current camera coordinate.
            // Never accumulate an unbounded session-long camera position.
            for(std::size_t i=0;i<count_;++i) {
                samples_[i].position.x+=camera_delta.x;samples_[i].position.y+=camera_delta.y;
            }
        }
        const bool previous = count_ > 0;
        const Vec raw_rate = previous ? Vec{float((position.x-raw_.x)/dt),float((position.y-raw_.y)/dt)} : Vec{};
        // Reuse the existing 25ms motion integration horizon. Replayed control
        // ticks never enter this history or renew the source's 32/40ms deadline.
        std::size_t kept=0;
        for(std::size_t i=0;i<count_;++i)
            if(capture-samples_[i].time<=.025) samples_[kept++]=samples_[i];
        count_=kept;
        if(count_==samples_.size()) {
            for(std::size_t i=1;i<count_;++i)samples_[i-1]=samples_[i];
            --count_;
        }
        samples_[count_++]={capture,position};
        // The legacy stored percentage now expresses measurement-noise scale:
        // that fraction of calibrated camera response over one capture interval.
        // It never sets a lower bound on physical output.
        const float sigma=std::max(0.f,noise_fraction)*std::max(0.f,response)*
            (previous ? float(dt) : 0.f);
        const Vec filtered=filter(position,capture,sigma);
        result_.position_offset={filtered.x-position.x,filtered.y-position.y};
        result_.rate_available=previous;
        result_.position_rate_offset=previous ? Vec{
            float((filtered.x-filtered_.x)/dt)-raw_rate.x,
            float((filtered.y-filtered_.y)/dt)-raw_rate.y} : Vec{};
        raw_=position;filtered_=filtered;capture_=capture;
        return result_;
    }
    Result at(std::uint64_t target, double capture) const noexcept {
        return target==target_ && std::fabs(capture-capture_)<1e-6 ? result_ : Result{};
    }
private:
    struct Fit {Vec position{},velocity{};};
    Fit trajectory(double capture) const noexcept {
        double t=0,tt=0;Vec mean{},cross{};
        for(std::size_t i=0;i<count_;++i) {
            const auto y=samples_[i].position;
            t+=samples_[i].time-capture;mean.x+=y.x;mean.y+=y.y;
        }
        t/=count_;mean.x/=float(count_);mean.y/=float(count_);
        for(std::size_t i=0;i<count_;++i) {
            const float d=float(samples_[i].time-capture-t);
            const auto y=samples_[i].position;
            tt+=d*d;cross.x+=d*(y.x-mean.x);cross.y+=d*(y.y-mean.y);
        }
        if(tt<=1e-12) return {mean,{}};
        Fit fit{{float(mean.x-cross.x*t/tt),float(mean.y-cross.y*t/tt)},
                {float(cross.x/tt),float(cross.y/tt)}};
        return fit;
    }
    Vec filter(Vec raw,double capture,float sigma) const noexcept {
        if(count_<3 || sigma<=0) return raw;
        const auto fit=trajectory(capture);
        const Vec innovation{fit.position.x-raw.x,fit.position.y-raw.y};
        const float ratio=(innovation.x*innovation.x+innovation.y*innovation.y)/(sigma*sigma);
        // Continuous spatial influence: deviations beyond the noise model
        // retain the new measurement instead of holding a stale trajectory.
        const float influence=1.f/(1.f+ratio*ratio);
        return {raw.x+influence*innovation.x,raw.y+influence*innovation.y};
    }
    struct Sample {double time=0;Vec position{};};
    std::array<Sample,8> samples_{};
    std::size_t count_=0;
    std::uint64_t target_=0;
    bool cue_=false,pixel_anchor_=false;
    double capture_=0;
    Vec raw_{},filtered_{};
    Result result_{};
};
} // namespace controller_native
