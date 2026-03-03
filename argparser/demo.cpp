#include <cstdlib>
#include <iostream>

#include "argparser.hxx"

void check_params(cli::ArgParser& parser) {
    auto name = parser.get<"name", std::string>();
    if (name == "Riccardo") {
        std::cerr << "This user cannot use this software.\n";
        exit(EXIT_FAILURE);
    } else if (name == "Matteo") {
        std::cout << "Recognized admin, setting mode to turbo.\n";
        parser.set<"mode", std::string>("turbo");
    }
}

auto main(int argc, char* argv[]) -> int {
    cli::ArgParser parser("myapp", "A demo CLI application");

    parser.add<"count", int>().shorthand('n').description("Number of iterations").default_val(10).min(1).max(100);
    parser.add<"verbose", bool>().shorthand('v').description("Enable verbose output").default_val(false);
    parser.add<"mode", std::string>()
        .shorthand('m')
        .description("Operating mode")
        .default_val(std::string("fast"))
        .allow<std::string>({"fast", "slow", "turbo"});
    parser.add<"delimiter", std::string>().shorthand('d').description("Single character delimiter").default_val(",");
    parser.add<"output", std::string>().shorthand('o').description("Output file path").default_val("out.txt");
    parser.add<"name", std::string>().shorthand('N').description("Your name").require();
    parser.add<"weight", double>().shorthand('w').description("A double variable").default_val(.5).min(-10).max(10);

    if (!argparser_parse(parser, argc, argv, check_params)) {
        return EXIT_FAILURE;
    }

    // ── Access via compile-time get<Name, T>() ─────────────────────────────

    int count = parser.get<"count", int>();
    bool verbose = parser.get<"verbose", bool>();
    std::string mode = parser.get<"mode", std::string>();
    std::string delimiter = parser.get<"delimiter", std::string>();
    auto output = parser.get<"output", std::string>();
    std::string name = parser.get<"name", std::string>();
    double weight = parser.get<"weight", double>();

    std::cout << "count     = " << count << "\n";
    std::cout << "verbose   = " << std::boolalpha << verbose << "\n";
    std::cout << "mode      = " << mode << "\n";
    std::cout << "delimiter = " << delimiter << "\n";
    std::cout << "output    = " << output << "\n";
    std::cout << "name      = " << name << "\n";
    std::cout << "weight    = " << weight << "\n";

    // ── Direct access to the ParameterRegistry after parse() ───────────────

    std::cout << "\n── via parameters() ──\n";
    parser.parameters().print_report();
}
