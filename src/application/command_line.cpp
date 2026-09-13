#include "application/command_line.hpp"

#include <cxxopts.hpp>

namespace project_describer {
namespace {

std::filesystem::path PathFromUtf8(const std::string& value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char byte : value) {
        utf8.push_back(static_cast<char8_t>(byte));
    }
    return std::filesystem::path(utf8);
}

}  // namespace

std::string UsageText() {
    cxxopts::Options parser("project-describer", "Create an AI-ready description of a local project.");
    parser.add_options()
        ("p,path", "Project root directory", cxxopts::value<std::string>())
        ("o,output", "Existing directory for the generated report", cxxopts::value<std::string>())
        ("h,help", "Show this help message");
    return parser.help();
}

ParseResult ParseCommandLine(int argc, const char* const argv[]) {
    cxxopts::Options parser("project-describer", "Create an AI-ready description of a local project.");
    parser.add_options()
        ("p,path", "Project root directory", cxxopts::value<std::string>())
        ("o,output", "Existing directory for the generated report", cxxopts::value<std::string>())
        ("h,help", "Show this help message");

    try {
        const auto parsed = parser.parse(argc, argv);
        if (parsed.count("help")) {
            return {ParseStatus::kHelpRequested, std::nullopt, {}};
        }
        if (!parsed.count("path")) {
            return {ParseStatus::kError, std::nullopt, "-p / --path is required."};
        }

        CommandLineOptions options{PathFromUtf8(parsed["path"].as<std::string>()), std::nullopt};
        if (parsed.count("output")) {
            options.output_directory = PathFromUtf8(parsed["output"].as<std::string>());
        }
        return {ParseStatus::kOk, std::move(options), {}};
    } catch (const cxxopts::exceptions::exception& error) {
        return {ParseStatus::kError, std::nullopt, error.what()};
    }
}

}  // namespace project_describer
