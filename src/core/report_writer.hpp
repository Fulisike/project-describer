#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "core/scanner.hpp"

namespace project_describer {

class ReportWriter {
public:
    static std::string Render(const std::filesystem::path& project_root,
                              const ScanResult& scan_result,
                              std::size_t max_lines_per_file = 0);  // 0 writes the full content.
    static std::filesystem::path ChooseOutputPath(const std::filesystem::path& output_directory);
    static bool WriteUtf8(const std::filesystem::path& output_path, const std::string& content,
                          std::string& error_message);
};

}  // namespace project_describer
