# argparser

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`argparser/argparser.hxx`](../argparser/argparser.hxx)
**Dependencies:** [parameters](parameters.md) → [ct_string](ct_string.md)
**Benchmarks:** [argparser/BENCHMARKS.md](../argparser/BENCHMARKS.md)

CLI argument parser with a fluent builder API, auto-generated help, optional TOML config file overlay, type validation, and range/choice constraints. Parsed values are stored in an underlying `ParameterRegistry` for O(1) access.

## Registering Arguments

```cpp
ArgParser parser;

parser.add<"name", Type>()
    .shorthand('n')           // -n short form
    .description("help text")
    .default_val(value)       // used if not provided
    .require()                // error if not provided and no default
    .min(0)                   // int range check
    .max(100)
    .allow({"adam", "sgd"})   // restrict to known values
    .positional(true);        // consume positional arg (no flag name)
```

**Supported types:** `int`, `double`, `bool`, `std::string`

## Parsing

```cpp
// Parse and exit on error
parser.parse(argc, argv);

// Parse and run a callback after validation
parser.parse(argc, argv, [&]() {
    // post-parse validation
});

// Helper that returns bool instead of throwing
if (!argparser_parse(parser, argc, argv)) return 1;
```

## Reading Values

```cpp
int   bs  = parser.get<"batch_size",   int>();
double lr  = parser.get<"learning_rate", double>();
bool  shuf = parser.get<"shuffle",       bool>();
std::string opt = parser.get<"optimizer", std::string>();

if (parser.has<"dropout">()) { ... }
```

Values can also be read through the underlying registry:

```cpp
ParameterRegistry& params = parser.parameters();
```

## TOML Config Overlay

Pass `--config path.toml` on the command line to load a config file. The built-in TOML parser handles flat `key = value` pairs and optional `[table]` headers.

**Precedence:** defaults < TOML values < explicit CLI flags

```toml
# config.toml
batch_size = 64
optimizer = "sgd"
```

## Auto-generated Help

`--help` / `-h` prints a formatted table of all arguments with their types, defaults, and descriptions, then exits.

## Error Handling

`parser.parse()` throws `ArgParser::ParseError` on unknown flags, type errors, or constraint violations. Use `argparser_parse()` to convert this to a `bool` return with a printed error message.

## Example

```cpp
#include "argparser.hxx"

int main(int argc, char* argv[]) {
    ArgParser parser;

    parser.add<"epochs", int>()
        .shorthand('e')
        .description("number of training epochs")
        .default_val(10)
        .min(1).max(1000);

    parser.add<"lr", double>()
        .description("learning rate")
        .default_val(0.001);

    parser.add<"optimizer", std::string>()
        .shorthand('o')
        .description("optimizer to use")
        .default_val("adam")
        .allow({"adam", "sgd", "rmsprop"});

    parser.add<"verbose", bool>()
        .shorthand('v')
        .description("enable verbose output")
        .default_val(false);

    if (!argparser_parse(parser, argc, argv)) return 1;

    int    epochs = parser.get<"epochs",    int>();
    double lr     = parser.get<"lr",        double>();
    auto   opt    = parser.get<"optimizer", std::string>();
    bool   verb   = parser.get<"verbose",   bool>();
}
```

```
$ ./train --epochs 50 -o sgd --lr 0.01
$ ./train --config config.toml -v
$ ./train --help
```
