#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "model/file_item.hpp"

namespace project_describer {

struct ProjectEntry {
    std::filesystem::path relative_path;
    bool is_directory = false;
};

struct ScanResult {
    std::vector<ProjectEntry> entries;
    std::vector<FileItem> text_files;
    std::size_t scanned_files = 0;
    std::vector<std::string> warnings;
};

struct ScannerConfig {
    std::uintmax_t max_file_size = 1 * 1024 * 1024;  // 1 MiB default
    // 0 selects up to 4 readers based on hardware; 1 runs synchronously.
    std::size_t worker_count = 0;
};

class Scanner {
public:
    static ScanResult Scan(const std::filesystem::path& project_root,
                           const std::filesystem::path& output_directory,
                           const ScannerConfig& config = {});
};

}  // namespace project_describer
