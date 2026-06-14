#pragma once

#include <array>

#include "fps_tracker/math.hpp"

namespace fps {

class KalmanCv2d {
public:
    KalmanCv2d() = default;

    void reset(Vec2 position, TimeSec time, double posVar = 1e-4, double velVar = 0.25);
    void predictTo(TimeSec time, double accelNoise);
    void updatePosition(Vec2 z, const Mat2& R);

    [[nodiscard]] Vec2 position() const { return {x_[0], x_[1]}; }
    [[nodiscard]] Vec2 velocity() const { return {x_[2], x_[3]}; }
    [[nodiscard]] TimeSec time() const { return time_; }
    [[nodiscard]] Mat2 positionCovariance() const { return {P_[0][0], P_[0][1], P_[1][0], P_[1][1]}; }
    [[nodiscard]] double positionSigma() const;
    [[nodiscard]] double mahalanobisD2(Vec2 z, const Mat2& R) const;

    [[nodiscard]] const std::array<double, 4>& state() const { return x_; }
    [[nodiscard]] const std::array<std::array<double, 4>, 4>& covariance() const { return P_; }

private:
    TimeSec time_ = 0.0;
    std::array<double, 4> x_{0.0, 0.0, 0.0, 0.0};
    std::array<std::array<double, 4>, 4> P_{{
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0},
    }};
};

} // namespace fps
