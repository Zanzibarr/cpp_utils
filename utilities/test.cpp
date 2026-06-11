#include <string>
#include <string_view>

#include "../testing/test_main.hpp"
#include "../utilities/ansi_colors.hxx"
#include "../utilities/ct_string.hxx"

using namespace utilz;

// ─────────────────────────────────────────────────────────────────────────────
// CTString
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("CTString")

namespace {
template <CTString Name>
struct tag {
    static constexpr std::string_view value = Name.view();
};
}  // namespace

// Compile-time guarantees (consteval API).
static_assert(CTString{"abc"}.view() == "abc");
static_assert(CTString{"abc"} == CTString{"abc"});
static_assert(!(CTString{"abc"} == CTString{"abd"}));
static_assert(!(CTString{"abc"} == CTString{"ab"}));
static_assert(CTString{""}.view().empty());
static_assert(tag<"hello">::value == "hello");

TEST_CASE("view() excludes the null terminator") {
    static constexpr CTString str{"hello"};
    constexpr auto view = str.view();
    expect(view.size()).to_equal(std::size_t{5});
    expect(std::string(view)).to_equal(std::string("hello"));
}

TEST_CASE("equality distinguishes different literals and lengths") {
    constexpr bool same = CTString{"timer"} == CTString{"timer"};
    constexpr bool diff = CTString{"timer"} == CTString{"tamer"};
    constexpr bool len = CTString{"timer"} == CTString{"timers"};
    expect(same).to_be_true();
    expect(diff).to_be_false();
    expect(len).to_be_false();
}

TEST_CASE("usable as non-type template parameter") {
    expect(std::string(tag<"slot_a">::value)).to_equal(std::string("slot_a"));
    expect(std::string(tag<"slot_b">::value)).not_to_equal(std::string(tag<"slot_a">::value));
}

// ─────────────────────────────────────────────────────────────────────────────
// hash_name
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("hash_name")

// FNV-1a reference values are stable by definition.
static_assert(hash_name("") == 14695981039346656037ULL);
static_assert(hash_name("a") != hash_name("b"));
static_assert(hash_name("abc") == hash_name("abc"));

TEST_CASE("is deterministic and collision-free on distinct short names") {
    constexpr auto h1 = hash_name("db_query");
    constexpr auto h2 = hash_name("db_query");
    constexpr auto h3 = hash_name("db_querz");
    expect(h1).to_equal(h2);
    expect(h1).not_to_equal(h3);
}

TEST_CASE("empty string hashes to the FNV offset basis") { expect(hash_name("")).to_equal(std::size_t{14695981039346656037ULL}); }

// ─────────────────────────────────────────────────────────────────────────────
// ansi colors
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("ansi")

TEST_CASE("escape-code constants are well-formed") {
    expect(std::string(ansi::codes::reset)).to_equal(std::string("\033[0m"));
    expect(std::string(ansi::codes::red)).to_contain("\033[31");
    expect(std::string(ansi::codes::bright_cyan)).to_contain("\033[96");
}

TEST_CASE("wrappers always preserve the original text") {
    expect(ansi::green("payload")).to_contain("payload");
    expect(ansi::red("payload")).to_contain("payload");
    expect(ansi::bold("payload")).to_contain("payload");
    expect(ansi::dim("payload")).to_contain("payload");
}

TEST_CASE("wrappers add codes only when a TTY is attached") {
    const bool tty = ansi::enabled();
    const std::string wrapped = ansi::yellow("x");
    if (tty) {
        expect(wrapped).to_contain("\033[33m");
        expect(wrapped).to_contain("\033[0m");
    } else {
        expect(wrapped).to_equal(std::string("x"));
    }
}

TEST_CASE("enabled() is stable across calls (cached)") { expect(ansi::enabled()).to_equal(ansi::enabled()); }
