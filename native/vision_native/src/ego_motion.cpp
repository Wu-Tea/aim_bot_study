#include "vision_native/ego_motion.h"

#include "image_ops.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vision_native {
namespace {

using detail::GrayFrame;
using detail::Point2f;
using detail::RectF;

constexpr int kDownsample = 2;
constexpr int kPatchRadius = 2;
constexpr int kSearchRadius = 4;
constexpr int kGridStep = 6;
constexpr float kVarianceThreshold = 45.0f;
constexpr float kInlierResidualThreshold = 2.2f;
constexpr int kMinimumValidPoints = 4;
constexpr int kMinimumAffinePoints = 6;

RectF inflate_detection_rect(const Detection& detection, int downsample) {
    const float pad_x = (detection.x2 - detection.x1) * 0.12f;
    const float pad_y = (detection.y2 - detection.y1) * 0.12f;
    return {
        (detection.x1 - pad_x) / static_cast<float>(downsample),
        (detection.y1 - pad_y) / static_cast<float>(downsample),
        (detection.x2 + pad_x) / static_cast<float>(downsample),
        (detection.y2 + pad_y) / static_cast<float>(downsample),
    };
}

void apply_fixed_mask(std::vector<uint8_t>& mask, int width, int height) {
    // Lower center weapon / hands region.
    detail::clear_rect(mask, width, height, RectF{
        width * 0.18f,
        height * 0.72f,
        width * 0.82f,
        static_cast<float>(height),
    });
    // Scope / edge frame bands.
    detail::clear_rect(mask, width, height, RectF{0.0f, 0.0f, width * 0.06f, static_cast<float>(height)});
    detail::clear_rect(mask, width, height, RectF{width * 0.94f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
    detail::clear_rect(mask, width, height, RectF{0.0f, 0.0f, static_cast<float>(width), height * 0.05f});
}

void apply_dynamic_mask(
    std::vector<uint8_t>& mask,
    const GrayFrame& gray,
    const DetectionBatch& batch) {
    for (const auto& detection : batch.detections) {
        detail::clear_rect(mask, gray.width, gray.height, detail::clamp_rect(inflate_detection_rect(detection, kDownsample), gray.width, gray.height));
    }

    // Muzzle flash / overbright disturbance suppression.
    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            if (gray.at(x, y) >= 245) {
                detail::clear_rect(mask, gray.width, gray.height, RectF{
                    static_cast<float>(x - 2),
                    static_cast<float>(y - 2),
                    static_cast<float>(x + 3),
                    static_cast<float>(y + 3),
                });
            }
        }
    }
}

std::vector<Point2f> collect_points(const GrayFrame& gray, const std::vector<uint8_t>& valid_mask) {
    std::vector<Point2f> points;
    for (int y = kPatchRadius + 2; y < (gray.height - (kPatchRadius + 2)); y += kGridStep) {
        for (int x = kPatchRadius + 2; x < (gray.width - (kPatchRadius + 2)); x += kGridStep) {
            if (valid_mask[static_cast<size_t>(y) * static_cast<size_t>(gray.width) + static_cast<size_t>(x)] == 0) {
                continue;
            }
            const float variance = detail::patch_variance(gray, x, y, kPatchRadius, &valid_mask);
            if (variance < kVarianceThreshold) {
                continue;
            }
            points.push_back({static_cast<float>(x), static_cast<float>(y)});
        }
    }
    return points;
}

bool solve_linear_6x6(float matrix[6][7], std::array<float, 6>& solution) {
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        float pivot_abs = std::fabs(matrix[pivot][col]);
        for (int row = col + 1; row < 6; ++row) {
            const float candidate = std::fabs(matrix[row][col]);
            if (candidate > pivot_abs) {
                pivot = row;
                pivot_abs = candidate;
            }
        }
        if (pivot_abs < 1e-5f) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 7; ++k) {
                std::swap(matrix[col][k], matrix[pivot][k]);
            }
        }
        const float div = matrix[col][col];
        for (int k = col; k < 7; ++k) {
            matrix[col][k] /= div;
        }
        for (int row = 0; row < 6; ++row) {
            if (row == col) {
                continue;
            }
            const float factor = matrix[row][col];
            for (int k = col; k < 7; ++k) {
                matrix[row][k] -= factor * matrix[col][k];
            }
        }
    }
    for (int row = 0; row < 6; ++row) {
        solution[static_cast<size_t>(row)] = matrix[row][6];
    }
    return true;
}

bool fit_affine(
    const std::vector<Point2f>& previous_points,
    const std::vector<Point2f>& current_points,
    std::array<float, 6>& solution) {
    if (previous_points.size() != current_points.size() || previous_points.size() < static_cast<size_t>(kMinimumAffinePoints)) {
        return false;
    }

    float normal[6][7] = {};
    for (size_t index = 0; index < previous_points.size(); ++index) {
        const float x = previous_points[index].x;
        const float y = previous_points[index].y;
        const float xp = current_points[index].x;
        const float yp = current_points[index].y;
        const float row_x[6] = {x, y, 1.0f, 0.0f, 0.0f, 0.0f};
        const float row_y[6] = {0.0f, 0.0f, 0.0f, x, y, 1.0f};
        for (int r = 0; r < 6; ++r) {
            for (int c = 0; c < 6; ++c) {
                normal[r][c] += row_x[r] * row_x[c];
                normal[r][c] += row_y[r] * row_y[c];
            }
            normal[r][6] += row_x[r] * xp;
            normal[r][6] += row_y[r] * yp;
        }
    }
    return solve_linear_6x6(normal, solution);
}

Point2f apply_affine(const std::array<float, 6>& affine, const Point2f& point) {
    return {
        (affine[0] * point.x) + (affine[1] * point.y) + affine[2],
        (affine[3] * point.x) + (affine[4] * point.y) + affine[5],
    };
}

EgoWarp make_identity() {
    EgoWarp warp;
    warp.model = "identity";
    return warp;
}

EgoWarp make_translation(float dx, float dy, int valid_points, int inlier_points, float confidence) {
    EgoWarp warp;
    warp.tx = dx;
    warp.ty = dy;
    warp.valid_points = valid_points;
    warp.inlier_points = inlier_points;
    warp.confidence = confidence;
    warp.model = (std::fabs(dx) > 0.01f || std::fabs(dy) > 0.01f) ? "translation" : "identity";
    return warp;
}

} // namespace

EgoMotionEstimator::EgoMotionEstimator(int width, int height)
    : width_(width),
      height_(height) {}

void EgoMotionEstimator::reset() {
    has_previous_ = false;
    previous_gray_.clear();
    previous_gray_width_ = 0;
    previous_gray_height_ = 0;
}

EgoWarp EgoMotionEstimator::estimate(const EgoFrameView& frame, const DetectionBatch& batch) {
    return estimate_gray(detail::to_grayscale(frame, kDownsample), batch);
}

EgoWarp EgoMotionEstimator::estimate_gray(const GrayFrame& current_gray, const DetectionBatch& batch) {
    if (current_gray.empty()) {
        return make_identity();
    }

    EgoWarp fallback = make_identity();
    std::vector<uint8_t> valid_mask = detail::make_valid_mask(current_gray.width, current_gray.height);
    apply_fixed_mask(valid_mask, current_gray.width, current_gray.height);
    apply_dynamic_mask(valid_mask, current_gray, batch);

    if (!has_previous_ || previous_gray_width_ != current_gray.width || previous_gray_height_ != current_gray.height) {
        previous_gray_ = current_gray.pixels;
        previous_gray_width_ = current_gray.width;
        previous_gray_height_ = current_gray.height;
        has_previous_ = true;
        return fallback;
    }

    GrayFrame previous_gray;
    previous_gray.width = previous_gray_width_;
    previous_gray.height = previous_gray_height_;
    previous_gray.pixels = previous_gray_;

    const std::vector<Point2f> seed_points = collect_points(previous_gray, valid_mask);
    if (seed_points.size() < static_cast<size_t>(kMinimumValidPoints)) {
        previous_gray_ = current_gray.pixels;
        previous_gray_width_ = current_gray.width;
        previous_gray_height_ = current_gray.height;
        return fallback;
    }

    std::vector<Point2f> previous_points;
    std::vector<Point2f> current_points;
    std::vector<float> dx_values;
    std::vector<float> dy_values;
    previous_points.reserve(seed_points.size());
    current_points.reserve(seed_points.size());
    dx_values.reserve(seed_points.size());
    dy_values.reserve(seed_points.size());

    for (const Point2f& seed : seed_points) {
        Point2f matched{};
        float point_conf = 0.0f;
        const int px = static_cast<int>(std::round(seed.x));
        const int py = static_cast<int>(std::round(seed.y));
        if (!detail::track_patch_ssd(
                previous_gray,
                current_gray,
                &valid_mask,
                px,
                py,
                px,
                py,
                kSearchRadius,
                kPatchRadius,
                matched,
                point_conf)) {
            continue;
        }
        previous_points.push_back(seed);
        current_points.push_back(matched);
        dx_values.push_back(matched.x - seed.x);
        dy_values.push_back(matched.y - seed.y);
    }

    const int valid_points = static_cast<int>(previous_points.size());
    if (valid_points < kMinimumValidPoints) {
        previous_gray_ = current_gray.pixels;
        previous_gray_width_ = current_gray.width;
        previous_gray_height_ = current_gray.height;
        return fallback;
    }

    const float median_dx = detail::median_value(dx_values);
    const float median_dy = detail::median_value(dy_values);

    std::vector<Point2f> filtered_previous;
    std::vector<Point2f> filtered_current;
    filtered_previous.reserve(previous_points.size());
    filtered_current.reserve(current_points.size());
    for (size_t index = 0; index < previous_points.size(); ++index) {
        const float dx = current_points[index].x - previous_points[index].x;
        const float dy = current_points[index].y - previous_points[index].y;
        if (std::fabs(dx - median_dx) <= 2.5f && std::fabs(dy - median_dy) <= 2.5f) {
            filtered_previous.push_back(previous_points[index]);
            filtered_current.push_back(current_points[index]);
        }
    }

    const int inlier_points = static_cast<int>(filtered_previous.size());
    if (inlier_points < kMinimumValidPoints) {
        previous_gray_ = current_gray.pixels;
        previous_gray_width_ = current_gray.width;
        previous_gray_height_ = current_gray.height;
        return fallback;
    }

    float residual_mean = 0.0f;
    std::array<float, 6> affine = {1.0f, 0.0f, median_dx, 0.0f, 1.0f, median_dy};
    bool affine_valid = fit_affine(filtered_previous, filtered_current, affine);
    if (affine_valid) {
        std::vector<Point2f> refined_previous;
        std::vector<Point2f> refined_current;
        refined_previous.reserve(filtered_previous.size());
        refined_current.reserve(filtered_current.size());
        for (size_t index = 0; index < filtered_previous.size(); ++index) {
            const Point2f warped = apply_affine(affine, filtered_previous[index]);
            const float residual = detail::point_distance(warped, filtered_current[index]);
            if (residual <= kInlierResidualThreshold) {
                refined_previous.push_back(filtered_previous[index]);
                refined_current.push_back(filtered_current[index]);
                residual_mean += residual;
            }
        }

        if (refined_previous.size() >= static_cast<size_t>(kMinimumAffinePoints) && fit_affine(refined_previous, refined_current, affine)) {
            filtered_previous = std::move(refined_previous);
            filtered_current = std::move(refined_current);
        } else {
            affine_valid = false;
        }
    }

    if (!affine_valid) {
        residual_mean = 0.0f;
        for (size_t index = 0; index < filtered_previous.size(); ++index) {
            const float residual = detail::point_distance(
                {filtered_previous[index].x + median_dx, filtered_previous[index].y + median_dy},
                filtered_current[index]);
            residual_mean += residual;
        }
        residual_mean /= std::max(1, static_cast<int>(filtered_previous.size()));
        previous_gray_ = current_gray.pixels;
        previous_gray_width_ = current_gray.width;
        previous_gray_height_ = current_gray.height;
        const float residual_score = detail::clampf(1.0f - (residual_mean / 3.0f), 0.0f, 1.0f);
        const float confidence =
            (detail::clampf(static_cast<float>(valid_points) / 20.0f, 0.0f, 1.0f) * 0.35f)
            + (detail::clampf(static_cast<float>(inlier_points) / static_cast<float>(valid_points), 0.0f, 1.0f) * 0.40f)
            + (residual_score * 0.25f);
        return make_translation(
            median_dx * static_cast<float>(kDownsample),
            median_dy * static_cast<float>(kDownsample),
            valid_points,
            inlier_points,
            confidence);
    }

    residual_mean = 0.0f;
    for (size_t index = 0; index < filtered_previous.size(); ++index) {
        const Point2f warped = apply_affine(affine, filtered_previous[index]);
        residual_mean += detail::point_distance(warped, filtered_current[index]);
    }
    residual_mean /= std::max(1, static_cast<int>(filtered_previous.size()));

    previous_gray_ = current_gray.pixels;
    previous_gray_width_ = current_gray.width;
    previous_gray_height_ = current_gray.height;

    EgoWarp warp;
    warp.a00 = affine[0];
    warp.a01 = affine[1];
    warp.a10 = affine[3];
    warp.a11 = affine[4];
    warp.tx = affine[2] * static_cast<float>(kDownsample);
    warp.ty = affine[5] * static_cast<float>(kDownsample);
    warp.valid_points = valid_points;
    warp.inlier_points = static_cast<int>(filtered_previous.size());
    const float residual_score = detail::clampf(1.0f - (residual_mean / 3.0f), 0.0f, 1.0f);
    warp.confidence =
        (detail::clampf(static_cast<float>(valid_points) / 20.0f, 0.0f, 1.0f) * 0.35f)
        + (detail::clampf(static_cast<float>(warp.inlier_points) / static_cast<float>(valid_points), 0.0f, 1.0f) * 0.40f)
        + (residual_score * 0.25f);
    warp.model = "affine";
    return warp;
}

} // namespace vision_native
