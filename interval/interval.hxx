#pragma once

/**
 * @file interval.hxx
 * @brief Simple Interval class for numeric types, with common operations and queries.
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>

template <typename T>
    requires std::is_arithmetic_v<T>
class Interval {
   public:
    // Construction
    Interval(T min, T max) {
        if (min > max) {
            throw std::invalid_argument("min cannot be greater than max");
        }
        min_ = min;
        max_ = max;
    }

    static auto make_empty() -> Interval { return Interval(std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest(), private_tag{}); }

    static auto make_universe() -> Interval { return Interval(std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()); }

    // Accessors
    auto min() const -> T { return min_; }
    auto max() const -> T { return max_; }
    auto length() const -> T { return max_ - min_; }
    auto center() const -> T { return min_ + (length() / 2); }  // avoids overflow

    // Queries
    auto contains(T value) const -> bool { return value >= min_ && value <= max_; }
    auto contains(const Interval& other) const -> bool { return other.min_ >= min_ && other.max_ <= max_; }
    auto overlaps(const Interval& other) const -> bool { return !is_empty() && !other.is_empty() && other.max_ >= min_ && other.min_ <= max_; }
    [[nodiscard]] auto is_empty() const -> bool { return min_ > max_; }

    // Operations
    auto clamp(T value) const -> T {
        if (is_empty()) {
            throw std::logic_error("clamp() called on empty Interval");
        }
        return std::clamp(value, min_, max_);
    }

    auto merge(const Interval& other) const -> Interval {
        if (is_empty()) {
            return other;
        }
        if (other.is_empty()) {
            return *this;
        }
        return Interval(std::min(min_, other.min_), std::max(max_, other.max_));
    }

    auto intersect(const Interval& other) const -> std::optional<Interval> {
        T new_min = std::max(min_, other.min_);
        T new_max = std::min(max_, other.max_);
        if (new_min > new_max) {
            return std::nullopt;
        }
        return Interval(new_min, new_max);
    }

    auto expand(T amount) const -> Interval {
        if (amount < T{0}) {
            throw std::invalid_argument("expand() amount must be non-negative");
        }
        return Interval(min_ - amount, max_ + amount);
    }

    auto translate(T offset) const -> Interval { return Interval(min_ + offset, max_ + offset); }

    // Normalization — floating point only
    auto normalize(T value) const -> T {
        static_assert(std::is_floating_point_v<T>, "normalize() requires a floating point type");
        if (is_empty()) {
            throw std::logic_error("normalize() called on empty Interval");
        }
        return (value - min_) / length();
    }

    auto denormalize(T norm_value) const -> T {
        static_assert(std::is_floating_point_v<T>, "denormalize() requires a floating point type");
        if (is_empty()) {
            throw std::logic_error("denormalize() called on empty Interval");
        }
        return min_ + (norm_value * length());
    }

    // Operators
    auto operator==(const Interval& other) const -> bool { return min_ == other.min_ && max_ == other.max_; }
    auto operator!=(const Interval& other) const -> bool { return !(*this == other); }

   private:
    struct private_tag {};

    // Private constructor that bypasses the min > max validation, used for make_empty()
    Interval(T min, T max, private_tag /*unused*/) : min_(min), max_(max) {}

    T min_;
    T max_;
};

template <typename T>
    requires std::is_arithmetic_v<T>
auto operator<<(std::ostream& out, const Interval<T>& inter) -> std::ostream& {
    if (inter.is_empty()) {
        return out << "[empty]";
    }
    return out << "[" << inter.min() << ", " << inter.max() << "]";
}
