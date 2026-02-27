#pragma once

/**
 * @file binary_set.hxx
 * @brief Compact binary set and related classes
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 *
 * =============================================================================
 * DESIGN OVERVIEW
 * =============================================================================
 *
 * BinarySet
 * ----------
 * A space-efficient set of unsigned integers over a fixed universe [0, N-1],
 * backed by a tightly-packed bit-array stored in 64-bit words (chunks).
 * Each bit position i encodes element presence: 1 = present, 0 = absent.
 *
 * Requires C++20 (std::popcount, std::countr_zero, concepts, [[nodiscard]]).
 *
 * BSSearcher
 * -----------
 * A binary-tree structure for fast subset queries over a collection of
 * BinarySets.  The tree has depth = capacity; each left edge encodes "element
 * absent", each right edge "element present".  find_subsets walks the tree
 * level-by-level, pruning branches that cannot possibly match the query.
 *
 * The node-pointer traversal now reuses pre-allocated vectors across calls
 * when invoked through find_subsets_reuse(), reducing heap pressure in hot
 * loops.
 *
 * =============================================================================
 */

#include <algorithm>  // std::all_of, std::fill, std::find
#include <bit>        // std::popcount, std::countr_zero  [C++20]
#include <cstddef>    // std::ptrdiff_t, std::size_t
#include <cstdint>    // uint64_t
#include <iterator>   // std::forward_iterator_tag
#include <memory>     // std::unique_ptr, std::make_unique
#include <ostream>    // std::ostream
#include <stdexcept>  // std::invalid_argument, std::domain_error, std::out_of_range
#include <string>     // std::string
#include <vector>     // std::vector

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------
namespace detail {
inline constexpr unsigned int CHUNK_BITS = 64U;          ///< Bits per storage word
using chunk_t = uint64_t;                                ///< Storage word type
inline constexpr chunk_t CHUNK_ALL = ~chunk_t{0};        ///< All bits set
inline constexpr std::size_t INITIAL_NODE_RESERVE = 16;  ///< Initial capacity for node vectors
}  // namespace detail

// ===========================================================================
//  BinarySet
// ===========================================================================

/**
 * @brief A space-efficient set of unsigned integers over a fixed universe.
 *
 * Elements are unsigned integers in the range [0, capacity-1].  Internally,
 * each element maps to one bit in a packed array of 64-bit words, so the
 * storage overhead is approximately capacity/8 bytes.
 *
 * ### Complexity summary
 * | Operation               | Time          | Notes                         |
 * |-------------------------|---------------|-------------------------------|
 * | add / remove / contains | O(1)          | Two arithmetic ops + bit test |
 * | size / empty            | O(1)          | Maintained as running counter |
 * | Set algebra (|, &, -, ^)| O(capacity/64)| Word-at-a-time                |
 * | Iteration               | O(capacity)   | One popcount per word         |
 * | sparse / string         | O(capacity)   | Full scan                     |
 *
 * ### Thread safety
 * No internal synchronisation.  Concurrent reads are safe; any concurrent
 * mutation requires external locking.
 *
 * ### Example
 * @code
 * BinarySet a(16), b(16);
 * a.add(1); a.add(3); a.add(5);
 * b.add(3); b.add(5); b.add(7);
 *
 * BinarySet u = a | b;   // {1,3,5,7}
 * BinarySet i = a & b;   // {3,5}
 * BinarySet d = a - b;   // {1}
 * BinarySet s = a ^ b;   // {1,7}
 *
 * for (unsigned int elem : a) { std::cout << elem << ' '; }   // 1 3 5
 * std::cout << a;                                             // [X-X-X-----------]
 * @endcode
 */
class BinarySet {
   public:
    // Forward declaration for begin()/end()
    class iterator;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /**
     * @brief Default constructor — creates an unusable empty set with capacity 0.
     *
     * A default-constructed set throws std::domain_error on any mutating or
     * querying operation.  It exists to allow storage in containers before
     * assignment.
     */
    BinarySet() noexcept = default;

    /**
     * @brief Constructs a BinarySet with the given universe size.
     *
     * @param capacity  Number of distinct representable elements;
     *                  elements are integers in [0, capacity-1].
     * @param fill_all  If @c true the set is initialised with every element
     *                  present; if @c false (default) the set is empty.
     *
     * @throw std::invalid_argument if @p capacity is 0.
     *
     * ### Example
     * @code
     * BinarySet empty_set(100);          // {}
     * BinarySet full_set(100, true);     // {0,1,...,99}
     * @endcode
     */
    explicit BinarySet(unsigned int capacity, bool fill_all = false) : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("Cannot explicitly create a BinarySet with capacity 0.");
        }
        const std::size_t n_chunks = chunks_needed(capacity_);
        chunks_.assign(n_chunks, fill_all ? detail::CHUNK_ALL : detail::chunk_t{0});

        if (fill_all) {
            mask_last_chunk();
            size_ = capacity_;
        }
    }

    // -----------------------------------------------------------------------
    // Element mutation
    // -----------------------------------------------------------------------

    /**
     * @brief Inserts @p element into the set.
     *
     * @param element  Must be in [0, capacity-1].
     * @return @c true  if the element was not already present and was inserted.
     * @return @c false if the element was already present (no-op).
     *
     * @throw std::domain_error   if capacity() == 0.
     * @throw std::out_of_range   if element >= capacity().
     */
    auto add(unsigned int element) -> bool {
        validate_element(element);
        if (test_bit(element)) {
            return false;
        }
        set_bit(element);
        ++size_;
        return true;
    }

    /**
     * @brief Removes @p element from the set.
     *
     * @param element  Must be in [0, capacity-1].
     * @return @c true  if the element was present and has been removed.
     * @return @c false if the element was absent (no-op).
     *
     * @throw std::domain_error   if capacity() == 0.
     * @throw std::out_of_range   if element >= capacity().
     */
    auto remove(unsigned int element) -> bool {
        validate_element(element);
        if (!test_bit(element)) {
            return false;
        }
        clear_bit(element);
        --size_;
        return true;
    }

    /**
     * @brief Removes all elements from the set.
     *
     * After this call size() == 0.  capacity() is unchanged.
     */
    void clear() noexcept {
        std::ranges::fill(chunks_, detail::chunk_t{0});
        size_ = 0;
    }

    /**
     * @brief Inserts every element in [0, capacity-1] into the set.
     *
     * After this call size() == capacity().  Equivalent to constructing with
     * @c fill_all = true.
     */
    void fill() {
        std::ranges::fill(chunks_, detail::CHUNK_ALL);
        mask_last_chunk();
        size_ = capacity_;
    }

    // -----------------------------------------------------------------------
    // Element queries
    // -----------------------------------------------------------------------

    /**
     * @brief Tests whether @p element is in the set.
     *
     * @param element  Must be in [0, capacity-1].
     * @return @c true if present, @c false otherwise.
     *
     * @throw std::domain_error  if capacity() == 0.
     * @throw std::out_of_range  if element >= capacity().
     */
    [[nodiscard]] auto contains(unsigned int element) const -> bool {
        validate_element(element);
        return test_bit(element);
    }

    /**
     * @brief Subscript read-only access — equivalent to contains().
     *
     * @param element  Must be in [0, capacity-1].
     *
     * @throw std::domain_error  if capacity() == 0.
     * @throw std::out_of_range  if element >= capacity().
     */
    [[nodiscard]] auto operator[](unsigned int element) const -> bool { return contains(element); }

    // -----------------------------------------------------------------------
    // Set-membership queries
    // -----------------------------------------------------------------------

    /**
     * @brief Tests whether every element of @p other is also in this set.
     *
     * Equivalently, tests @p other ⊆ *this.
     *
     * @param other  Must have the same capacity as *this.
     * @return @c true if @p other is a subset of this set.
     *
     * @throw std::invalid_argument if capacities differ.
     *
     * @see subset_of(), intersects()
     */
    [[nodiscard]] auto superset_of(const BinarySet& other) const -> bool {
        validate_same_capacity(other);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            // Bits present in other but absent in *this → not a superset
            if ((~chunks_[i] & other.chunks_[i]) != 0) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Tests whether every element of this set is also in @p other.
     *
     * Equivalently, tests *this ⊆ @p other.
     *
     * @param other  Must have the same capacity as *this.
     * @return @c true if this set is a subset of @p other.
     *
     * @throw std::invalid_argument if capacities differ.
     *
     * @see superset_of(), intersects()
     */
    [[nodiscard]] auto subset_of(const BinarySet& other) const -> bool { return other.superset_of(*this); }

    /**
     * @brief Tests whether this set and @p other share at least one element.
     *
     * @param other  Must have the same capacity as *this.
     * @return @c true if the intersection is non-empty.
     *
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto intersects(const BinarySet& other) const -> bool {
        validate_same_capacity(other);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            if ((chunks_[i] & other.chunks_[i]) != 0U) {
                return true;
            }
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Capacity and size
    // -----------------------------------------------------------------------

    /**
     * @brief Returns the fixed universe size (maximum number of elements).
     *
     * Elements are integers in [0, capacity()-1].
     */
    [[nodiscard]] auto capacity() const noexcept -> unsigned int { return capacity_; }

    /**
     * @brief Returns the number of elements currently in the set.
     *
     * Maintained as an O(1) counter; no bit-scan is performed.
     */
    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

    /**
     * @brief Tests whether the set is empty (size() == 0).
     *
     * O(1) — uses the cached size counter.
     */
    [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }

    // -----------------------------------------------------------------------
    // Bulk conversion
    // -----------------------------------------------------------------------

    /**
     * @brief Returns all present elements as a sorted vector.
     *
     * Elements are returned in strictly ascending order.
     *
     * @return std::vector<unsigned int> of elements in [0, capacity-1].
     *
     * @throw std::domain_error if capacity() == 0.
     */
    [[nodiscard]] auto sparse() const -> std::vector<unsigned int> {
        if (capacity_ == 0) {
            throw std::domain_error("This BinarySet has a capacity of 0.");
        }
        return {begin(), end()};
    }

    /**
     * @brief Returns a compact visual representation of the set.
     *
     * Format: @c '[' followed by @c 'X' for each present element and @c '-'
     * for each absent element, then @c ']'.
     *
     * @par Example
     * A set with elements {0, 3, 5} and capacity 8:
     * @code
     * "[X--X-X--]"
     * @endcode
     *
     * @note Use operator<< for direct streaming; this explicit conversion is
     *       provided for contexts that require a std::string value.
     */
    [[nodiscard]] explicit operator std::string() const {
        std::string result;
        result.reserve(static_cast<std::size_t>(capacity_) + 2);
        result.push_back('[');
        for (unsigned int i = 0; i < capacity_; ++i) {
            result.push_back(test_bit_unchecked(i) ? 'X' : '-');
        }
        result.push_back(']');
        return result;
    }

    /**
     * @brief Streams the set's string representation to @p os.
     *
     * @see operator std::string()
     */
    friend auto operator<<(std::ostream& ostr, const BinarySet& bin_set) -> std::ostream& { return ostr << static_cast<std::string>(bin_set); }

    // -----------------------------------------------------------------------
    // Set algebra — non-mutating
    // -----------------------------------------------------------------------

    /**
     * @brief Returns the intersection of *this and @p other  (A ∩ B).
     *
     * The result contains every element present in **both** sets.
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto operator&(const BinarySet& other) const -> BinarySet {
        validate_same_capacity(other);
        BinarySet result(capacity_);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            result.chunks_[i] = chunks_[i] & other.chunks_[i];
        }
        result.recalculate_size();
        return result;
    }

    /**
     * @brief Returns the union of *this and @p other  (A ∪ B).
     *
     * The result contains every element present in **either** set.
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto operator|(const BinarySet& other) const -> BinarySet {
        validate_same_capacity(other);
        BinarySet result(capacity_);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            result.chunks_[i] = chunks_[i] | other.chunks_[i];
        }
        result.recalculate_size();
        return result;
    }

    /**
     * @brief Returns the set difference  (A \ B).
     *
     * The result contains elements present in *this but **not** in @p other.
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto operator-(const BinarySet& other) const -> BinarySet {
        validate_same_capacity(other);
        BinarySet result(capacity_);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            result.chunks_[i] = chunks_[i] & ~other.chunks_[i];
        }
        result.recalculate_size();
        return result;
    }

    /**
     * @brief Returns the symmetric difference  (A △ B).
     *
     * The result contains elements present in **exactly one** of the two sets.
     * Equivalent to (A | B) - (A & B), but computed in a single pass.
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto operator^(const BinarySet& other) const -> BinarySet {
        validate_same_capacity(other);
        BinarySet result(capacity_);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            result.chunks_[i] = chunks_[i] ^ other.chunks_[i];
        }
        result.recalculate_size();
        return result;
    }

    /**
     * @brief Returns the complement of *this  (∁A).
     *
     * The result contains every element in [0, capacity-1] that is **not** in
     * *this.
     *
     * @note Unary @c ! is used because unary @c ~ is not overloadable on class
     *       types in C++.
     */
    [[nodiscard]] auto operator!() const -> BinarySet {
        if (capacity_ == 0) {
            throw std::domain_error("Cannot complement a BinarySet with capacity 0.");
        }

        BinarySet result(capacity_);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            result.chunks_[i] = ~chunks_[i];
        }
        result.mask_last_chunk();
        result.recalculate_size();
        return result;
    }

    // -----------------------------------------------------------------------
    // Set algebra — mutating (in-place)
    // -----------------------------------------------------------------------

    /**
     * @brief In-place intersection  (*this = *this ∩ other).
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    auto operator&=(const BinarySet& other) -> BinarySet& {
        validate_same_capacity(other);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            chunks_[i] &= other.chunks_[i];
        }
        recalculate_size();
        return *this;
    }

    /**
     * @brief In-place union  (*this = *this ∪ other).
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    auto operator|=(const BinarySet& other) -> BinarySet& {
        validate_same_capacity(other);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            chunks_[i] |= other.chunks_[i];
        }
        recalculate_size();
        return *this;
    }

    /**
     * @brief In-place set difference  (*this = *this \ other).
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    auto operator-=(const BinarySet& other) -> BinarySet& {
        validate_same_capacity(other);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            chunks_[i] &= ~other.chunks_[i];
        }
        recalculate_size();
        return *this;
    }

    /**
     * @brief In-place symmetric difference  (*this = *this △ other).
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    auto operator^=(const BinarySet& other) -> BinarySet& {
        validate_same_capacity(other);
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            chunks_[i] ^= other.chunks_[i];
        }
        recalculate_size();
        return *this;
    }

    // -----------------------------------------------------------------------
    // Equality
    // -----------------------------------------------------------------------

    /**
     * @brief Tests whether *this and @p other contain exactly the same elements.
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto operator==(const BinarySet& other) const -> bool {
        validate_same_capacity(other);
        return chunks_ == other.chunks_;
    }

    /**
     * @brief Tests whether *this and @p other differ in at least one element.
     *
     * @param other  Must have the same capacity as *this.
     * @throw std::invalid_argument if capacities differ.
     */
    [[nodiscard]] auto operator!=(const BinarySet& other) const -> bool {
        validate_same_capacity(other);
        return chunks_ != other.chunks_;
    }

    // -----------------------------------------------------------------------
    // Iteration
    // -----------------------------------------------------------------------

    /**
     * @brief Returns a forward iterator to the smallest element in the set.
     *
     * Iterates elements in strictly ascending order.  Invalidated by any
     * mutation (add, remove, clear, fill, in-place operators).
     *
     * @return iterator pointing to the first element, or end() if the set is
     *         empty.
     */
    [[nodiscard]] auto begin() const noexcept -> iterator { return {this, 0U}; }

    /**
     * @brief Returns the past-the-end iterator.
     *
     * @return iterator representing the end position (value == capacity()).
     */
    [[nodiscard]] auto end() const noexcept -> iterator { return {this, capacity_}; }

    // -----------------------------------------------------------------------
    // Iterator
    // -----------------------------------------------------------------------

    /**
     * @brief Forward iterator over elements present in the set.
     *
     * Advances word-by-word using std::countr_zero to skip absent elements in
     * O(1) amortised per step instead of scanning bit-by-bit.
     *
     * @note The iterator stores a raw pointer to the parent BinarySet.  Any
     *       mutation of the parent invalidates all outstanding iterators.
     */
    class iterator {
       public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = unsigned int;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        /**
         * @brief Constructs an iterator starting at position @p pos.
         *
         * If @p pos does not correspond to a set element, the iterator
         * advances to the next set element automatically.
         */
        iterator(const BinarySet* set, unsigned int pos) noexcept : bs_(set), current_pos_(pos) {
            if (current_pos_ < bs_->capacity_ && !bs_->test_bit_unchecked(current_pos_)) {
                advance();
            }
        }

        /** @brief Pre-increment: advance to the next element. */
        auto operator++() -> iterator& {
            ++current_pos_;
            if (current_pos_ < bs_->capacity_) {
                advance();
            }
            return *this;
        }

        /** @brief Post-increment. */
        auto operator++(int) -> iterator {
            const iterator tmp{*this};
            ++(*this);
            return tmp;
        }

        /** @brief Dereferences the iterator to obtain the current element. */
        [[nodiscard]] auto operator*() const noexcept -> value_type { return current_pos_; }

        [[nodiscard]] auto operator==(const iterator& other) const noexcept -> bool { return current_pos_ == other.current_pos_; }
        [[nodiscard]] auto operator!=(const iterator& other) const noexcept -> bool { return !(*this == other); }

       private:
        const BinarySet* bs_;
        unsigned int current_pos_;

        /**
         * @brief Advances current_pos_ to the next set bit, starting from
         *        the current position.
         *
         * Uses std::countr_zero to skip over zero-bits in 64-bit chunks,
         * which is typically a single instruction (BSF/TZCNT on x86).
         */
        void advance() noexcept {
            while (current_pos_ < bs_->capacity_) {
                const unsigned int chunk_idx = current_pos_ / detail::CHUNK_BITS;
                const unsigned int bit_idx = current_pos_ % detail::CHUNK_BITS;

                // Mask out bits below current position in the current chunk
                const detail::chunk_t remaining = bs_->chunks_[chunk_idx] >> bit_idx;

                if (remaining != 0) {
                    // std::countr_zero gives the offset of the lowest set bit
                    current_pos_ += static_cast<unsigned int>(std::countr_zero(remaining));
                    if (current_pos_ < bs_->capacity_) {
                        return;
                    }
                    break;
                }
                // Move to the start of the next chunk
                current_pos_ = (chunk_idx + 1U) * detail::CHUNK_BITS;
            }
            // Clamp to capacity (end sentinel)
            current_pos_ = std::min(current_pos_, bs_->capacity_);
        }
    };

   private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    static auto chunks_needed(unsigned int capacity) noexcept -> std::size_t {
        return (static_cast<std::size_t>(capacity) + detail::CHUNK_BITS - 1) / detail::CHUNK_BITS;
    }

    // Low-level bit accessors — no bounds checking.
    [[nodiscard]] auto test_bit_unchecked(unsigned int element) const noexcept -> bool {
        return ((chunks_[element / detail::CHUNK_BITS] >> (element % detail::CHUNK_BITS)) & detail::chunk_t{1}) != 0U;
    }
    [[nodiscard]] auto test_bit(unsigned int element) const noexcept -> bool { return test_bit_unchecked(element); }
    void set_bit(unsigned int element) noexcept { chunks_[element / detail::CHUNK_BITS] |= detail::chunk_t{1} << (element % detail::CHUNK_BITS); }
    void clear_bit(unsigned int element) noexcept {
        chunks_[element / detail::CHUNK_BITS] &= ~(detail::chunk_t{1} << (element % detail::CHUNK_BITS));
    }

    /**
     * @brief Clears the unused high bits of the last chunk.
     *
     * When capacity is not a multiple of CHUNK_BITS, the final chunk has
     * bits beyond index (capacity-1) that must remain zero.  This is called
     * after any operation that might set those bits (fill, complement).
     */
    void mask_last_chunk() noexcept {
        if (capacity_ == 0) {
            return;
        }
        const unsigned int tail = capacity_ % detail::CHUNK_BITS;
        if (tail != 0) {
            // Keep only the lowest `tail` bits of the last word
            chunks_.back() &= (detail::chunk_t{1} << tail) - 1U;
        }
    }

    /**
     * @brief Recomputes size_ by summing std::popcount over all chunks.
     *
     * Called after bulk operations (&=, |=, -=, ^=, complement).
     * std::popcount compiles to a single POPCNT instruction on modern CPUs.
     */
    void recalculate_size() noexcept {
        size_ = 0;
        for (detail::chunk_t chunk : chunks_) {
            size_ += static_cast<std::size_t>(std::popcount(chunk));
        }
    }

    // Validation helpers
    void validate_element(unsigned int element) const {
        if (capacity_ == 0) {
            throw std::domain_error("This BinarySet has a capacity of 0.");
        }
        if (element >= capacity_) {
            throw std::out_of_range("Specified element is outside the valid range [0, capacity-1].");
        }
    }

    void validate_same_capacity(const BinarySet& other) const {
        if (capacity_ != other.capacity_) {
            throw std::invalid_argument("The two BinarySets do not have the same capacity.");
        }
    }

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    unsigned int capacity_{0};
    std::size_t size_{0};
    std::vector<detail::chunk_t> chunks_;

    // BSSearcher needs direct chunk access for efficient traversal
    friend class BSSearcher;
};

// ===========================================================================
//  BSSearcher
// ===========================================================================

/**
 * @brief Trie-based structure for efficient subset lookups over a collection
 *        of BinarySets.
 *
 * ### Structure
 * Internally, the searcher builds a binary trie of depth @c capacity.  Each
 * level @c i corresponds to element @c i of the universe.  An edge going
 * **left** encodes "element absent"; an edge going **right** encodes "element
 * present".  A leaf (a node at depth == capacity) stores the user-supplied
 * identifiers of all sets that were registered via add().
 *
 * ### Subset query
 * Given a query set @c Q, a stored set @c S is a subset of @c Q iff for every
 * element @c i: if S contains @c i then Q also contains @c i.  During the
 * traversal, when element @c i is absent from @c Q we can only follow the left
 * edge (element absent in S); when element @c i is present in @c Q we may
 * follow both edges (S may or may not have it).  This prunes the search space
 * to only matching paths.
 *
 * ### Complexity
 * | Operation      | Time                                       |
 * |----------------|--------------------------------------------|
 * | add            | O(capacity)                                |
 * | remove         | O(capacity)                                |
 * | find_subsets   | O(capacity × number_of_live_paths)         |
 *
 * In the worst case (all stored sets are subsets of the query) every stored
 * path is visited.  In practice the pruning is highly effective.
 *
 * ### Example
 * @code
 * BSSearcher idx(10);
 *
 * BinarySet s1(10); s1.add(1); s1.add(3);
 * BinarySet s2(10); s2.add(1); s2.add(5);
 * idx.add(101, s1);
 * idx.add(102, s2);
 *
 * BinarySet query(10);
 * query.add(1); query.add(3); query.add(7);
 *
 * auto result = idx.find_subsets(query);
 * // result == {101}  (s2 is not a subset because element 5 ∉ query)
 * @endcode
 */
class BSSearcher {
   private:
    struct treenode {
        std::vector<unsigned int> values;  ///< IDs stored at this leaf path
        std::unique_ptr<treenode> left;    ///< "element absent" child
        std::unique_ptr<treenode> right;   ///< "element present" child

        treenode() = default;
    };

   public:
    /**
     * @brief Constructs the searcher for BinarySets of the given capacity.
     *
     * All sets passed to add(), remove(), and find_subsets() must have exactly
     * this capacity.
     *
     * @param capacity  Universe size; must match BinarySet::capacity() of
     *                  every set used with this searcher.
     */
    explicit BSSearcher(unsigned int capacity) : root_(std::make_unique<treenode>()), capacity_(capacity) {}

    // -----------------------------------------------------------------------
    // Mutation
    // -----------------------------------------------------------------------

    /**
     * @brief Registers a BinarySet under the given identifier.
     *
     * Multiple sets with the same @p value may be registered.  Multiple
     * registrations of the same (@p value, @p set) pair are allowed and each
     * will produce an independent entry.
     *
     * @param value  Arbitrary unsigned integer used to identify this set in
     *               find_subsets() results.
     * @param set     The BinarySet to register; must have capacity().
     *
     * @throw std::invalid_argument if set.capacity() != capacity().
     */
    void add(unsigned int value, const BinarySet& set) {
        validate_capacity(set);

        treenode* node = root_.get();
        for (unsigned int i = 0; i < capacity_; ++i) {
            if (set.test_bit_unchecked(i)) {
                if (!node->right) {
                    node->right = std::make_unique<treenode>();
                }
                node = node->right.get();
            } else {
                if (!node->left) {
                    node->left = std::make_unique<treenode>();
                }
                node = node->left.get();
            }
        }
        node->values.push_back(value);
    }

    /**
     * @brief Removes one registration of the given (@p value, @p set) pair.
     *
     * If multiple identical registrations exist, only one is removed (the
     * first match, by insertion order).  After removal, empty branches are
     * pruned from the trie.
     *
     * @param value  Identifier passed to add().
     * @param set     BinarySet passed to add(); must have capacity().
     * @return @c true  if a matching registration was found and removed.
     * @return @c false if no matching registration existed.
     *
     * @throw std::invalid_argument if set.capacity() != capacity().
     */
    auto remove(unsigned int value, const BinarySet& set) -> bool {
        validate_capacity(set);

        // Record the path for later pruning
        std::vector<treenode*> path;
        std::vector<bool> went_right;
        path.reserve(capacity_);
        went_right.reserve(capacity_);

        treenode* node = root_.get();
        for (unsigned int i = 0; i < capacity_ && (node != nullptr); ++i) {
            path.push_back(node);
            bool right = set.test_bit_unchecked(i);
            went_right.push_back(right);
            node = right ? node->right.get() : node->left.get();
        }
        if (node == nullptr) {
            return false;
        }

        // Remove one occurrence of value (swap-and-pop for O(1) erase)
        auto iter = std::find(node->values.begin(), node->values.end(), value);
        if (iter == node->values.end()) {
            return false;
        }
        if (iter != node->values.end() - 1) {
            *iter = node->values.back();
        }
        node->values.pop_back();

        // Prune empty branches bottom-up
        if (node->values.empty() && !node->left && !node->right) {
            for (std::size_t i = path.size(); i > 0; --i) {
                treenode* parent = path[i - 1];
                if (went_right[i - 1]) {
                    parent->right.reset();
                } else {
                    parent->left.reset();
                }

                if (!parent->values.empty() || parent->left || parent->right) {
                    break;
                }
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    /**
     * @brief Returns the identifiers of all registered sets that are subsets
     *        of @p query.
     *
     * A registered set @c S is included in the result iff S ⊆ @p query, i.e.
     * every element in @c S is also in @p query.
     *
     * The order of identifiers in the returned vector is unspecified.  If the
     * same (value, set) pair was registered multiple times, the value appears
     * multiple times in the result.
     *
     * @param query  The query set; must have capacity().
     * @return std::vector<unsigned int> of matching identifiers (may be empty).
     *
     * @throw std::invalid_argument if query.capacity() != capacity().
     *
     * @see find_subsets_into() for a variant that avoids heap allocation.
     */
    [[nodiscard]]
    std::vector<unsigned int> find_subsets(const BinarySet& query) const {
        std::vector<unsigned int> result;
        find_subsets_into(query, result);
        return result;
    }

    /**
     * @brief Like find_subsets(), but appends results into a caller-supplied
     *        vector.
     *
     * Use this overload in tight loops to reuse the same result buffer and
     * avoid repeated heap allocation.
     *
     * @param query   The query set; must have capacity().
     * @param[out] out  Vector to which matching identifiers are appended.
     *                  Existing contents are not cleared.
     *
     * @throw std::invalid_argument if query.capacity() != capacity().
     */
    void find_subsets_into(const BinarySet& query, std::vector<unsigned int>& out) const {
        validate_capacity(query);

        // Level-by-level BFS through the trie.
        // We maintain two alternating vectors of active nodes.
        std::vector<const treenode*> current;
        std::vector<const treenode*> next;
        current.reserve(detail::INITIAL_NODE_RESERVE);
        next.reserve(detail::INITIAL_NODE_RESERVE);

        if (root_) {
            current.push_back(root_.get());
        }

        for (unsigned int i = 0; i < capacity_ && !current.empty(); ++i) {
            next.clear();
            const bool q_has_i = query.test_bit_unchecked(i);

            for (const treenode* node : current) {
                // Element absent branch: always reachable (S may omit element i)
                if (node->left) {
                    next.push_back(node->left.get());
                }
                // Element present branch: only reachable if query also has element i
                if (q_has_i && node->right) {
                    next.push_back(node->right.get());
                }
            }

            current.swap(next);
        }

        // Collect all values from surviving leaf nodes
        for (const treenode* node : current) {
            out.insert(out.end(), node->values.begin(), node->values.end());
        }
    }

    /**
     * @brief Returns the capacity this searcher was constructed with.
     */
    [[nodiscard]] unsigned int capacity() const noexcept { return capacity_; }

   private:
    std::unique_ptr<treenode> root_;
    unsigned int capacity_;

    void validate_capacity(const BinarySet& set) const {
        if (capacity_ != set.capacity()) {
            throw std::invalid_argument("The BinarySet has an unexpected capacity.");
        }
    }
};