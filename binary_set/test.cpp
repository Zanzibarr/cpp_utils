#include "../binary_set/binary_set.hxx"
#include "../testing/test_main.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("construction")

TEST_CASE("default constructor creates a set with capacity 0") {
    BinarySet s;
    expect(s.capacity()).to_equal(0u);
    expect(s.size()).to_equal(0u);
    expect(s.empty()).to_be_true();
}

TEST_CASE("explicit constructor stores capacity correctly") {
    BinarySet s(16);
    expect(s.capacity()).to_equal(16u);
}

TEST_CASE("explicit constructor creates an empty set by default") {
    BinarySet s(16);
    expect(s.empty()).to_be_true();
    expect(s.size()).to_equal(0u);
}

TEST_CASE("fill_all=true fills the set completely") {
    BinarySet s(16, true);
    expect(s.size()).to_equal(16u);
    expect(s.empty()).to_be_false();
}

TEST_CASE("fill_all=true sets all elements in range") {
    BinarySet s(10, true);
    for (unsigned int i = 0; i < 10; ++i) {
        expect(s.contains(i)).to_be_true();
    }
}

TEST_CASE("fill_all=false leaves all elements absent") {
    BinarySet s(10, false);
    for (unsigned int i = 0; i < 10; ++i) {
        expect(s.contains(i)).to_be_false();
    }
}

TEST_CASE("constructor with capacity 0 throws invalid_argument") { expect_throws(std::invalid_argument, BinarySet(0)); }

TEST_CASE("constructor with large capacity works") {
    BinarySet s(100000);
    expect(s.capacity()).to_equal(100000u);
    expect(s.size()).to_equal(0u);
}

TEST_CASE("constructor with capacity not a multiple of 64 works") {
    BinarySet s(70, true);
    expect(s.size()).to_equal(70u);
    for (unsigned int i = 0; i < 70; ++i) {
        expect(s.contains(i)).to_be_true();
    }
}

TEST_CASE("full set with non-multiple-of-64 capacity has no stray bits") {
    // If stray bits leaked past capacity, complement would produce wrong size
    BinarySet s(65, true);
    expect(s.size()).to_equal(65u);
    BinarySet c = !s;
    expect(c.size()).to_equal(0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// add
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("add")

TEST_CASE("add returns true when element is newly inserted") {
    BinarySet s(10);
    expect(s.add(5)).to_be_true();
}

TEST_CASE("add returns false when element already present") {
    BinarySet s(10);
    s.add(5);
    expect(s.add(5)).to_be_false();
}

TEST_CASE("add increases size by one") {
    BinarySet s(10);
    s.add(3);
    expect(s.size()).to_equal(1u);
    s.add(7);
    expect(s.size()).to_equal(2u);
}

TEST_CASE("add does not increase size when element already present") {
    BinarySet s(10);
    s.add(3);
    s.add(3);
    expect(s.size()).to_equal(1u);
}

TEST_CASE("add element at index 0") {
    BinarySet s(10);
    s.add(0);
    expect(s.contains(0)).to_be_true();
}

TEST_CASE("add element at last valid index") {
    BinarySet s(10);
    s.add(9);
    expect(s.contains(9)).to_be_true();
}

TEST_CASE("add element on word boundary (index 63)") {
    BinarySet s(128);
    s.add(63);
    expect(s.contains(63)).to_be_true();
    expect(s.size()).to_equal(1u);
}

TEST_CASE("add element just past word boundary (index 64)") {
    BinarySet s(128);
    s.add(64);
    expect(s.contains(64)).to_be_true();
    expect(s.size()).to_equal(1u);
}

TEST_CASE("add throws out_of_range for element >= capacity") {
    BinarySet s(10);
    expect_throws(std::out_of_range, s.add(10));
}

TEST_CASE("add throws domain_error on default-constructed set") {
    BinarySet s;
    expect_throws(std::domain_error, s.add(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// remove
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("remove")

TEST_CASE("remove returns true when element was present") {
    BinarySet s(10);
    s.add(5);
    expect(s.remove(5)).to_be_true();
}

TEST_CASE("remove returns false when element was absent") {
    BinarySet s(10);
    expect(s.remove(5)).to_be_false();
}

TEST_CASE("remove decreases size by one") {
    BinarySet s(10);
    s.add(3);
    s.add(7);
    s.remove(3);
    expect(s.size()).to_equal(1u);
}

TEST_CASE("remove makes element absent") {
    BinarySet s(10);
    s.add(5);
    s.remove(5);
    expect(s.contains(5)).to_be_false();
}

TEST_CASE("remove does not affect other elements") {
    BinarySet s(10);
    s.add(3);
    s.add(5);
    s.remove(3);
    expect(s.contains(5)).to_be_true();
    expect(s.contains(3)).to_be_false();
}

TEST_CASE("remove throws out_of_range for element >= capacity") {
    BinarySet s(10);
    expect_throws(std::out_of_range, s.remove(10));
}

TEST_CASE("remove throws domain_error on default-constructed set") {
    BinarySet s;
    expect_throws(std::domain_error, s.remove(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// clear
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("clear")

TEST_CASE("clear empties a non-empty set") {
    BinarySet s(10);
    s.add(1);
    s.add(3);
    s.add(9);
    s.clear();
    expect(s.empty()).to_be_true();
    expect(s.size()).to_equal(0u);
}

TEST_CASE("clear on already-empty set is a no-op") {
    BinarySet s(10);
    s.clear();
    expect(s.empty()).to_be_true();
}

TEST_CASE("clear removes all elements") {
    BinarySet s(10);
    s.add(0);
    s.add(5);
    s.add(9);
    s.clear();
    for (unsigned int i = 0; i < 10; ++i) {
        expect(s.contains(i)).to_be_false();
    }
}

TEST_CASE("clear preserves capacity") {
    BinarySet s(10);
    s.add(1);
    s.clear();
    expect(s.capacity()).to_equal(10u);
}

TEST_CASE("elements can be re-added after clear") {
    BinarySet s(10);
    s.add(5);
    s.clear();
    expect(s.add(5)).to_be_true();
    expect(s.size()).to_equal(1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// fill
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("fill")

TEST_CASE("fill sets size to capacity") {
    BinarySet s(16);
    s.fill();
    expect(s.size()).to_equal(16u);
}

TEST_CASE("fill makes every element present") {
    BinarySet s(16);
    s.fill();
    for (unsigned int i = 0; i < 16; ++i) {
        expect(s.contains(i)).to_be_true();
    }
}

TEST_CASE("fill on already-full set is a no-op") {
    BinarySet s(16, true);
    s.fill();
    expect(s.size()).to_equal(16u);
}

TEST_CASE("fill works for capacity not a multiple of 64") {
    BinarySet s(100);
    s.fill();
    expect(s.size()).to_equal(100u);
}

TEST_CASE("fill then complement gives empty set") {
    BinarySet s(20);
    s.fill();
    BinarySet c = !s;
    expect(c.empty()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// contains / operator[]
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("contains and operator[]")

TEST_CASE("contains returns true for added element") {
    BinarySet s(10);
    s.add(4);
    expect(s.contains(4)).to_be_true();
}

TEST_CASE("contains returns false for absent element") {
    BinarySet s(10);
    expect(s.contains(4)).to_be_false();
}

TEST_CASE("operator[] returns true for added element") {
    BinarySet s(10);
    s.add(7);
    expect(s[7]).to_be_true();
}

TEST_CASE("operator[] returns false for absent element") {
    BinarySet s(10);
    expect(s[3]).to_be_false();
}

TEST_CASE("contains throws out_of_range for element >= capacity") {
    BinarySet s(10);
    expect_throws(std::out_of_range, s.contains(10));
}

TEST_CASE("contains throws domain_error on default-constructed set") {
    BinarySet s;
    expect_throws(std::domain_error, s.contains(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// size / empty
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("size and empty")

TEST_CASE("empty set has size 0") {
    BinarySet s(10);
    expect(s.size()).to_equal(0u);
    expect(s.empty()).to_be_true();
}

TEST_CASE("set with one element has size 1 and is not empty") {
    BinarySet s(10);
    s.add(0);
    expect(s.size()).to_equal(1u);
    expect(s.empty()).to_be_false();
}

TEST_CASE("size tracks additions correctly") {
    BinarySet s(10);
    for (unsigned int i = 0; i < 10; ++i) {
        s.add(i);
        expect(s.size()).to_equal(static_cast<std::size_t>(i + 1));
    }
}

TEST_CASE("size tracks removals correctly") {
    BinarySet s(10, true);
    for (unsigned int i = 0; i < 10; ++i) {
        s.remove(i);
        expect(s.size()).to_equal(static_cast<std::size_t>(9 - i));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// superset_of / subset_of
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("superset_of and subset_of")

TEST_CASE("full set is superset of any set") {
    BinarySet full(10, true);
    BinarySet other(10);
    other.add(1);
    other.add(5);
    expect(full.superset_of(other)).to_be_true();
}

TEST_CASE("empty set is subset of any set") {
    BinarySet empty(10);
    BinarySet other(10);
    other.add(1);
    other.add(5);
    expect(empty.subset_of(other)).to_be_true();
}

TEST_CASE("set is superset of itself") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    expect(s.superset_of(s)).to_be_true();
}

TEST_CASE("set is subset of itself") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    expect(s.subset_of(s)).to_be_true();
}

TEST_CASE("strict superset relationship holds") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    a.add(3);
    b.add(1);
    b.add(3);
    expect(a.superset_of(b)).to_be_true();
    expect(b.superset_of(a)).to_be_false();
}

TEST_CASE("strict subset relationship holds") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    a.add(3);
    b.add(1);
    b.add(3);
    expect(b.subset_of(a)).to_be_true();
    expect(a.subset_of(b)).to_be_false();
}

TEST_CASE("disjoint sets are not supersets of each other") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    b.add(3);
    b.add(4);
    expect(a.superset_of(b)).to_be_false();
    expect(b.superset_of(a)).to_be_false();
}

TEST_CASE("superset_of throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a.superset_of(b));
}

TEST_CASE("subset_of throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a.subset_of(b));
}

// ─────────────────────────────────────────────────────────────────────────────
// intersects
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("intersects")

TEST_CASE("two overlapping sets intersect") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    b.add(2);
    b.add(3);
    expect(a.intersects(b)).to_be_true();
}

TEST_CASE("two disjoint sets do not intersect") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    b.add(3);
    b.add(4);
    expect(a.intersects(b)).to_be_false();
}

TEST_CASE("set intersects itself when non-empty") {
    BinarySet s(10);
    s.add(5);
    expect(s.intersects(s)).to_be_true();
}

TEST_CASE("empty set does not intersect any set") {
    BinarySet empty(10);
    BinarySet other(10);
    other.add(1);
    expect(empty.intersects(other)).to_be_false();
    expect(other.intersects(empty)).to_be_false();
}

TEST_CASE("intersects is symmetric") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(5);
    b.add(5);
    b.add(9);
    expect(a.intersects(b)).to_equal(b.intersects(a));
}

TEST_CASE("intersects throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a.intersects(b));
}

// ─────────────────────────────────────────────────────────────────────────────
// operator& (intersection)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("operator& (intersection)")

TEST_CASE("intersection contains shared elements only") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    a.add(5);
    b.add(3);
    b.add(5);
    b.add(7);
    BinarySet i = a & b;
    expect(i.contains(3)).to_be_true();
    expect(i.contains(5)).to_be_true();
    expect(i.contains(1)).to_be_false();
    expect(i.contains(7)).to_be_false();
    expect(i.size()).to_equal(2u);
}

TEST_CASE("intersection of disjoint sets is empty") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    b.add(3);
    b.add(4);
    expect((a & b).empty()).to_be_true();
}

TEST_CASE("intersection with itself is itself") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    expect((s & s) == s).to_be_true();
}

TEST_CASE("intersection with full set is itself") {
    BinarySet s(10), full(10, true);
    s.add(1);
    s.add(7);
    expect((s & full) == s).to_be_true();
}

TEST_CASE("intersection with empty set is empty") {
    BinarySet s(10), empty(10);
    s.add(1);
    s.add(7);
    expect((s & empty).empty()).to_be_true();
}

TEST_CASE("intersection is commutative") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    b.add(3);
    b.add(5);
    expect((a & b) == (b & a)).to_be_true();
}

TEST_CASE("operator& throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a & b);
}

// ─────────────────────────────────────────────────────────────────────────────
// operator| (union)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("operator| (union)")

TEST_CASE("union contains all elements from both sets") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    b.add(3);
    b.add(5);
    BinarySet u = a | b;
    expect(u.contains(1)).to_be_true();
    expect(u.contains(3)).to_be_true();
    expect(u.contains(5)).to_be_true();
    expect(u.size()).to_equal(3u);
}

TEST_CASE("union with itself is itself") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    expect((s | s) == s).to_be_true();
}

TEST_CASE("union with empty set is itself") {
    BinarySet s(10), empty(10);
    s.add(1);
    s.add(7);
    expect((s | empty) == s).to_be_true();
}

TEST_CASE("union with full set is full set") {
    BinarySet s(10), full(10, true);
    s.add(1);
    expect((s | full) == full).to_be_true();
}

TEST_CASE("union is commutative") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    b.add(3);
    b.add(5);
    expect((a | b) == (b | a)).to_be_true();
}

TEST_CASE("operator| throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a | b);
}

// ─────────────────────────────────────────────────────────────────────────────
// operator- (set difference)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("operator- (set difference)")

TEST_CASE("difference removes elements of subtracted set") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    a.add(5);
    b.add(3);
    b.add(5);
    b.add(7);
    BinarySet d = a - b;
    expect(d.contains(1)).to_be_true();
    expect(d.contains(3)).to_be_false();
    expect(d.contains(5)).to_be_false();
    expect(d.size()).to_equal(1u);
}

TEST_CASE("difference with empty set is itself") {
    BinarySet s(10), empty(10);
    s.add(2);
    s.add(4);
    expect((s - empty) == s).to_be_true();
}

TEST_CASE("difference with itself is empty") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    expect((s - s).empty()).to_be_true();
}

TEST_CASE("difference with full set is empty") {
    BinarySet s(10), full(10, true);
    s.add(1);
    s.add(7);
    expect((s - full).empty()).to_be_true();
}

TEST_CASE("difference is not commutative in general") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    b.add(2);
    b.add(3);
    expect((a - b) == (b - a)).to_be_false();
}

TEST_CASE("operator- throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a - b);
}

// ─────────────────────────────────────────────────────────────────────────────
// operator^ (symmetric difference)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("operator^ (symmetric difference)")

TEST_CASE("symmetric difference contains elements in exactly one set") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    a.add(5);
    b.add(3);
    b.add(5);
    b.add(7);
    BinarySet s = a ^ b;
    expect(s.contains(1)).to_be_true();
    expect(s.contains(7)).to_be_true();
    expect(s.contains(3)).to_be_false();
    expect(s.contains(5)).to_be_false();
    expect(s.size()).to_equal(2u);
}

TEST_CASE("symmetric difference with itself is empty") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    expect((s ^ s).empty()).to_be_true();
}

TEST_CASE("symmetric difference with empty set is itself") {
    BinarySet s(10), empty(10);
    s.add(1);
    s.add(7);
    expect((s ^ empty) == s).to_be_true();
}

TEST_CASE("symmetric difference is commutative") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    b.add(3);
    b.add(5);
    expect((a ^ b) == (b ^ a)).to_be_true();
}

TEST_CASE("symmetric difference equals (a|b)-(a&b)") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(2);
    a.add(3);
    b.add(2);
    b.add(3);
    b.add(4);
    BinarySet sym_diff = a ^ b;
    BinarySet union_sub = (a | b) - (a & b);
    expect(sym_diff == union_sub).to_be_true();
}

TEST_CASE("operator^ throws invalid_argument for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a ^ b);
}

// ─────────────────────────────────────────────────────────────────────────────
// operator! (complement)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("operator! (complement)")

TEST_CASE("complement of empty set is full set") {
    BinarySet s(10);
    BinarySet c = !s;
    expect(c.size()).to_equal(10u);
}

TEST_CASE("complement of full set is empty set") {
    BinarySet s(10, true);
    BinarySet c = !s;
    expect(c.empty()).to_be_true();
}

TEST_CASE("complement is an involution") {
    BinarySet s(10);
    s.add(1);
    s.add(5);
    s.add(9);
    expect((!(!s)) == s).to_be_true();
}

TEST_CASE("complement has correct size") {
    BinarySet s(10);
    s.add(1);
    s.add(2);
    s.add(3);
    BinarySet c = !s;
    expect(c.size()).to_equal(7u);
}

TEST_CASE("complement does not contain original elements") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    s.add(6);
    BinarySet c = !s;
    expect(c.contains(2)).to_be_false();
    expect(c.contains(4)).to_be_false();
    expect(c.contains(6)).to_be_false();
}

TEST_CASE("complement contains elements absent from original") {
    BinarySet s(10);
    s.add(2);
    s.add(4);
    BinarySet c = !s;
    expect(c.contains(0)).to_be_true();
    expect(c.contains(1)).to_be_true();
    expect(c.contains(3)).to_be_true();
}

TEST_CASE("complement of default-constructed set throws domain_error") {
    BinarySet s;
    expect_throws(std::domain_error, !s);
}

TEST_CASE("complement works correctly for non-multiple-of-64 capacity") {
    BinarySet s(65, true);
    BinarySet c = !s;
    expect(c.size()).to_equal(0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// In-place operators (&=, |=, -=, ^=)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("in-place operators")

TEST_CASE("operator&= produces same result as operator&") {
    BinarySet a(10), b(10), a2(10);
    a.add(1);
    a.add(3);
    a.add(5);
    a2.add(1);
    a2.add(3);
    a2.add(5);
    b.add(3);
    b.add(5);
    b.add(7);
    a2 &= b;
    expect(a2 == (a & b)).to_be_true();
}

TEST_CASE("operator|= produces same result as operator|") {
    BinarySet a(10), b(10), a2(10);
    a.add(1);
    a.add(3);
    a2.add(1);
    a2.add(3);
    b.add(3);
    b.add(5);
    a2 |= b;
    expect(a2 == (a | b)).to_be_true();
}

TEST_CASE("operator-= produces same result as operator-") {
    BinarySet a(10), b(10), a2(10);
    a.add(1);
    a.add(3);
    a.add(5);
    a2.add(1);
    a2.add(3);
    a2.add(5);
    b.add(3);
    b.add(5);
    a2 -= b;
    expect(a2 == (a - b)).to_be_true();
}

TEST_CASE("operator^= produces same result as operator^") {
    BinarySet a(10), b(10), a2(10);
    a.add(1);
    a.add(3);
    a.add(5);
    a2.add(1);
    a2.add(3);
    a2.add(5);
    b.add(3);
    b.add(5);
    b.add(7);
    a2 ^= b;
    expect(a2 == (a ^ b)).to_be_true();
}

TEST_CASE("operator&= updates size correctly") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    a.add(5);
    b.add(3);
    b.add(5);
    b.add(7);
    a &= b;
    expect(a.size()).to_equal(2u);
}

TEST_CASE("operator|= updates size correctly") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(3);
    b.add(3);
    b.add(5);
    a |= b;
    expect(a.size()).to_equal(3u);
}

TEST_CASE("operator&= throws for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a &= b);
}

TEST_CASE("operator|= throws for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a |= b);
}

TEST_CASE("operator-= throws for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a -= b);
}

TEST_CASE("operator^= throws for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a ^= b);
}

// ─────────────────────────────────────────────────────────────────────────────
// operator== / operator!=
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("equality operators")

TEST_CASE("two empty sets of same capacity are equal") {
    BinarySet a(10), b(10);
    expect(a == b).to_be_true();
    expect(a != b).to_be_false();
}

TEST_CASE("two full sets of same capacity are equal") {
    BinarySet a(10, true), b(10, true);
    expect(a == b).to_be_true();
}

TEST_CASE("sets with same elements are equal") {
    BinarySet a(10), b(10);
    a.add(1);
    a.add(5);
    b.add(1);
    b.add(5);
    expect(a == b).to_be_true();
    expect(a != b).to_be_false();
}

TEST_CASE("sets with different elements are not equal") {
    BinarySet a(10), b(10);
    a.add(1);
    b.add(2);
    expect(a == b).to_be_false();
    expect(a != b).to_be_true();
}

TEST_CASE("operator== throws for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a == b);
}

TEST_CASE("operator!= throws for different capacities") {
    BinarySet a(10), b(20);
    expect_throws(std::invalid_argument, a != b);
}

// ─────────────────────────────────────────────────────────────────────────────
// sparse
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("sparse")

TEST_CASE("sparse on empty set returns empty vector") {
    BinarySet s(10);
    auto v = s.sparse();
    expect(v.empty()).to_be_true();
}

TEST_CASE("sparse returns elements in ascending order") {
    BinarySet s(10);
    s.add(7);
    s.add(2);
    s.add(5);
    auto v = s.sparse();
    expect(v.size()).to_equal(3u);
    expect(v[0]).to_equal(2u);
    expect(v[1]).to_equal(5u);
    expect(v[2]).to_equal(7u);
}

TEST_CASE("sparse returns all elements for a full set") {
    BinarySet s(8, true);
    auto v = s.sparse();
    expect(v.size()).to_equal(8u);
    for (unsigned int i = 0; i < 8; ++i) {
        expect(v[i]).to_equal(i);
    }
}

TEST_CASE("sparse throws domain_error on default-constructed set") {
    BinarySet s;
    expect_throws(std::domain_error, s.sparse());
}

// ─────────────────────────────────────────────────────────────────────────────
// operator std::string / operator<<
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("string conversion")

TEST_CASE("string representation of empty set is all dashes") {
    BinarySet s(5);
    expect(static_cast<std::string>(s)).to_equal("[-----]");
}

TEST_CASE("string representation of full set is all X") {
    BinarySet s(5, true);
    expect(static_cast<std::string>(s)).to_equal("[XXXXX]");
}

TEST_CASE("string representation has correct X and dash positions") {
    BinarySet s(8);
    s.add(0);
    s.add(3);
    s.add(5);
    expect(static_cast<std::string>(s)).to_equal("[X--X-X--]");
}

TEST_CASE("string representation length equals capacity plus 2") {
    BinarySet s(12);
    std::string str = static_cast<std::string>(s);
    expect(str.size()).to_equal(14u);
}

TEST_CASE("operator<< streams same content as string conversion") {
    BinarySet s(6);
    s.add(1);
    s.add(4);
    std::ostringstream oss;
    oss << s;
    expect(oss.str()).to_equal(static_cast<std::string>(s));
}

// ─────────────────────────────────────────────────────────────────────────────
// Iteration
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("iteration")

TEST_CASE("iterating empty set yields no elements") {
    BinarySet s(10);
    int count = 0;
    for (unsigned int elem : s) {
        (void)elem;
        ++count;
    }
    expect(count).to_equal(0);
}

TEST_CASE("iteration visits all elements in ascending order") {
    BinarySet s(20);
    s.add(1);
    s.add(5);
    s.add(12);
    s.add(19);
    std::vector<unsigned int> visited;
    for (unsigned int elem : s) {
        visited.push_back(elem);
    }
    expect(visited.size()).to_equal(4u);
    expect(visited[0]).to_equal(1u);
    expect(visited[1]).to_equal(5u);
    expect(visited[2]).to_equal(12u);
    expect(visited[3]).to_equal(19u);
}

TEST_CASE("iteration over full set visits exactly capacity elements") {
    BinarySet s(64, true);
    std::size_t count = 0;
    for (unsigned int elem : s) {
        (void)elem;
        ++count;
    }
    expect(count).to_equal(64u);
}

TEST_CASE("begin equals end for empty set") {
    BinarySet s(10);
    expect(s.begin() == s.end()).to_be_true();
}

TEST_CASE("begin does not equal end for non-empty set") {
    BinarySet s(10);
    s.add(5);
    expect(s.begin() != s.end()).to_be_true();
}

TEST_CASE("dereferencing begin gives smallest element") {
    BinarySet s(10);
    s.add(7);
    s.add(2);
    s.add(9);
    expect(*s.begin()).to_equal(2u);
}

TEST_CASE("iteration works across word boundaries") {
    BinarySet s(130);
    s.add(63);
    s.add(64);
    s.add(128);
    auto v = s.sparse();
    expect(v.size()).to_equal(3u);
    expect(v[0]).to_equal(63u);
    expect(v[1]).to_equal(64u);
    expect(v[2]).to_equal(128u);
}

TEST_CASE("post-increment iterator returns old value") {
    BinarySet s(10);
    s.add(2);
    s.add(5);
    auto it = s.begin();
    auto old = it++;
    expect(*old).to_equal(2u);
    expect(*it).to_equal(5u);
}

TEST_CASE("pre-increment iterator advances to next element") {
    BinarySet s(10);
    s.add(3);
    s.add(8);
    auto it = s.begin();
    ++it;
    expect(*it).to_equal(8u);
}

// ─────────────────────────────────────────────────────────────────────────────
// BSSearcher — construction and add
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("BSSearcher construction and add")

TEST_CASE("searcher stores correct capacity") {
    BSSearcher idx(10);
    expect(idx.capacity()).to_equal(10u);
}

TEST_CASE("add throws if set has wrong capacity") {
    BSSearcher idx(10);
    BinarySet s(20);
    expect_throws(std::invalid_argument, idx.add(1, s));
}

// ─────────────────────────────────────────────────────────────────────────────
// BSSearcher — find_subsets
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("BSSearcher find_subsets")

TEST_CASE("no subsets found for empty index") {
    BSSearcher idx(10);
    BinarySet q(10, true);
    expect(idx.find_subsets(q).empty()).to_be_true();
}

TEST_CASE("empty set is a subset of any non-empty query") {
    BSSearcher idx(10);
    BinarySet empty_s(10);
    idx.add(1, empty_s);
    BinarySet q(10);
    q.add(3);
    q.add(7);
    auto r = idx.find_subsets(q);
    expect(r.size()).to_equal(1u);
    expect(r[0]).to_equal(1u);
}

TEST_CASE("exact match is found") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(2);
    s.add(5);
    idx.add(42, s);
    auto r = idx.find_subsets(s);
    expect(r.size()).to_equal(1u);
    expect(r[0]).to_equal(42u);
}

TEST_CASE("strict subset is found") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(1);
    s.add(3);
    idx.add(99, s);
    BinarySet q(10);
    q.add(1);
    q.add(3);
    q.add(7);
    auto r = idx.find_subsets(q);
    expect(r.size()).to_equal(1u);
    expect(r[0]).to_equal(99u);
}

TEST_CASE("non-subset is not returned") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(1);
    s.add(5);
    idx.add(10, s);
    BinarySet q(10);
    q.add(1);
    q.add(3);  // 5 absent → s is not a subset of q
    auto r = idx.find_subsets(q);
    expect(r.empty()).to_be_true();
}

TEST_CASE("multiple subsets are all returned") {
    BSSearcher idx(10);
    BinarySet s1(10), s2(10), s3(10);
    s1.add(1);
    s2.add(1);
    s2.add(3);
    s3.add(5);  // not a subset of query
    idx.add(1, s1);
    idx.add(2, s2);
    idx.add(3, s3);
    BinarySet q(10);
    q.add(1);
    q.add(3);
    q.add(7);
    auto r = idx.find_subsets(q);
    expect(r.size()).to_equal(2u);
}

TEST_CASE("duplicate registrations produce duplicate results") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(2);
    idx.add(7, s);
    idx.add(7, s);
    BinarySet q(10, true);
    auto r = idx.find_subsets(q);
    expect(r.size()).to_equal(2u);
}

TEST_CASE("find_subsets throws for wrong capacity") {
    BSSearcher idx(10);
    BinarySet q(20);
    expect_throws(std::invalid_argument, idx.find_subsets(q));
}

// ─────────────────────────────────────────────────────────────────────────────
// BSSearcher — remove
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("BSSearcher remove")

TEST_CASE("remove returns true for registered entry") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(1);
    idx.add(5, s);
    expect(idx.remove(5, s)).to_be_true();
}

TEST_CASE("remove returns false for unregistered entry") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(1);
    expect(idx.remove(5, s)).to_be_false();
}

TEST_CASE("removed entry is no longer found") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(2);
    s.add(4);
    idx.add(10, s);
    idx.remove(10, s);
    BinarySet q(10, true);
    expect(idx.find_subsets(q).empty()).to_be_true();
}

TEST_CASE("remove one duplicate leaves the other") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(3);
    idx.add(9, s);
    idx.add(9, s);
    idx.remove(9, s);
    BinarySet q(10, true);
    auto r = idx.find_subsets(q);
    expect(r.size()).to_equal(1u);
}

TEST_CASE("remove throws for wrong capacity") {
    BSSearcher idx(10);
    BinarySet s(20);
    expect_throws(std::invalid_argument, idx.remove(1, s));
}

// ─────────────────────────────────────────────────────────────────────────────
// BSSearcher — find_subsets_into
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("BSSearcher find_subsets_into")

TEST_CASE("find_subsets_into appends to existing vector contents") {
    BSSearcher idx(10);
    BinarySet s(10);
    s.add(1);
    idx.add(42, s);
    BinarySet q(10, true);
    std::vector<unsigned int> out = {99u};
    idx.find_subsets_into(q, out);
    expect(out.size()).to_equal(2u);
    expect(out[0]).to_equal(99u);
}

TEST_CASE("find_subsets_into and find_subsets return same elements") {
    BSSearcher idx(10);
    BinarySet s1(10), s2(10);
    s1.add(1);
    s1.add(2);
    s2.add(2);
    s2.add(3);
    idx.add(1, s1);
    idx.add(2, s2);
    BinarySet q(10, true);
    auto r1 = idx.find_subsets(q);
    std::vector<unsigned int> r2;
    idx.find_subsets_into(q, r2);
    std::sort(r1.begin(), r1.end());
    std::sort(r2.begin(), r2.end());
    expect(r1 == r2).to_be_true();
}

TEST_CASE("find_subsets_into throws for wrong capacity") {
    BSSearcher idx(10);
    BinarySet q(20);
    std::vector<unsigned int> out;
    expect_throws(std::invalid_argument, idx.find_subsets_into(q, out));
}