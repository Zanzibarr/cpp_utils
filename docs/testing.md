# testing

**Files:** [`testing/test_framework.hxx`](../testing/test_framework.hxx), [`testing/test_main.hpp`](../testing/test_main.hpp)
**Dependencies:** [ansi_colors](ansi_colors.md)

Lightweight unit-test framework with fluent expectations, exception testing, and colored output. No external dependencies.

## Registration Macros

```cpp
TEST_SUITE("Suite name")   // sets the current suite label

TEST_CASE("case name") {
    // assertions here
}
```

Cases are registered at static init time and run in registration order.

## Assertions — `expect()`

`expect(value)` returns an `expectation<T>` with a fluent assertion API:

```cpp
expect(result).to_equal(42);
expect(result).not_to_equal(0);

expect(flag).to_be_true();
expect(flag).to_be_false();

expect(x).to_be_greater_than(0);
expect(x).to_be_less_than(100);
expect(x).to_be_greater_or_equal(1);
expect(x).to_be_less_or_equal(99);

expect(pi).to_approx_equal(3.14159, 1e-5);   // floating-point with epsilon

expect(msg).to_contain("error");              // string contains check
```

Failed assertions throw `assertion_error` (which includes the file and line number) and are caught by the framework. The test is marked failed, but other tests continue.

## Exception Testing

```cpp
// Verify that a specific exception type is thrown
expect_throws<std::out_of_range>([] {
    std::vector<int> v;
    v.at(0);
});

// Verify that no exception is thrown
expect_no_throw([] {
    std::vector<int> v;
    v.push_back(1);
});
```

## Running Tests

Use `test_main.hpp` to provide a `main()` that runs all registered tests:

```cpp
#include "../testing/test_main.hpp"
```

Or run manually:

```cpp
#include "test_framework.hxx"
int main() {
    return testing::test_registry::instance().run_all();
    // returns 0 on all pass, 1 on any failure
}
```

## Output

```
Suite: MyContainer
  ✓ default constructed size is zero
  ✓ insert increments size
  ✗ remove non-existent returns false
      FAILED: expected true, got false  [test.cpp:42]

1 failed, 2 passed
```

## Example

```cpp
#include "../testing/test_main.hpp"
#include "my_container.hxx"

TEST_SUITE("MyContainer")

TEST_CASE("default constructed is empty") {
    MyContainer c;
    expect(c.size()).to_equal(0u);
    expect(c.empty()).to_be_true();
}

TEST_CASE("insert increases size") {
    MyContainer c;
    c.insert(1);
    c.insert(2);
    expect(c.size()).to_equal(2u);
    expect(c.contains(1)).to_be_true();
    expect(c.contains(99)).to_be_false();
}

TEST_CASE("out_of_range on bad access") {
    MyContainer c;
    expect_throws<std::out_of_range>([&] { c.at(0); });
}
```

Compile and run:

```bash
g++ -std=c++20 -O2 test.cpp -o test && ./test
```
