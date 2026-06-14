#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace fps {

using TimeSec = double;

constexpr double kEps = 1e-9;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }

    [[nodiscard]] double length2() const { return x * x + y * y; }
    [[nodiscard]] double length() const { return std::sqrt(length2()); }
};

inline constexpr Vec2 operator*(double s, const Vec2& v) { return v * s; }
inline constexpr double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline constexpr Vec2 lerp(const Vec2& a, const Vec2& b, double t) { return a * (1.0 - t) + b * t; }

inline double clamp(double v, double lo, double hi) {
    return std::max(lo, std::min(v, hi));
}

inline double safeLogRatio(double a, double b) {
    return std::log(std::max(a, kEps) / std::max(b, kEps));
}

struct Box2 {
    // Full-screen pixel coordinates.
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;

    [[nodiscard]] double width() const { return std::max(0.0, x1 - x0); }
    [[nodiscard]] double height() const { return std::max(0.0, y1 - y0); }
    [[nodiscard]] double area() const { return width() * height(); }
    [[nodiscard]] Vec2 center() const { return {(x0 + x1) * 0.5, (y0 + y1) * 0.5}; }
    [[nodiscard]] double aspect() const { return width() / std::max(height(), kEps); }

    static Box2 fromCenterSize(Vec2 c, Vec2 size) {
        return {c.x - size.x * 0.5, c.y - size.y * 0.5,
                c.x + size.x * 0.5, c.y + size.y * 0.5};
    }
};

inline double iou(const Box2& a, const Box2& b) {
    const double ix0 = std::max(a.x0, b.x0);
    const double iy0 = std::max(a.y0, b.y0);
    const double ix1 = std::min(a.x1, b.x1);
    const double iy1 = std::min(a.y1, b.y1);
    const double iw = std::max(0.0, ix1 - ix0);
    const double ih = std::max(0.0, iy1 - iy0);
    const double inter = iw * ih;
    const double uni = a.area() + b.area() - inter;
    return uni <= kEps ? 0.0 : inter / uni;
}

struct Mat2 {
    // Row-major: [a b; c d]
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;

    static Mat2 diag(double x, double y) { return {x, 0.0, 0.0, y}; }
    static Mat2 identity() { return {}; }

    [[nodiscard]] double det() const { return a * d - b * c; }

    [[nodiscard]] Mat2 inverse(double regularization = 1e-9) const {
        double determinant = det();
        if (std::abs(determinant) < regularization) {
            Mat2 reg = *this;
            reg.a += regularization;
            reg.d += regularization;
            determinant = reg.det();
            if (std::abs(determinant) < regularization) {
                return {1.0 / regularization, 0.0, 0.0, 1.0 / regularization};
            }
            const double invDet = 1.0 / determinant;
            return {reg.d * invDet, -reg.b * invDet, -reg.c * invDet, reg.a * invDet};
        }
        const double invDet = 1.0 / determinant;
        return {d * invDet, -b * invDet, -c * invDet, a * invDet};
    }
};

inline Vec2 operator*(const Mat2& m, const Vec2& v) {
    return {m.a * v.x + m.b * v.y, m.c * v.x + m.d * v.y};
}

inline Mat2 operator+(const Mat2& x, const Mat2& y) {
    return {x.a + y.a, x.b + y.b, x.c + y.c, x.d + y.d};
}

inline Mat2 operator*(const Mat2& x, double s) {
    return {x.a * s, x.b * s, x.c * s, x.d * s};
}

} // namespace fps
