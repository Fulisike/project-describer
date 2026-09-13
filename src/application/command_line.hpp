#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace project_describer {

enum class ParseStatus { kOk, kHelpRequested, kError };

struct CommandLineOptions {
    std::filesystem::path project_root;
    std::optional<std::filesystem::path> output_directory;
};

struct ParseResult {
    ParseStatus status = ParseStatus::kError;
    std::optional<CommandLineOptions> options;
    std::string message;
};

ParseResult ParseCommandLine(int argc, const char* const argv[]);
std::string UsageText();

}  // namespace project_describer
