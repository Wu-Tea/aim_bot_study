#pragma once

#include "pipeline_contract/target_plan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace control_learning {

struct DeliveredControlSample {
    std::uint64_t sample_seq = 0;
    std::uint64_t applied_at_ns = 0;
    pipeline_contract::Vec2f physical_right{};
    pipeline_contract::Vec2f physical_left{};
    pipeline_contract::Vec2f manual_component{};
    pipeline_contract::Vec2f ai_component{};
    pipeline_contract::Vec2f pre_recoil{};
    pipeline_contract::Vec2f recoil_component{};
    pipeline_contract::Vec2f final_right{};
    pipeline_contract::Vec2f final_left{};
    std::uint64_t ads_epoch = 0;
    bool output_delivered = false;
    bool output_disabled = false;
    bool firing = false;
    bool recoil_active = false;
    bool saturated = false;
};

struct ControlIntegral {
    pipeline_contract::Vec2f physical_right_stick_seconds{};
    pipeline_contract::Vec2f physical_left_stick_seconds{};
    pipeline_contract::Vec2f manual_stick_seconds{};
    pipeline_contract::Vec2f ai_stick_seconds{};
    pipeline_contract::Vec2f pre_recoil_stick_seconds{};
    pipeline_contract::Vec2f recoil_stick_seconds{};
    pipeline_contract::Vec2f final_right_stick_seconds{};
    pipeline_contract::Vec2f final_left_stick_seconds{};
    std::uint64_t first_seq = 0;
    std::uint64_t last_seq = 0;
    std::uint32_t expected = 0;
    std::uint32_t written = 0;
    bool failed_delivery = false;
    bool output_disabled = false;
    bool firing = false;
    bool recoil_active = false;
    bool saturated = false;
    bool complete = false;
};

namespace detail {

struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

struct ComponentIntegral {
    Vec2d physical_right{};
    Vec2d physical_left{};
    Vec2d manual{};
    Vec2d ai{};
    Vec2d pre_recoil{};
    Vec2d recoil{};
    Vec2d final_right{};
    Vec2d final_left{};
};

inline Vec2d add_scaled(
    Vec2d base,
    pipeline_contract::Vec2f value,
    double seconds) noexcept {
    return {base.x + value.x * seconds, base.y + value.y * seconds};
}

inline pipeline_contract::Vec2f subtract(
    Vec2d a,
    Vec2d b) noexcept {
    return {static_cast<float>(a.x - b.x), static_cast<float>(a.y - b.y)};
}

inline ComponentIntegral advance(
    ComponentIntegral value,
    const DeliveredControlSample& held,
    double seconds) noexcept {
    value.physical_right = add_scaled(value.physical_right, held.physical_right, seconds);
    value.physical_left = add_scaled(value.physical_left, held.physical_left, seconds);
    value.manual = add_scaled(value.manual, held.manual_component, seconds);
    value.ai = add_scaled(value.ai, held.ai_component, seconds);
    value.pre_recoil = add_scaled(value.pre_recoil, held.pre_recoil, seconds);
    value.recoil = add_scaled(value.recoil, held.recoil_component, seconds);
    value.final_right = add_scaled(value.final_right, held.final_right, seconds);
    value.final_left = add_scaled(value.final_left, held.final_left, seconds);
    return value;
}

}  // namespace detail

template <std::size_t Capacity>
class ControlHistory {
    static_assert(Capacity > 0, "ControlHistory capacity must be positive");

    struct Entry {
        DeliveredControlSample sample{};
        DeliveredControlSample held_after{};
        detail::ComponentIntegral cumulative_at_sample{};
        std::uint64_t failed_prefix = 0;
        std::uint64_t disabled_prefix = 0;
        std::uint64_t firing_prefix = 0;
        std::uint64_t recoil_prefix = 0;
        std::uint64_t saturated_prefix = 0;
    };

public:
    bool push(const DeliveredControlSample& sample) noexcept {
        if (sample.sample_seq == 0 || sample.applied_at_ns == 0) return false;
        if (size_ != 0) {
            const Entry& previous = entry(size_ - 1);
            if (sample.applied_at_ns <= previous.sample.applied_at_ns ||
                sample.sample_seq <= previous.sample.sample_seq) {
                return false;
            }
        }

        Entry next{};
        next.sample = sample;
        if (size_ != 0) {
            const Entry& previous = entry(size_ - 1);
            const double seconds = static_cast<double>(
                sample.applied_at_ns - previous.sample.applied_at_ns) /
                1'000'000'000.0;
            next.cumulative_at_sample = detail::advance(
                previous.cumulative_at_sample, previous.held_after, seconds);
            next.held_after = sample.output_delivered
                ? sample : previous.held_after;
            next.failed_prefix = previous.failed_prefix;
            next.disabled_prefix = previous.disabled_prefix;
            next.firing_prefix = previous.firing_prefix;
            next.recoil_prefix = previous.recoil_prefix;
            next.saturated_prefix = previous.saturated_prefix;
        } else if (sample.output_delivered) {
            next.held_after = sample;
        }
        next.failed_prefix += sample.output_delivered ? 0u : 1u;
        next.disabled_prefix += sample.output_disabled ? 1u : 0u;
        next.firing_prefix += sample.firing ? 1u : 0u;
        next.recoil_prefix += sample.recoil_active ? 1u : 0u;
        next.saturated_prefix += sample.saturated ? 1u : 0u;

        if (size_ < Capacity) {
            entries_[(head_ + size_) % Capacity] = next;
            ++size_;
        } else {
            entries_[head_] = next;
            head_ = (head_ + 1) % Capacity;
            ++overwritten_;
        }
        return true;
    }

    ControlIntegral integrate(
        std::uint64_t begin_ns,
        std::uint64_t end_ns) const noexcept {
        ControlIntegral result;
        if (size_ == 0 || end_ns <= begin_ns ||
            end_ns <= oldest().applied_at_ns) {
            return result;
        }

        const std::uint64_t effective_begin =
            begin_ns < oldest().applied_at_ns ? oldest().applied_at_ns : begin_ns;
        const auto begin_value = cumulative_at(effective_begin);
        const auto end_value = cumulative_at(end_ns);
        result.physical_right_stick_seconds = detail::subtract(
            end_value.physical_right, begin_value.physical_right);
        result.physical_left_stick_seconds = detail::subtract(
            end_value.physical_left, begin_value.physical_left);
        result.manual_stick_seconds = detail::subtract(end_value.manual, begin_value.manual);
        result.ai_stick_seconds = detail::subtract(end_value.ai, begin_value.ai);
        result.pre_recoil_stick_seconds = detail::subtract(
            end_value.pre_recoil, begin_value.pre_recoil);
        result.recoil_stick_seconds = detail::subtract(end_value.recoil, begin_value.recoil);
        result.final_right_stick_seconds = detail::subtract(
            end_value.final_right, begin_value.final_right);
        result.final_left_stick_seconds = detail::subtract(
            end_value.final_left, begin_value.final_left);

        const std::size_t first = lower_bound_index(begin_ns);
        const std::size_t last = floor_index_strict(end_ns);
        if (first >= size_ || last >= size_ || last < first) return result;

        const Entry& first_entry = entry(first);
        const Entry& last_entry = entry(last);
        result.first_seq = first_entry.sample.sample_seq;
        result.last_seq = last_entry.sample.sample_seq;
        result.written = static_cast<std::uint32_t>(last - first + 1);
        const std::uint64_t expected = result.last_seq - result.first_seq + 1;
        result.expected = expected > UINT32_MAX
            ? UINT32_MAX : static_cast<std::uint32_t>(expected);
        result.failed_delivery = range_count(
            first, last, &Entry::failed_prefix,
            first_entry.sample.output_delivered ? 0u : 1u) != 0;
        result.output_disabled = range_count(
            first, last, &Entry::disabled_prefix,
            first_entry.sample.output_disabled ? 1u : 0u) != 0;
        result.firing = range_count(
            first, last, &Entry::firing_prefix,
            first_entry.sample.firing ? 1u : 0u) != 0;
        result.recoil_active = range_count(
            first, last, &Entry::recoil_prefix,
            first_entry.sample.recoil_active ? 1u : 0u) != 0;
        result.saturated = range_count(
            first, last, &Entry::saturated_prefix,
            first_entry.sample.saturated ? 1u : 0u) != 0;
        const bool truncated = overwritten_ != 0 && begin_ns < oldest().applied_at_ns;
        result.complete = result.written != 0 && !truncated &&
            result.expected == result.written && !result.failed_delivery;
        return result;
    }

    void clear() noexcept {
        head_ = 0;
        size_ = 0;
        overwritten_ = 0;
    }

    std::size_t size() const noexcept { return size_; }
    const DeliveredControlSample& oldest() const noexcept { return entry(0).sample; }
    const DeliveredControlSample& newest() const noexcept { return entry(size_ - 1).sample; }

private:
    const Entry& entry(std::size_t logical_index) const noexcept {
        return entries_[(head_ + logical_index) % Capacity];
    }

    std::size_t floor_index(std::uint64_t time_ns) const noexcept {
        std::size_t low = 0;
        std::size_t high = size_;
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2;
            if (entry(middle).sample.applied_at_ns <= time_ns) low = middle + 1;
            else high = middle;
        }
        return low == 0 ? 0 : low - 1;
    }

    std::size_t lower_bound_index(std::uint64_t time_ns) const noexcept {
        std::size_t low = 0;
        std::size_t high = size_;
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2;
            if (entry(middle).sample.applied_at_ns < time_ns) low = middle + 1;
            else high = middle;
        }
        return low;
    }

    std::size_t floor_index_strict(std::uint64_t time_ns) const noexcept {
        std::size_t low = 0;
        std::size_t high = size_;
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2;
            if (entry(middle).sample.applied_at_ns < time_ns) low = middle + 1;
            else high = middle;
        }
        return low == 0 ? size_ : low - 1;
    }

    detail::ComponentIntegral cumulative_at(std::uint64_t time_ns) const noexcept {
        if (time_ns <= oldest().applied_at_ns) return entry(0).cumulative_at_sample;
        const Entry& base = entry(floor_index(time_ns));
        const double seconds = static_cast<double>(
            time_ns - base.sample.applied_at_ns) / 1'000'000'000.0;
        return detail::advance(base.cumulative_at_sample, base.held_after, seconds);
    }

    using PrefixMember = std::uint64_t Entry::*;
    std::uint64_t range_count(
        std::size_t first,
        std::size_t last,
        PrefixMember member,
        std::uint64_t first_value) const noexcept {
        return entry(last).*member - entry(first).*member + first_value;
    }

    std::array<Entry, Capacity> entries_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t overwritten_ = 0;
};

static_assert(std::is_trivially_copyable_v<ControlHistory<1>>);

}  // namespace control_learning
