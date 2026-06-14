#include "fps_tracker/kalman_cv2d.hpp"

#include <algorithm>
#include <cmath>

namespace fps {

namespace {
using Mat4 = std::array<std::array<double, 4>, 4>;

Mat4 identity4() {
    Mat4 m{};
    for (int i = 0; i < 4; ++i) {
        m[i][i] = 1.0;
    }
    return m;
}

Mat4 transpose4(const Mat4& a) {
    Mat4 out{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r][c] = a[c][r];
        }
    }
    return out;
}

Mat4 mul4(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += a[r][k] * b[k][c];
            }
            out[r][c] = sum;
        }
    }
    return out;
}

Mat4 add4(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r][c] = a[r][c] + b[r][c];
        }
    }
    return out;
}

} // namespace

void KalmanCv2d::reset(Vec2 position, TimeSec time, double posVar, double velVar) {
    time_ = time;
    x_ = {position.x, position.y, 0.0, 0.0};
    P_ = {{
        {posVar, 0.0, 0.0, 0.0},
        {0.0, posVar, 0.0, 0.0},
        {0.0, 0.0, velVar, 0.0},
        {0.0, 0.0, 0.0, velVar},
    }};
}

void KalmanCv2d::predictTo(TimeSec time, double accelNoise) {
    double dt = time - time_;
    if (dt <= 0.0) {
        return;
    }
    dt = std::min(dt, 0.250); // protect against huge gaps in reference implementation.

    Mat4 F = identity4();
    F[0][2] = dt;
    F[1][3] = dt;

    x_[0] += x_[2] * dt;
    x_[1] += x_[3] * dt;

    const double q = std::max(0.0, accelNoise);
    const double dt2 = dt * dt;
    const double dt3 = dt2 * dt;
    const double dt4 = dt2 * dt2;

    Mat4 Q{};
    Q[0][0] = q * dt4 * 0.25;
    Q[0][2] = q * dt3 * 0.5;
    Q[1][1] = q * dt4 * 0.25;
    Q[1][3] = q * dt3 * 0.5;
    Q[2][0] = q * dt3 * 0.5;
    Q[2][2] = q * dt2;
    Q[3][1] = q * dt3 * 0.5;
    Q[3][3] = q * dt2;

    P_ = add4(mul4(mul4(F, P_), transpose4(F)), Q);
    time_ = time;
}

void KalmanCv2d::updatePosition(Vec2 z, const Mat2& R) {
    // H = [1 0 0 0; 0 1 0 0]
    const Vec2 y{z.x - x_[0], z.y - x_[1]};

    Mat2 S{
        P_[0][0] + R.a,
        P_[0][1] + R.b,
        P_[1][0] + R.c,
        P_[1][1] + R.d,
    };
    const Mat2 Sinv = S.inverse();

    // K = P H^T S^-1 = first two columns of P times S^-1.
    double K[4][2]{};
    for (int r = 0; r < 4; ++r) {
        K[r][0] = P_[r][0] * Sinv.a + P_[r][1] * Sinv.c;
        K[r][1] = P_[r][0] * Sinv.b + P_[r][1] * Sinv.d;
    }

    for (int r = 0; r < 4; ++r) {
        x_[r] += K[r][0] * y.x + K[r][1] * y.y;
    }

    // Joseph form: P = (I-KH)P(I-KH)^T + K R K^T
    Mat4 I_KH = identity4();
    for (int r = 0; r < 4; ++r) {
        I_KH[r][0] -= K[r][0];
        I_KH[r][1] -= K[r][1];
    }

    Mat4 KRKT{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            KRKT[r][c] =
                K[r][0] * R.a * K[c][0] + K[r][0] * R.b * K[c][1] +
                K[r][1] * R.c * K[c][0] + K[r][1] * R.d * K[c][1];
        }
    }

    P_ = add4(mul4(mul4(I_KH, P_), transpose4(I_KH)), KRKT);
}

double KalmanCv2d::positionSigma() const {
    const double sx = std::max(0.0, P_[0][0]);
    const double sy = std::max(0.0, P_[1][1]);
    return std::sqrt(0.5 * (sx + sy));
}

double KalmanCv2d::mahalanobisD2(Vec2 z, const Mat2& R) const {
    const Vec2 y{z.x - x_[0], z.y - x_[1]};
    const Mat2 S{
        P_[0][0] + R.a,
        P_[0][1] + R.b,
        P_[1][0] + R.c,
        P_[1][1] + R.d,
    };
    const Vec2 v = S.inverse() * y;
    return dot(y, v);
}

} // namespace fps
