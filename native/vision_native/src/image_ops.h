#pragma once

#include "vision_native/ego_motion.h"
#include "vision_native/gray_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace vision_native::detail {

struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

using GrayFrame = ::vision_native::GrayFrame;

inline float clampf(float value, float lower, float upper) {
    return std::max(lower, std::min(upper, value));
}

inline int clampi(int value, int lower, int upper) {
    return std::max(lower, std::min(upper, value));
}

inline RectF clamp_rect(RectF rect, int width, int height) {
    rect.left = clampf(rect.left, 0.0f, static_cast<float>(width));
    rect.right = clampf(rect.right, 0.0f, static_cast<float>(width));
    rect.top = clampf(rect.top, 0.0f, static_cast<float>(height));
    rect.bottom = clampf(rect.bottom, 0.0f, static_cast<float>(height));
    if (rect.right < rect.left) {
        rect.right = rect.left;
    }
    if (rect.bottom < rect.top) {
        rect.bottom = rect.top;
    }
    return rect;
}

inline float rect_width(const RectF& rect) {
    return rect.right - rect.left;
}

inline float rect_height(const RectF& rect) {
    return rect.bottom - rect.top;
}

inline Point2f rect_center(const RectF& rect) {
    return {
        (rect.left + rect.right) * 0.5f,
        (rect.top + rect.bottom) * 0.5f,
    };
}

inline float point_distance(const Point2f& lhs, const Point2f& rhs) {
    return std::hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

inline RectF rect_from_result(const VisionResult& result) {
    return {
        result.body_x1,
        result.body_y1,
        result.body_x2,
        result.body_y2,
    };
}

inline RectF torso_band_from_body_box(const RectF& body_box) {
    const float box_w = rect_width(body_box);
    const float box_h = rect_height(body_box);
    return {
        body_box.left + (box_w * 0.22f),
        body_box.top + (box_h * 0.18f),
        body_box.right - (box_w * 0.22f),
        body_box.bottom - (box_h * 0.20f),
    };
}

inline Point2f apply_ego_warp(const EgoWarp& warp, const Point2f& point) {
    return {
        (warp.a00 * point.x) + (warp.a01 * point.y) + warp.tx,
        (warp.a10 * point.x) + (warp.a11 * point.y) + warp.ty,
    };
}

inline RectF apply_ego_warp(const EgoWarp& warp, const RectF& rect) {
    const Point2f tl = apply_ego_warp(warp, Point2f{rect.left, rect.top});
    const Point2f tr = apply_ego_warp(warp, Point2f{rect.right, rect.top});
    const Point2f bl = apply_ego_warp(warp, Point2f{rect.left, rect.bottom});
    const Point2f br = apply_ego_warp(warp, Point2f{rect.right, rect.bottom});
    return {
        std::min(std::min(tl.x, tr.x), std::min(bl.x, br.x)),
        std::min(std::min(tl.y, tr.y), std::min(bl.y, br.y)),
        std::max(std::max(tl.x, tr.x), std::max(bl.x, br.x)),
        std::max(std::max(tl.y, tr.y), std::max(bl.y, br.y)),
    };
}

inline GrayFrame to_grayscale(const EgoFrameView& frame, int downsample) {
    GrayFrame gray;
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 || frame.row_pitch <= 0) {
        return gray;
    }

    const int step = std::max(1, downsample);
    gray.width = std::max(1, frame.width / step);
    gray.height = std::max(1, frame.height / step);
    gray.pixels.resize(static_cast<size_t>(gray.width) * static_cast<size_t>(gray.height));

    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            const int src_x = std::min(frame.width - 1, x * step);
            const int src_y = std::min(frame.height - 1, y * step);
            const uint8_t* row = frame.data + (static_cast<size_t>(src_y) * static_cast<size_t>(frame.row_pitch));
            int r = 0;
            int g = 0;
            int b = 0;
            if (frame.format == PixelFormat::BGRA8) {
                const uint8_t* pixel = row + (static_cast<size_t>(src_x) * 4);
                b = pixel[0];
                g = pixel[1];
                r = pixel[2];
            } else {
                const uint8_t* pixel = row + (static_cast<size_t>(src_x) * 3);
                r = pixel[0];
                g = pixel[1];
                b = pixel[2];
            }
            gray.pixels[static_cast<size_t>(y) * static_cast<size_t>(gray.width) + static_cast<size_t>(x)] =
                static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
        }
    }

    return gray;
}

inline GrayFrame downsample_gray(const GrayFrame& source, int downsample) {
    GrayFrame gray;
    if (source.empty()) {
        return gray;
    }

    const int step = std::max(1, downsample);
    gray.width = std::max(1, source.width / step);
    gray.height = std::max(1, source.height / step);
    gray.pixels.resize(static_cast<size_t>(gray.width) * static_cast<size_t>(gray.height));

    for (int y = 0; y < gray.height; ++y) {
        for (int x = 0; x < gray.width; ++x) {
            const int src_x = std::min(source.width - 1, x * step);
            const int src_y = std::min(source.height - 1, y * step);
            gray.pixels[static_cast<size_t>(y) * static_cast<size_t>(gray.width) + static_cast<size_t>(x)] =
                source.at(src_x, src_y);
        }
    }

    return gray;
}

inline std::vector<uint8_t> make_valid_mask(int width, int height) {
    return std::vector<uint8_t>(static_cast<size_t>(width) * static_cast<size_t>(height), 1);
}

inline void clear_rect(std::vector<uint8_t>& mask, int width, int height, const RectF& rect) {
    const int left = clampi(static_cast<int>(std::floor(rect.left)), 0, width);
    const int right = clampi(static_cast<int>(std::ceil(rect.right)), 0, width);
    const int top = clampi(static_cast<int>(std::floor(rect.top)), 0, height);
    const int bottom = clampi(static_cast<int>(std::ceil(rect.bottom)), 0, height);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            mask[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = 0;
        }
    }
}

inline bool patch_inside(const GrayFrame& frame, int cx, int cy, int radius) {
    return cx >= radius
        && cy >= radius
        && cx < (frame.width - radius)
        && cy < (frame.height - radius);
}

inline float patch_variance(
    const GrayFrame& frame,
    int cx,
    int cy,
    int radius,
    const std::vector<uint8_t>* valid_mask = nullptr) {
    if (!patch_inside(frame, cx, cy, radius)) {
        return 0.0f;
    }

    float mean = 0.0f;
    float mean_sq = 0.0f;
    int count = 0;
    for (int y = cy - radius; y <= cy + radius; ++y) {
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (valid_mask != nullptr) {
                const uint8_t valid = (*valid_mask)[static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)];
                if (valid == 0) {
                    return 0.0f;
                }
            }
            const float value = static_cast<float>(frame.at(x, y));
            mean += value;
            mean_sq += value * value;
            ++count;
        }
    }
    if (count <= 0) {
        return 0.0f;
    }
    mean /= static_cast<float>(count);
    mean_sq /= static_cast<float>(count);
    return std::max(0.0f, mean_sq - (mean * mean));
}

inline bool track_patch_ssd(
    const GrayFrame& previous,
    const GrayFrame& current,
    const std::vector<uint8_t>* current_valid_mask,
    int prev_x,
    int prev_y,
    int expected_x,
    int expected_y,
    int search_radius,
    int patch_radius,
    Point2f& matched_point,
    float& confidence) {
    if (!patch_inside(previous, prev_x, prev_y, patch_radius)) {
        return false;
    }

    float best_score = std::numeric_limits<float>::max();
    float second_score = std::numeric_limits<float>::max();
    int best_x = expected_x;
    int best_y = expected_y;

    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            const int cand_x = expected_x + dx;
            const int cand_y = expected_y + dy;
            if (!patch_inside(current, cand_x, cand_y, patch_radius)) {
                continue;
            }
            if (current_valid_mask != nullptr) {
                if ((*current_valid_mask)[static_cast<size_t>(cand_y) * static_cast<size_t>(current.width) + static_cast<size_t>(cand_x)] == 0) {
                    continue;
                }
            }

            float ssd = 0.0f;
            for (int py = -patch_radius; py <= patch_radius; ++py) {
                for (int px = -patch_radius; px <= patch_radius; ++px) {
                    const float lhs = static_cast<float>(previous.at(prev_x + px, prev_y + py));
                    const float rhs = static_cast<float>(current.at(cand_x + px, cand_y + py));
                    const float diff = lhs - rhs;
                    ssd += diff * diff;
                }
            }

            if (ssd < best_score) {
                second_score = best_score;
                best_score = ssd;
                best_x = cand_x;
                best_y = cand_y;
            } else if (ssd < second_score) {
                second_score = ssd;
            }
        }
    }

    if (!std::isfinite(best_score)) {
        return false;
    }

    const float ambiguity = second_score > 0.0f ? (best_score / second_score) : 0.0f;
    if (best_score > 15000.0f) {
        return false;
    }
    if (second_score < std::numeric_limits<float>::max() && ambiguity > 0.90f) {
        return false;
    }

    matched_point = {
        static_cast<float>(best_x),
        static_cast<float>(best_y),
    };
    confidence = clampf(1.0f - (best_score / 15000.0f), 0.0f, 1.0f);
    return true;
}

inline bool extract_patch(
    const GrayFrame& frame,
    int center_x,
    int center_y,
    int radius,
    std::vector<uint8_t>& patch,
    int& patch_width,
    int& patch_height) {
    if (!patch_inside(frame, center_x, center_y, radius)) {
        return false;
    }

    patch_width = (radius * 2) + 1;
    patch_height = patch_width;
    patch.resize(static_cast<size_t>(patch_width) * static_cast<size_t>(patch_height));
    size_t index = 0;
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
        for (int x = center_x - radius; x <= center_x + radius; ++x) {
            patch[index++] = frame.at(x, y);
        }
    }
    return true;
}

inline bool search_patch_ncc(
    const GrayFrame& frame,
    const std::vector<uint8_t>& patch,
    int patch_width,
    int patch_height,
    int expected_x,
    int expected_y,
    int search_radius,
    const std::vector<uint8_t>* valid_mask,
    Point2f& matched_point,
    float& confidence) {
    if (patch.empty() || patch_width <= 0 || patch_height <= 0 || patch_width != patch_height) {
        return false;
    }
    const int radius = patch_width / 2;
    if (!patch_inside(frame, expected_x, expected_y, radius)) {
        return false;
    }

    float patch_mean = 0.0f;
    for (const uint8_t value : patch) {
        patch_mean += static_cast<float>(value);
    }
    patch_mean /= static_cast<float>(patch.size());

    float patch_energy = 0.0f;
    for (const uint8_t value : patch) {
        const float centered = static_cast<float>(value) - patch_mean;
        patch_energy += centered * centered;
    }
    if (patch_energy <= 1.0f) {
        return false;
    }

    float best_score = -1.0f;
    int best_x = expected_x;
    int best_y = expected_y;

    for (int dy = -search_radius; dy <= search_radius; ++dy) {
        for (int dx = -search_radius; dx <= search_radius; ++dx) {
            const int cand_x = expected_x + dx;
            const int cand_y = expected_y + dy;
            if (!patch_inside(frame, cand_x, cand_y, radius)) {
                continue;
            }
            if (valid_mask != nullptr) {
                if ((*valid_mask)[static_cast<size_t>(cand_y) * static_cast<size_t>(frame.width) + static_cast<size_t>(cand_x)] == 0) {
                    continue;
                }
            }

            float current_mean = 0.0f;
            for (int py = -radius; py <= radius; ++py) {
                for (int px = -radius; px <= radius; ++px) {
                    current_mean += static_cast<float>(frame.at(cand_x + px, cand_y + py));
                }
            }
            current_mean /= static_cast<float>(patch.size());

            float numerator = 0.0f;
            float current_energy = 0.0f;
            size_t index = 0;
            for (int py = -radius; py <= radius; ++py) {
                for (int px = -radius; px <= radius; ++px) {
                    const float lhs = static_cast<float>(patch[index++]) - patch_mean;
                    const float rhs = static_cast<float>(frame.at(cand_x + px, cand_y + py)) - current_mean;
                    numerator += lhs * rhs;
                    current_energy += rhs * rhs;
                }
            }
            if (current_energy <= 1.0f) {
                continue;
            }

            const float score = numerator / std::sqrt(patch_energy * current_energy);
            if (score > best_score) {
                best_score = score;
                best_x = cand_x;
                best_y = cand_y;
            }
        }
    }

    if (best_score < 0.40f) {
        return false;
    }

    matched_point = {
        static_cast<float>(best_x),
        static_cast<float>(best_y),
    };
    confidence = clampf((best_score - 0.40f) / 0.60f, 0.0f, 1.0f);
    return true;
}

inline float median_value(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if ((values.size() % 2) == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) * 0.5f;
}

} // namespace vision_native::detail
