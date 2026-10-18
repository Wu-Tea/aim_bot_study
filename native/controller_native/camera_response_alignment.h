#pragma once

#include "pipeline_contract/target_plan.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace controller_native {

// Identifies a physical camera, not an ADS/BodyLock policy. Each command is
// integrated over the SAME capture interval at a candidate transport offset.
// A per-axis affine nuisance term absorbs locally moving targets / zoom drift.
// No output is queued or delayed by this estimator.
class CameraResponseAlignment {
public:
    static constexpr int delay_count = 31;
    using Commands = std::array<pipeline_contract::Vec2f, delay_count>;
    struct Estimate { float response = 0.f; float delay_seconds = .009f; bool valid = false; };

    void begin_target(std::uint64_t target) noexcept {
        if (target == target_) return;
        target_ = target; count_ = next_ = 0;
    }
    void reset() noexcept { *this = {}; }
    void invalidate_evidence() noexcept {count_=next_=0;target_=0;}
    Estimate estimate() const noexcept { return estimate_; }
    void update(double capture, pipeline_contract::Vec2f rate, const Commands& commands) noexcept {
        if (!std::isfinite(capture) || !pipeline_contract::finite(rate)) return;
        for (auto c : commands) if (!pipeline_contract::finite(c)) return;
        samples_[next_] = {capture, {rate.x, -rate.y}, commands};
        next_ = (next_ + 1) % samples_.size();
        count_ = std::min(count_ + 1, samples_.size());
        if (count_ < 8) return; // identification needs excitation, not a control wait
        double mt=0,tt=0; pipeline_contract::Vec2f my{},ty{};
        for (std::size_t i=0;i<count_;++i) {
            const float t=float(samples_[i].capture-capture);
            mt+=t;my.x+=samples_[i].rate.x;my.y+=samples_[i].rate.y;
        }
        mt/=count_;my.x/=float(count_);my.y/=float(count_);
        for (std::size_t i=0;i<count_;++i) {
            const float t=float(samples_[i].capture-capture-mt);
            tt+=t*t;ty.x+=t*(samples_[i].rate.x-my.x);ty.y+=t*(samples_[i].rate.y-my.y);
        }
        if (tt<1e-9) return;
        double best=std::numeric_limits<double>::infinity(); Estimate fitted{};
        for (int delay=0;delay<delay_count;++delay) {
            pipeline_contract::Vec2f mu{},tu{};
            for(std::size_t i=0;i<count_;++i) {mu.x+=samples_[i].commands[delay].x;mu.y+=samples_[i].commands[delay].y;}
            mu.x/=float(count_);mu.y/=float(count_);
            for(std::size_t i=0;i<count_;++i) {
                const float t=float(samples_[i].capture-capture-mt);
                tu.x+=t*(samples_[i].commands[delay].x-mu.x);tu.y+=t*(samples_[i].commands[delay].y-mu.y);
            }
            double uu=0,uy=0,yy=0;
            for(std::size_t i=0;i<count_;++i) {
                const double t=samples_[i].capture-capture-mt;
                const double ux=samples_[i].commands[delay].x-mu.x-tu.x*t/tt;
                const double uy_axis=samples_[i].commands[delay].y-mu.y-tu.y*t/tt;
                const double yx=samples_[i].rate.x-my.x-ty.x*t/tt;
                const double y_y=samples_[i].rate.y-my.y-ty.y*t/tt;
                uu+=ux*ux+uy_axis*uy_axis;uy+=ux*yx+uy_axis*y_y;yy+=yx*yx+y_y*y_y;
            }
            // Unexcited / unexplained data cannot identify camera gain or lag.
            if(uu<.001*count_ || yy<1e-6) continue;
            const double response=-uy/uu;
            if(response<80 || response>4000) continue;
            const double residual=std::max(0.,yy-uy*uy/uu)/yy;
            if(residual<best) {best=residual;fitted={float(response),delay*.001f,true};}
        }
        // This is an identification trust boundary. Poor fits retain prior
        // calibration, never grant authority or manufacture target motion.
        if(best<.08) estimate_=fitted;
    }
private:
    struct Sample {double capture=0;pipeline_contract::Vec2f rate{};Commands commands{};};
    std::array<Sample,16> samples_{};
    std::size_t count_=0,next_=0;
    std::uint64_t target_=0;
    Estimate estimate_{};
};
} // namespace controller_native
