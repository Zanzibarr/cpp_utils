#include "../interval/interval.hxx"
#include "../testing/test_main.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("construction")

TEST_CASE("valid Interval stores min and max correctly") {
    Interval<int> i(2, 8);
    expect(i.min()).to_equal(2);
    expect(i.max()).to_equal(8);
}

TEST_CASE("single-point Interval is valid and not empty") {
    Interval<int> i(5, 5);
    expect(i.is_empty()).to_be_false();
    expect(i.min()).to_equal(5);
    expect(i.max()).to_equal(5);
}

TEST_CASE("constructor throws when min > max") { expect_throws(std::invalid_argument, Interval<int>(10, 2)); }

TEST_CASE("constructor accepts negative bounds") {
    Interval<int> i(-10, -2);
    expect(i.min()).to_equal(-10);
    expect(i.max()).to_equal(-2);
}

TEST_CASE("constructor accepts zero-crossing range") {
    Interval<int> i(-5, 5);
    expect(i.min()).to_equal(-5);
    expect(i.max()).to_equal(5);
}

TEST_CASE("make_empty produces an empty Interval") {
    auto i = Interval<int>::make_empty();
    expect(i.is_empty()).to_be_true();
}

TEST_CASE("make_universe is not empty") {
    auto i = Interval<int>::make_universe();
    expect(i.is_empty()).to_be_false();
}

TEST_CASE("make_universe contains extreme values") {
    auto i = Interval<int>::make_universe();
    expect(i.contains(0)).to_be_true();
    expect(i.contains(std::numeric_limits<int>::max())).to_be_true();
    expect(i.contains(std::numeric_limits<int>::lowest())).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("accessors")

TEST_CASE("length() returns max minus min") {
    expect(Interval<int>(2, 8).length()).to_equal(6);
    expect(Interval<int>(-3, 3).length()).to_equal(6);
}

TEST_CASE("length() of single-point Interval is zero") { expect(Interval<int>(5, 5).length()).to_equal(0); }

TEST_CASE("length() works for float") { expect(Interval<float>(1.f, 3.f).length()).to_approx_equal(2.f); }

TEST_CASE("center() returns midpoint for even range") { expect(Interval<int>(0, 10).center()).to_equal(5); }

TEST_CASE("center() returns midpoint for odd range") {
    // integer division: (1 + 9) / 2 = 5 via min + length/2 = 1 + 4 = 5
    expect(Interval<int>(1, 9).center()).to_equal(5);
}

TEST_CASE("center() returns midpoint for float") { expect(Interval<float>(0.f, 1.f).center()).to_approx_equal(0.5f); }

TEST_CASE("center() does not overflow for large integers") {
    int hi = std::numeric_limits<int>::max();
    int lo = hi - 100;
    Interval<int> i(lo, hi);
    expect(i.center()).to_equal(lo + 50);
}

TEST_CASE("center() does not overflow for small (negative) integers") {
    int lo = std::numeric_limits<int>::lowest();
    int hi = lo + 100;
    Interval<int> i(lo, hi);
    expect(i.center()).to_equal(lo + 50);
}

// ─────────────────────────────────────────────────────────────────────────────
// is_empty
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("is_empty")

TEST_CASE("normal Interval is not empty") { expect(Interval<int>(0, 1).is_empty()).to_be_false(); }

TEST_CASE("single-point Interval is not empty") { expect(Interval<int>(0, 0).is_empty()).to_be_false(); }

TEST_CASE("make_empty() is empty") { expect(Interval<int>::make_empty().is_empty()).to_be_true(); }

TEST_CASE("make_universe() is not empty") { expect(Interval<int>::make_universe().is_empty()).to_be_false(); }

// ─────────────────────────────────────────────────────────────────────────────
// contains(value)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("contains(value)")

TEST_CASE("contains value strictly inside range") { expect(Interval<int>(0, 10).contains(5)).to_be_true(); }

TEST_CASE("contains value at min boundary") { expect(Interval<int>(0, 10).contains(0)).to_be_true(); }

TEST_CASE("contains value at max boundary") { expect(Interval<int>(0, 10).contains(10)).to_be_true(); }

TEST_CASE("does not contain value below min") { expect(Interval<int>(0, 10).contains(-1)).to_be_false(); }

TEST_CASE("does not contain value above max") { expect(Interval<int>(0, 10).contains(11)).to_be_false(); }

TEST_CASE("single-point Interval contains its own value") { expect(Interval<int>(7, 7).contains(7)).to_be_true(); }

TEST_CASE("single-point Interval does not contain adjacent values") {
    expect(Interval<int>(7, 7).contains(6)).to_be_false();
    expect(Interval<int>(7, 7).contains(8)).to_be_false();
}

// ─────────────────────────────────────────────────────────────────────────────
// contains(Interval)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("contains(Interval)")

TEST_CASE("Interval contains a smaller Interval inside it") { expect(Interval<int>(0, 10).contains(Interval<int>(2, 8))).to_be_true(); }

TEST_CASE("Interval contains an identical Interval") { expect(Interval<int>(0, 10).contains(Interval<int>(0, 10))).to_be_true(); }

TEST_CASE("Interval contains a single-point Interval on boundary") {
    expect(Interval<int>(0, 10).contains(Interval<int>(0, 0))).to_be_true();
    expect(Interval<int>(0, 10).contains(Interval<int>(10, 10))).to_be_true();
}

TEST_CASE("Interval does not contain an overlapping but larger Interval") {
    expect(Interval<int>(0, 10).contains(Interval<int>(-1, 5))).to_be_false();
    expect(Interval<int>(0, 10).contains(Interval<int>(5, 15))).to_be_false();
}

TEST_CASE("Interval does not contain a fully outside Interval") { expect(Interval<int>(0, 5).contains(Interval<int>(6, 10))).to_be_false(); }

// ─────────────────────────────────────────────────────────────────────────────
// overlaps
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("overlaps")

TEST_CASE("overlapping intervals return true") { expect(Interval<int>(0, 5).overlaps(Interval<int>(3, 8))).to_be_true(); }

TEST_CASE("touching at a single boundary point overlaps") { expect(Interval<int>(0, 5).overlaps(Interval<int>(5, 10))).to_be_true(); }

TEST_CASE("identical intervals overlap") { expect(Interval<int>(0, 5).overlaps(Interval<int>(0, 5))).to_be_true(); }

TEST_CASE("one containing the other overlaps") { expect(Interval<int>(0, 10).overlaps(Interval<int>(3, 7))).to_be_true(); }

TEST_CASE("non-touching intervals do not overlap") { expect(Interval<int>(0, 5).overlaps(Interval<int>(6, 10))).to_be_false(); }

TEST_CASE("empty Interval does not overlap with anything") {
    auto empty = Interval<int>::make_empty();
    expect(empty.overlaps(Interval<int>(0, 10))).to_be_false();
    expect(Interval<int>(0, 10).overlaps(empty)).to_be_false();
}

TEST_CASE("two empty intervals do not overlap") { expect(Interval<int>::make_empty().overlaps(Interval<int>::make_empty())).to_be_false(); }

// ─────────────────────────────────────────────────────────────────────────────
// clamp
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("clamp")

TEST_CASE("clamp returns value unchanged when inside range") { expect(Interval<int>(0, 10).clamp(5)).to_equal(5); }

TEST_CASE("clamp returns min when value is below range") { expect(Interval<int>(0, 10).clamp(-5)).to_equal(0); }

TEST_CASE("clamp returns max when value is above range") { expect(Interval<int>(0, 10).clamp(15)).to_equal(10); }

TEST_CASE("clamp returns min when value equals min boundary") { expect(Interval<int>(0, 10).clamp(0)).to_equal(0); }

TEST_CASE("clamp returns max when value equals max boundary") { expect(Interval<int>(0, 10).clamp(10)).to_equal(10); }

TEST_CASE("clamp on empty Interval throws") { expect_throws(std::logic_error, Interval<int>::make_empty().clamp(5)); }

// ─────────────────────────────────────────────────────────────────────────────
// merge
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("merge")

TEST_CASE("merge of overlapping intervals spans both") {
    auto m = Interval<int>(0, 5).merge(Interval<int>(3, 10));
    expect(m.min()).to_equal(0);
    expect(m.max()).to_equal(10);
}

TEST_CASE("merge of non-overlapping intervals spans the gap") {
    auto m = Interval<int>(0, 3).merge(Interval<int>(7, 10));
    expect(m.min()).to_equal(0);
    expect(m.max()).to_equal(10);
}

TEST_CASE("merge of identical intervals returns same Interval") {
    auto m = Interval<int>(2, 8).merge(Interval<int>(2, 8));
    expect(m).to_equal(Interval<int>(2, 8));
}

TEST_CASE("merge with empty Interval returns the non-empty one") {
    Interval<int> a(2, 8);
    expect(a.merge(Interval<int>::make_empty())).to_equal(a);
    expect(Interval<int>::make_empty().merge(a)).to_equal(a);
}

TEST_CASE("merge is commutative") {
    auto ab = Interval<int>(0, 5).merge(Interval<int>(3, 10));
    auto ba = Interval<int>(3, 10).merge(Interval<int>(0, 5));
    expect(ab).to_equal(ba);
}

// ─────────────────────────────────────────────────────────────────────────────
// intersect
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("intersect")

TEST_CASE("intersect of overlapping intervals returns overlap region") {
    auto result = Interval<int>(0, 10).intersect(Interval<int>(5, 15));
    expect(result.has_value()).to_be_true();
    expect(result->min()).to_equal(5);
    expect(result->max()).to_equal(10);
}

TEST_CASE("intersect of identical intervals returns the same Interval") {
    auto result = Interval<int>(2, 8).intersect(Interval<int>(2, 8));
    expect(result.has_value()).to_be_true();
    expect(*result).to_equal(Interval<int>(2, 8));
}

TEST_CASE("intersect touching at single boundary point returns that point") {
    auto result = Interval<int>(0, 5).intersect(Interval<int>(5, 10));
    expect(result.has_value()).to_be_true();
    expect(result->min()).to_equal(5);
    expect(result->max()).to_equal(5);
}

TEST_CASE("intersect of non-overlapping intervals returns nullopt") {
    expect(Interval<int>(0, 5).intersect(Interval<int>(6, 10)).has_value()).to_be_false();
}

TEST_CASE("intersect is commutative") {
    auto ab = Interval<int>(0, 8).intersect(Interval<int>(4, 12));
    auto ba = Interval<int>(4, 12).intersect(Interval<int>(0, 8));
    expect(ab.has_value()).to_be_true();
    expect(ba.has_value()).to_be_true();
    expect(*ab).to_equal(*ba);
}

// ─────────────────────────────────────────────────────────────────────────────
// expand
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("expand")

TEST_CASE("expand grows both sides by the given amount") {
    auto e = Interval<int>(2, 8).expand(2);
    expect(e.min()).to_equal(0);
    expect(e.max()).to_equal(10);
}

TEST_CASE("expand by zero returns equal Interval") {
    auto e = Interval<int>(2, 8).expand(0);
    expect(e).to_equal(Interval<int>(2, 8));
}

TEST_CASE("expand increases length by twice the amount") {
    Interval<int> i(0, 10);
    expect(i.expand(3).length()).to_equal(i.length() + 6);
}

TEST_CASE("expand with negative amount throws") { expect_throws(std::invalid_argument, Interval<int>(2, 8).expand(-1)); }

// ─────────────────────────────────────────────────────────────────────────────
// translate
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("translate")

TEST_CASE("translate shifts both bounds by positive offset") {
    auto t = Interval<int>(2, 8).translate(3);
    expect(t.min()).to_equal(5);
    expect(t.max()).to_equal(11);
}

TEST_CASE("translate shifts both bounds by negative offset") {
    auto t = Interval<int>(2, 8).translate(-2);
    expect(t.min()).to_equal(0);
    expect(t.max()).to_equal(6);
}

TEST_CASE("translate by zero returns equal Interval") {
    auto t = Interval<int>(2, 8).translate(0);
    expect(t).to_equal(Interval<int>(2, 8));
}

TEST_CASE("translate preserves length") {
    Interval<int> i(2, 8);
    expect(i.translate(100).length()).to_equal(i.length());
    expect(i.translate(-100).length()).to_equal(i.length());
}

// ─────────────────────────────────────────────────────────────────────────────
// normalize / denormalize (float only)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("normalize and denormalize")

TEST_CASE("normalize maps min to 0") { expect(Interval<float>(2.f, 8.f).normalize(2.f)).to_approx_equal(0.f); }

TEST_CASE("normalize maps max to 1") { expect(Interval<float>(2.f, 8.f).normalize(8.f)).to_approx_equal(1.f); }

TEST_CASE("normalize maps midpoint to 0.5") { expect(Interval<float>(0.f, 10.f).normalize(5.f)).to_approx_equal(0.5f); }

TEST_CASE("normalize value outside range produces out-of-[0,1] result") {
    // normalize does not clamp — it extrapolates
    expect(Interval<float>(0.f, 10.f).normalize(15.f)).to_approx_equal(1.5f);
    expect(Interval<float>(0.f, 10.f).normalize(-5.f)).to_approx_equal(-0.5f);
}

TEST_CASE("denormalize maps 0 to min") { expect(Interval<float>(2.f, 8.f).denormalize(0.f)).to_approx_equal(2.f); }

TEST_CASE("denormalize maps 1 to max") { expect(Interval<float>(2.f, 8.f).denormalize(1.f)).to_approx_equal(8.f); }

TEST_CASE("denormalize maps 0.5 to midpoint") { expect(Interval<float>(0.f, 10.f).denormalize(0.5f)).to_approx_equal(5.f); }

TEST_CASE("normalize and denormalize are exact inverses") {
    Interval<float> i(2.f, 8.f);
    float values[] = {2.f, 3.5f, 5.f, 6.25f, 8.f};
    for (float v : values) expect(i.denormalize(i.normalize(v))).to_approx_equal(v, 1e-5f);
}

TEST_CASE("normalize on empty Interval throws") { expect_throws(std::logic_error, Interval<float>::make_empty().normalize(1.f)); }

TEST_CASE("denormalize on empty Interval throws") { expect_throws(std::logic_error, Interval<float>::make_empty().denormalize(0.5f)); }

// ─────────────────────────────────────────────────────────────────────────────
// operator== / operator!=
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("equality operators")

TEST_CASE("equal intervals compare equal") { expect(Interval<int>(0, 10) == Interval<int>(0, 10)).to_be_true(); }

TEST_CASE("intervals with different min compare not equal") { expect(Interval<int>(0, 10) == Interval<int>(1, 10)).to_be_false(); }

TEST_CASE("intervals with different max compare not equal") { expect(Interval<int>(0, 10) == Interval<int>(0, 9)).to_be_false(); }

TEST_CASE("operator!= returns true for different intervals") { expect(Interval<int>(0, 10) != Interval<int>(1, 10)).to_be_true(); }

TEST_CASE("operator!= returns false for identical intervals") { expect(Interval<int>(0, 10) != Interval<int>(0, 10)).to_be_false(); }