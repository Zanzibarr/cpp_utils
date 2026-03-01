/**
 * demo_binary_set.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * A comprehensive demo for BinarySet.
 * Every public API is exercised; each section is self-contained.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 demo_binary_set.cpp -o demo_bs && ./demo_bs
 */

#include <iomanip>
#include <iostream>
#include <string>

#include "binary_set.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Small helpers
// ─────────────────────────────────────────────────────────────────────────────

static void section(const std::string& title) {
    const int W = 70;
    std::cout << "\n" << std::string(W, '=') << "\n";
    int pad = (W - static_cast<int>(title.size()) - 2) / 2;
    std::cout << std::string(static_cast<std::size_t>(std::max(0, pad)), ' ') << "[ " << title << " ]\n" << std::string(W, '=') << "\n\n";
}

static void subsection(const std::string& title) { std::cout << "\n── " << title << " ──────────────────────────────────\n"; }

// Prints a BinarySet's name, visual representation, and element list.
static void print_set(const std::string& name, const BinarySet& bs) {
    std::cout << std::setw(6) << name << " = " << bs << "  size=" << bs.size() << "  {";
    bool first = true;
    for (unsigned int e : bs) {
        if (!first) std::cout << ", ";
        std::cout << e;
        first = false;
    }
    std::cout << "}\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — Construction
// ─────────────────────────────────────────────────────────────────────────────

void demo_construction() {
    section("1 · Construction");

    subsection("1a. Default constructor (capacity 0 — unusable placeholder)");
    BinarySet placeholder;
    std::cout << "placeholder.capacity() = " << placeholder.capacity() << "\n";
    std::cout << "placeholder.size()     = " << placeholder.size() << "\n";
    std::cout << "placeholder.empty()    = " << std::boolalpha << placeholder.empty() << "\n";

    subsection("1b. Empty set with explicit capacity");
    BinarySet empty(12);
    print_set("empty", empty);

    subsection("1c. Fully filled set (fill_all = true)");
    BinarySet full(12, true);
    print_set("full", full);

    subsection("1d. Copy construction");
    BinarySet copy = full;
    print_set("copy", copy);
    copy.remove(0);
    copy.remove(11);
    std::cout << "After removing 0 and 11 from copy:\n";
    print_set("copy", copy);
    std::cout << "Original full is unchanged:\n";
    print_set("full", full);

    subsection("1e. Invalid construction — throws std::invalid_argument");
    try {
        BinarySet bad(0);
    } catch (const std::invalid_argument& ex) {
        std::cout << "Caught expected exception: " << ex.what() << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — Element mutation
// ─────────────────────────────────────────────────────────────────────────────

void demo_mutation() {
    section("2 · Element Mutation");

    BinarySet bs(10);

    subsection("2a. add() — returns true on first insert, false if already present");
    std::cout << "add(3): " << std::boolalpha << bs.add(3) << "\n";
    std::cout << "add(3): " << bs.add(3) << "  (already present)\n";
    std::cout << "add(7): " << bs.add(7) << "\n";
    std::cout << "add(9): " << bs.add(9) << "\n";
    print_set("bs", bs);

    subsection("2b. remove() — returns true on successful removal");
    std::cout << "remove(7): " << bs.remove(7) << "\n";
    std::cout << "remove(7): " << bs.remove(7) << "  (already absent)\n";
    print_set("bs", bs);

    subsection("2c. clear() — removes all elements");
    bs.clear();
    print_set("bs", bs);
    std::cout << "empty() = " << bs.empty() << "\n";

    subsection("2d. fill() — inserts every element in [0, capacity-1]");
    bs.fill();
    print_set("bs", bs);
    std::cout << "size() == capacity(): " << (bs.size() == bs.capacity()) << "\n";

    subsection("2e. Out-of-range add — throws std::out_of_range");
    try {
        bs.add(bs.capacity());
    } catch (const std::out_of_range& ex) {
        std::cout << "Caught expected exception: " << ex.what() << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3 — Element queries
// ─────────────────────────────────────────────────────────────────────────────

void demo_queries() {
    section("3 · Element Queries");

    BinarySet bs(16);
    for (unsigned int e : {1u, 4u, 7u, 10u, 13u}) bs.add(e);
    print_set("bs", bs);

    subsection("3a. contains()");
    std::cout << "contains(4):  " << std::boolalpha << bs.contains(4) << "\n";
    std::cout << "contains(5):  " << bs.contains(5) << "\n";
    std::cout << "contains(13): " << bs.contains(13) << "\n";

    subsection("3b. operator[] — identical to contains()");
    std::cout << "bs[7]:  " << bs[7] << "\n";
    std::cout << "bs[8]:  " << bs[8] << "\n";

    subsection("3c. size() and empty()");
    std::cout << "size():  " << bs.size() << "\n";
    std::cout << "empty(): " << bs.empty() << "\n";
    BinarySet empty_bs(16);
    std::cout << "empty set empty(): " << empty_bs.empty() << "\n";

    subsection("3d. capacity()");
    std::cout << "capacity(): " << bs.capacity() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 4 — Set-membership queries
// ─────────────────────────────────────────────────────────────────────────────

void demo_membership() {
    section("4 · Set-Membership Queries");

    BinarySet large(16, true);  // {0..15}
    BinarySet small(16);
    small.add(2);
    small.add(5);
    small.add(9);

    BinarySet disjoint(16);
    disjoint.add(0);
    disjoint.add(6);
    disjoint.add(12);

    print_set("large", large);
    print_set("small", small);
    print_set("disjoint", disjoint);

    subsection("4a. subset_of()");
    std::cout << "small.subset_of(large):   " << std::boolalpha << small.subset_of(large) << "  (expected true)\n";
    std::cout << "large.subset_of(small):   " << large.subset_of(small) << "  (expected false)\n";
    std::cout << "small.subset_of(disjoint):" << small.subset_of(disjoint) << "  (expected false)\n";

    subsection("4b. superset_of()");
    std::cout << "large.superset_of(small):   " << large.superset_of(small) << "  (expected true)\n";
    std::cout << "small.superset_of(large):   " << small.superset_of(large) << "  (expected false)\n";

    subsection("4c. intersects()");
    std::cout << "small.intersects(large):    " << small.intersects(large) << "  (expected true)\n";
    std::cout << "small.intersects(disjoint): " << small.intersects(disjoint) << "  (expected false)\n";

    subsection("4d. Capacity mismatch — throws std::invalid_argument");
    BinarySet other(8);
    try {
        small.subset_of(other);
    } catch (const std::invalid_argument& ex) {
        std::cout << "Caught expected exception: " << ex.what() << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5 — Set algebra (non-mutating)
// ─────────────────────────────────────────────────────────────────────────────

void demo_algebra() {
    section("5 · Set Algebra (Non-Mutating)");

    BinarySet a(16), b(16);
    for (unsigned int e : {1u, 3u, 5u, 7u, 9u}) a.add(e);
    for (unsigned int e : {3u, 5u, 6u, 8u, 9u}) b.add(e);
    print_set("a", a);
    print_set("b", b);

    subsection("5a. operator& — intersection (A ∩ B)");
    print_set("a&b", a & b);

    subsection("5b. operator| — union (A ∪ B)");
    print_set("a|b", a | b);

    subsection("5c. operator- — set difference (A \\ B)");
    print_set("a-b", a - b);
    print_set("b-a", b - a);

    subsection("5d. operator^ — symmetric difference (A △ B)");
    print_set("a^b", a ^ b);

    subsection("5e. operator! — complement (∁A)");
    print_set("!a", !a);
    print_set("!b", !b);

    subsection("5f. Chaining operators");
    // Elements in exactly one of a or b, excluding element 6
    BinarySet excl(16);
    excl.add(6);
    BinarySet result = (a ^ b) - excl;
    print_set("(a^b)-{6}", result);

    subsection("5g. Algebraic identities");
    BinarySet universe(16, true);
    BinarySet empty_set(16);

    std::cout << "a | !a == universe: " << ((a | !a) == universe) << "\n";
    std::cout << "a & !a == empty:    " << ((a & !a) == empty_set) << "\n";
    std::cout << "a | empty == a:     " << ((a | empty_set) == a) << "\n";
    std::cout << "a & universe == a:  " << ((a & universe) == a) << "\n";
    std::cout << "a - a == empty:     " << ((a - a) == empty_set) << "\n";
    std::cout << "!!a == a:           " << ((!(!a)) == a) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 6 — In-place algebra
// ─────────────────────────────────────────────────────────────────────────────

void demo_inplace_algebra() {
    section("6 · In-Place Algebra");

    BinarySet a(16), b(16);
    for (unsigned int e : {1u, 3u, 5u, 7u, 9u}) a.add(e);
    for (unsigned int e : {3u, 5u, 6u, 8u, 9u}) b.add(e);

    subsection("6a. operator&= — in-place intersection");
    BinarySet t = a;
    t &= b;
    print_set("a&=b", t);

    subsection("6b. operator|= — in-place union");
    t = a;
    t |= b;
    print_set("a|=b", t);

    subsection("6c. operator-= — in-place set difference");
    t = a;
    t -= b;
    print_set("a-=b", t);

    subsection("6d. operator^= — in-place symmetric difference");
    t = a;
    t ^= b;
    print_set("a^=b", t);

    subsection("6e. In-place ops do not affect the right-hand operand");
    t = a;
    t &= b;
    std::cout << "b unchanged after a&=b: " << std::boolalpha;
    BinarySet b_check(16);
    for (unsigned int e : {3u, 5u, 6u, 8u, 9u}) b_check.add(e);
    std::cout << (b == b_check) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 7 — Equality
// ─────────────────────────────────────────────────────────────────────────────

void demo_equality() {
    section("7 · Equality");

    BinarySet a(8), b(8), c(8);
    a.add(1);
    a.add(3);
    a.add(5);
    b.add(1);
    b.add(3);
    b.add(5);
    c.add(1);
    c.add(3);

    print_set("a", a);
    print_set("b", b);
    print_set("c", c);

    std::cout << "a == b: " << std::boolalpha << (a == b) << "  (expected true)\n";
    std::cout << "a == c: " << (a == c) << "  (expected false)\n";
    std::cout << "a != c: " << (a != c) << "  (expected true)\n";
    std::cout << "a != b: " << (a != b) << "  (expected false)\n";

    subsection("Self-equality");
    std::cout << "a == a: " << (a == a) << "\n";

    subsection("Empty sets with same capacity are equal");
    BinarySet e1(8), e2(8);
    std::cout << "empty(8) == empty(8): " << (e1 == e2) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 8 — Iteration
// ─────────────────────────────────────────────────────────────────────────────

void demo_iteration() {
    section("8 · Iteration");

    subsection("8a. Range-for — sparse set");
    BinarySet sparse(20);
    for (unsigned int e : {2u, 7u, 11u, 19u}) sparse.add(e);
    print_set("sparse", sparse);
    std::cout << "Elements: ";
    for (unsigned int e : sparse) std::cout << e << " ";
    std::cout << "\n";

    subsection("8b. Range-for — dense set");
    BinarySet dense(10, true);
    dense.remove(3);
    dense.remove(6);
    dense.remove(9);
    print_set("dense", dense);
    std::cout << "Elements: ";
    for (unsigned int e : dense) std::cout << e << " ";
    std::cout << "\n";

    subsection("8c. Range-for — empty set (no iterations)");
    BinarySet empty(8);
    std::cout << "Iterating empty set: ";
    for (unsigned int e : empty) std::cout << e << " ";
    std::cout << "(nothing)\n";

    subsection("8d. Range-for — full set");
    BinarySet full(8, true);
    std::cout << "Full set elements: ";
    for (unsigned int e : full) std::cout << e << " ";
    std::cout << "\n";

    subsection("8e. Computing sum and max via iteration");
    BinarySet bs(64);
    for (unsigned int e : {4u, 8u, 15u, 16u, 23u, 42u}) bs.add(e);
    unsigned int sum = 0, max_elem = 0;
    for (unsigned int e : bs) {
        sum += e;
        max_elem = e;
    }
    std::cout << "Set: ";
    for (unsigned int e : bs) std::cout << e << " ";
    std::cout << "\n";
    std::cout << "Sum: " << sum << "  Max: " << max_elem << "\n";

    subsection("8f. Iterating across chunk boundaries (capacity > 64)");
    BinarySet cross(130);
    cross.add(0);    // chunk 0, bit 0
    cross.add(63);   // chunk 0, bit 63
    cross.add(64);   // chunk 1, bit 0
    cross.add(128);  // chunk 2, bit 0
    cross.add(129);  // chunk 2, bit 1
    print_set("cross", cross);
    std::cout << "Elements: ";
    for (unsigned int e : cross) std::cout << e << " ";
    std::cout << "\n";

    subsection("8g. Post-increment iterator");
    BinarySet small(8);
    small.add(2);
    small.add(5);
    small.add(7);
    auto it = small.begin();
    std::cout << "First (post-increment): " << *it++ << "\n";
    std::cout << "Second:                 " << *it++ << "\n";
    std::cout << "Third:                  " << *it << "\n";
    std::cout << "At end: " << std::boolalpha << (++it == small.end()) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 9 — Bulk conversion
// ─────────────────────────────────────────────────────────────────────────────

void demo_conversion() {
    section("9 · Bulk Conversion");

    subsection("9a. sparse() — returns sorted vector of elements");
    BinarySet bs(16);
    for (unsigned int e : {0u, 3u, 6u, 9u, 12u, 15u}) bs.add(e);
    print_set("bs", bs);
    auto vec = bs.sparse();
    std::cout << "sparse() = { ";
    for (unsigned int e : vec) std::cout << e << " ";
    std::cout << "}\n";

    subsection("9b. operator std::string — visual representation");
    std::cout << "string: " << static_cast<std::string>(bs) << "\n";

    subsection("9c. operator<< — stream directly");
    std::cout << "stream: " << bs << "\n";

    subsection("9d. sparse() on empty set");
    BinarySet empty(8);
    auto empty_vec = empty.sparse();
    std::cout << "sparse() on empty set size: " << empty_vec.size() << "\n";

    subsection("9e. sparse() on full set");
    BinarySet full(8, true);
    auto full_vec = full.sparse();
    std::cout << "sparse() on full set: { ";
    for (unsigned int e : full_vec) std::cout << e << " ";
    std::cout << "}\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 10 — Practical example: task dependency tracking
// ─────────────────────────────────────────────────────────────────────────────

void demo_practical() {
    section("10 · Practical Example — Task Dependency Tracking");

    std::cout << "We have 8 tasks (IDs 0–7). Each task has a set of prerequisites.\n"
                 "We track which tasks are 'done' and find which tasks are now runnable.\n\n";

    // Prerequisites for each task — stored as BinarySets over universe [0,8).
    constexpr unsigned int N = 8;
    BinarySet prereqs[N] = {
        BinarySet(N),  // task 0: no prerequisites
        BinarySet(N),  // task 1: no prerequisites
        BinarySet(N),  // task 2: depends on 0, 1
        BinarySet(N),  // task 3: depends on 1
        BinarySet(N),  // task 4: depends on 2, 3
        BinarySet(N),  // task 5: depends on 0
        BinarySet(N),  // task 6: depends on 4, 5
        BinarySet(N),  // task 7: depends on 3, 6
    };
    prereqs[2].add(0);
    prereqs[2].add(1);
    prereqs[3].add(1);
    prereqs[4].add(2);
    prereqs[4].add(3);
    prereqs[5].add(0);
    prereqs[6].add(4);
    prereqs[6].add(5);
    prereqs[7].add(3);
    prereqs[7].add(6);

    BinarySet done(N);  // initially nothing is done
    BinarySet all_tasks(N, true);

    auto print_runnable = [&]() {
        BinarySet pending = all_tasks - done;
        std::cout << "  Done:    " << done << "\n";
        std::cout << "  Runnable: { ";
        for (unsigned int t : pending) {
            if (prereqs[t].subset_of(done)) std::cout << t << " ";
        }
        std::cout << "}\n";
    };

    std::cout << "Initial state:\n";
    print_runnable();

    auto complete = [&](unsigned int task) {
        done.add(task);
        std::cout << "\nCompleted task " << task << ":\n";
        print_runnable();
    };

    complete(0);
    complete(1);
    complete(5);
    complete(2);
    complete(3);
    complete(4);
    complete(6);
    complete(7);

    std::cout << "\nAll tasks done: " << std::boolalpha << (done == all_tasks) << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::string(70, '-') << "\n"
              << "  BinarySet Demo\n"
              << std::string(70, '-') << "\n";

    demo_construction();
    demo_mutation();
    demo_queries();
    demo_membership();
    demo_algebra();
    demo_inplace_algebra();
    demo_equality();
    demo_iteration();
    demo_conversion();
    demo_practical();

    std::cout << "\n"
              << std::string(70, '-') << "\n"
              << "  Demo complete.\n"
              << std::string(70, '-') << "\n";
    return 0;
}