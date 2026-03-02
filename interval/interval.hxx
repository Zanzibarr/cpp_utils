#pragma once

/**
 * @file interval.hxx
 * @brief Closed interval [lo, hi] for arithmetic types, with queries and arithmetic.
 * @version 1.0.0
 *
 * @details
 * `Interval<T>` (requires `std::is_arithmetic_v<T>`) represents a closed
 * interval [lo, hi] and provides:
 *
 *   - **Construction** — from two endpoints, or from a center and half-width.
 *   - **Accessors** — `lo()`, `hi()`, `center()`, `half_width()`.
 *   - **Queries** — `contains(v)`, `contains(other)`, `overlaps(other)`,
 *     `length()`.
 *   - **Operations** — `intersect(other)`, `merge(other)`,
 *     `expand(amount)`, `translate(delta)`.
 *   - **Normalization** — `normalize()` swaps lo/hi if they are out of order
 *     (floating-point types only, as integral overflow is undefined).
 *   - **Operators** — `==`, `!=`, and `<<` for stream output.
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

/**
 * Closed interval [min, max] for arithmetic types.
 *
 * @tparam T Any arithmetic type (`std::is_arithmetic_v<T>` must be true).
 */
template <typename T>
    requires std::is_arithmetic_v<T>
class Interval {
   public:
    // ── Construction ────────────────────────────────────────────────────────

    /**
     * @param min Lower bound (inclusive).
     * @param max Upper bound (inclusive).
     * @throws std::invalid_argument if `min > max`.
     */
    Interval(T min, T max) {
        if (min > max) {
            throw std::invalid_argument("min cannot be greater than max");
        }
        min_ = min;
        max_ = max;
    }

    /** Returns an empty interval (min > max sentinel values). */
    static auto make_empty() -> Interval { return Interval(std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest(), private_tag{}); }

    /** Returns the largest representable interval [lowest, max]. */
    static auto make_universe() -> Interval { return Interval(std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max()); }

    // ── Accessors ───────────────────────────────────────────────────────────
    auto min() const -> T { return min_; }
    auto max() const -> T { return max_; }
    auto length() const -> T { return max_ - min_; }
    auto center() const -> T { return min_ + (length() / 2); }  // avoids overflow

    // ── Queries ─────────────────────────────────────────────────────────────
    auto contains(T value) const -> bool { return value >= min_ && value <= max_; }
    auto contains(const Interval& other) const -> bool { return other.min_ >= min_ && other.max_ <= max_; }
    auto overlaps(const Interval& other) const -> bool { return !is_empty() && !other.is_empty() && other.max_ >= min_ && other.min_ <= max_; }
    [[nodiscard]] auto is_empty() const -> bool { return min_ > max_; }

    // ── Operations ──────────────────────────────────────────────────────────

    /**
     * Clamps `value` to [min, max].
     * @throws std::logic_error if the interval is empty.
     */
    auto clamp(T value) const -> T {
        if (is_empty()) {
            throw std::logic_error("clamp() called on empty Interval");
        }
        return std::clamp(value, min_, max_);
    }

    /**
     * Returns the smallest interval containing both `*this` and `other`.
     * If either interval is empty, returns the other.
     */
    auto merge(const Interval& other) const -> Interval {
        if (is_empty()) {
            return other;
        }
        if (other.is_empty()) {
            return *this;
        }
        return Interval(std::min(min_, other.min_), std::max(max_, other.max_));
    }

    /**
     * Returns the intersection of `*this` and `other`, or `std::nullopt` if
     * they do not overlap.
     */
    auto intersect(const Interval& other) const -> std::optional<Interval> {
        T new_min = std::max(min_, other.min_);
        T new_max = std::min(max_, other.max_);
        if (new_min > new_max) {
            return std::nullopt;
        }
        return Interval(new_min, new_max);
    }

    /**
     * Returns an interval expanded by `amount` on both sides.
     * @throws std::invalid_argument if `amount < 0`.
     */
    auto expand(T amount) const -> Interval {
        if (amount < T{0}) {
            throw std::invalid_argument("expand() amount must be non-negative");
        }

        // Overflow or Underflow check
        if constexpr (std::is_integral_v<T>) {
            // underflow
            if (amount > 0 && min_ < std::numeric_limits<T>::min() + amount) {
                throw std::overflow_error("expand() would underflow min");
            }
            // overflow
            if (amount > 0 && max_ > std::numeric_limits<T>::max() - amount) {
                throw std::overflow_error("expand() would overflow max");
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            if (min_ - amount == -std::numeric_limits<T>::infinity() || max_ + amount == std::numeric_limits<T>::infinity()) {
                throw std::overflow_error("expand() would overflow");
            }
        }

        return Interval(min_ - amount, max_ + amount);
    }

    /** Returns an interval shifted by `offset`. */
    auto translate(T offset) const -> Interval {
        if constexpr (std::is_integral_v<T>) {
            if (offset > T{0} && max_ > std::numeric_limits<T>::max() - offset) {
                throw std::overflow_error("translate() would overflow max");
            }
            if (offset < T{0} && min_ < std::numeric_limits<T>::min() - offset) {
                throw std::overflow_error("translate() would underflow min");
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            if (min_ + offset == -std::numeric_limits<T>::infinity() || max_ + offset == std::numeric_limits<T>::infinity()) {
                throw std::overflow_error("translate() would overflow");
            }
        }

        return Interval(min_ + offset, max_ + offset);
    }

    // ── Normalization — floating point only ─────────────────────────────────

    /**
     * Maps `value` from [min, max] to [0, 1].
     * @throws std::logic_error if the interval is empty.
     * @note Requires `T` to be a floating-point type.
     */
    auto normalize(T value) const -> T {
        static_assert(std::is_floating_point_v<T>, "normalize() requires a floating point type");
        if (is_empty()) {
            throw std::logic_error("normalize() called on empty Interval");
        }
        return (value - min_) / length();
    }

    /**
     * Maps `norm_value` from [0, 1] back to [min, max].
     * @throws std::logic_error if the interval is empty.
     * @note Requires `T` to be a floating-point type.
     */
    auto denormalize(T norm_value) const -> T {
        static_assert(std::is_floating_point_v<T>, "denormalize() requires a floating point type");
        if (is_empty()) {
            throw std::logic_error("denormalize() called on empty Interval");
        }
        return min_ + (norm_value * length());
    }

    // ── Operators ───────────────────────────────────────────────────────────
    auto operator==(const Interval& other) const -> bool { return min_ == other.min_ && max_ == other.max_; }
    auto operator!=(const Interval& other) const -> bool { return !(*this == other); }

   private:
    struct private_tag {};

    // Private constructor that bypasses the min > max validation, used for make_empty()
    Interval(T min, T max, private_tag /*unused*/) : min_(min), max_(max) {}

    T min_;
    T max_;
};

/** Prints `[min, max]` or `[empty]` to the stream. */
template <typename T>
    requires std::is_arithmetic_v<T>
auto operator<<(std::ostream& out, const Interval<T>& inter) -> std::ostream& {
    if (inter.is_empty()) {
        return out << "[empty]";
    }
    return out << "[" << inter.min() << ", " << inter.max() << "]";
}
