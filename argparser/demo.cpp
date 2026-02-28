#include <cstdlib>
#include <iostream>

#include "argparser.hxx"

auto main(int argc, char* argv[]) -> int {
    cli::ArgParser parser("myapp", "A demo CLI application");

    parser.add<int>("count").shorthand('n').description("Number of iterations").default_val(10).min(1).max(100);
    parser.add<bool>("verbose").shorthand('v').description("Enable verbose output").default_val(false);
    parser.add<std::string>("mode")
        .shorthand('m')
        .description("Operating mode")
        .default_val(std::string("fast"))
        .allow<std::string>({"fast", "slow", "turbo"});
    parser.add<char>("delimiter").shorthand('d').description("Single character delimiter").default_val(',').min('!').max('~');
    parser.add<std::filesystem::path>("output").shorthand('o').description("Output file path").default_val(std::filesystem::path("out.txt"));
    parser.add<std::string>("name").shorthand('N').description("Your name").require();
    parser.add<double>("weight").shorthand('w').description("A double variable").default_val(.5).min(-10).max(10);

    if (!argparser_parse(parser, argc, argv)) {
        exit(EXIT_FAILURE);
    }

    int count = parser.get<int>("count");
    bool verbose = parser.get<bool>("verbose");
    auto mode = parser.get<std::string>("mode");
    char delimiter = parser.get<char>("delimiter");
    auto output = parser.get<std::filesystem::path>("output");
    auto name = parser.get<std::string>("name");
    auto weight = parser.get<double>("weight");

    std::cout << "count    = " << count << "\n";
    std::cout << "verbose  = " << std::boolalpha << verbose << "\n";
    std::cout << "mode     = " << mode << "\n";
    std::cout << "delimiter= " << delimiter << "\n";
    std::cout << "output   = " << output << "\n";
    std::cout << "name     = " << name << "\n";
    std::cout << "weight   = " << weight << "\n";
}