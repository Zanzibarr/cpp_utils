#pragma once

/**
 * @file ct_string.hxx
 * @brief Compile-time string type and FNV-1a hash for use as template parameters.
 * @version 1.0.0
 */

#include <algorithm>
#include <cstddef>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// ct_string — a string literal usable as a non-type template parameter (C++20)
// ─────────────────────────────────────────────────────────────────────────────

template <std::size_t N>
struct ct_string {
    char data[N]{};

    consteval ct_string(const char (&str)[N]) noexcept { std::copy_n(str, N, data); }

    [[nodiscard]] consteval auto view() const noexcept -> std::string_view {
        return {data, N - 1};  // exclude null terminator
    }

    consteval operator std::string_view() const noexcept { return view(); }

    // Two ct_strings are equal if their contents match — used by the compiler
    // to deduplicate template instantiations for the same literal.
    template <std::size_t M>
    consteval auto operator==(const ct_string<M>& other) const noexcept -> bool {
        if constexpr (N != M) {
            return false;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (data[i] != other.data[i]) {
                return false;
            }
        }
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// hash_name — consteval FNV-1a 64-bit hash of a string_view
//
// Used to produce a unique compile-time integer from a ct_string so it can
// drive template specialisations and slot-array indexing.
// Collision probability over 64 bits is negligible for any realistic number
// of timer/counter/gauge names.
// ─────────────────────────────────────────────────────────────────────────────

consteval auto hash_name(std::string_view s) noexcept -> std::size_t {
    std::size_t hash = 14695981039346656037ULL;
    for (char c : s) {
        hash ^= static_cast<std::size_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}