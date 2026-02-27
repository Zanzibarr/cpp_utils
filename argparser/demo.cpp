#include <iostream>

#include "argparser.hxx"

auto main(int argc, char* argv[]) -> int {
    cli::ArgParser parser("myapp", "A demo CLI application");

    parser.add("count").shorthand('n').description("Number of iterations").default_val(10).min(1).max(100);
    parser.add("verbose").shorthand('v').description("Enable verbose output").default_val(false);
    parser.add("mode").shorthand('m').description("Operating mode").default_val(std::string("fast")).allow<std::string>({"fast", "slow", "turbo"});
    parser.add("delimiter").shorthand('d').description("Single character delimiter").default_val(',').min('!').max('~');
    parser.add("output").shorthand('o').description("Output file path").default_val(std::filesystem::path("out.txt"));
    parser.add("name").shorthand('N').description("Your name").require();

    ARGPARSER_PARSE(parser, argc, argv);

    int count = parser.get<int>("count");
    bool verbose = parser.get<bool>("verbose");
    std::string mode = parser.get<std::string>("mode");
    char delimiter = parser.get<char>("delimiter");
    std::filesystem::path output = parser.get<std::filesystem::path>("output");
    std::string name = parser.get<std::string>("name");

    std::cout << "count    = " << count << "\n";
    std::cout << "verbose  = " << std::boolalpha << verbose << "\n";
    std::cout << "mode     = " << mode << "\n";
    std::cout << "delimiter= " << delimiter << "\n";
    std::cout << "output   = " << output << "\n";
    std::cout << "name     = " << name << "\n";
}