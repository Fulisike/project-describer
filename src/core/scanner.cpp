#include "core/scanner.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "utils/file_reader.hpp"

namespace project_describer {
namespace {

std::filesystem::path NormalizedAbsolute(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool IsDescriptionOutput(const std::filesystem::path& file,
                         const std::filesystem::path& output_directory) {
    if (NormalizedAbsolute(file.parent_path()) != NormalizedAbsolute(output_directory)) {
        return false;
    }

    const std::string filename = file.filename().string();
    constexpr std::string_view prefix = "describer";
    constexpr std::string_view suffix = ".txt";
    if (filename.size() < prefix.size() + suffix.size() ||
        filename.compare(0, prefix.size(), prefix) != 0 ||
        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }

    const auto number_start = prefix.size();
    const auto number_length = filename.size() - prefix.size() - suffix.size();
    return std::all_of(filename.begin() + static_cast<std::ptrdiff_t>(number_start),
                       filename.begin() + static_cast<std::ptrdiff_t>(number_start + number_length),
                       [](const unsigned char character) { return std::isdigit(character) != 0; });
}

std::string DisplayPath(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

bool ShouldSkipDirectory(const std::string& name) {
    // 常见噪声目录：VCS、依赖、构建产物
    static constexpr std::string_view skip_dirs[] = {
        ".git",    ".svn",     ".hg",         "node_modules", ".venv",
        "venv",    "__pycache__", ".idea",    ".vscode",      ".gradle",
        "build",   "out",      "dist",        ".next",
    };
    for (const auto dir : skip_dirs) {
        if (name == dir) {
            return true;
        }
    }
    return false;
}

}  // namespace

ScanResult Scanner::Scan(const std::filesystem::path& project_root,
                         const std::filesystem::path& output_directory,
                         const ScannerConfig& config) {
    ScanResult result;
    std::error_code error;
    if (!std::filesystem::is_directory(project_root, error) || error) {
        result.warnings.push_back("Project root is not a readable directory: " + DisplayPath(project_root));
        return result;
    }

    std::filesystem::recursive_directory_iterator iterator(
        project_root, std::filesystem::directory_options::skip_permission_denied, error);
    std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.warnings.push_back("Could not start directory traversal: " + error.message());
        return result;
    }

    while (iterator != end) {
        const auto entry = *iterator;
        const auto relative_path = std::filesystem::relative(entry.path(), project_root, error);
        if (error) {
            result.warnings.push_back("Could not create a relative path: " + DisplayPath(entry.path()));
            error.clear();
        } else {
            const bool is_directory = entry.is_directory(error);
            if (!error) {
                if (is_directory) {
                    if (ShouldSkipDirectory(entry.path().filename().string())) {
                        iterator.disable_recursion_pending();
                        continue;
                    }
                    result.entries.push_back({relative_path, true});
                } else if (!IsDescriptionOutput(entry.path(), output_directory)) {
                    result.entries.push_back({relative_path, false});
                }
            }
            error.clear();

            if (!is_directory && entry.is_regular_file(error)) {
                ++result.scanned_files;
                if (!IsDescriptionOutput(entry.path(), output_directory)) {
                    const auto file_size = entry.file_size(error);
                    if (error) {
                        result.warnings.push_back("Could not get file size: " + DisplayPath(entry.path()));
                        error.clear();
                    } else if (file_size > config.max_file_size) {
                        result.warnings.push_back("Skipping large file: " + DisplayPath(entry.path()) +
                                                   " (" + std::to_string(file_size) + " bytes)");
                    } else {
                        const auto read = FileReader::ReadTextFile(entry.path());
                        if (read.success) {
                            result.text_files.emplace_back(relative_path, read.text.source_encoding,
                                                           read.text.utf8);
                        }
                    }
                }
            }
            error.clear();
        }

        iterator.increment(error);
        if (error) {
            result.warnings.push_back("Could not inspect an entry: " + error.message());
            error.clear();
        }
    }

    std::sort(result.entries.begin(), result.entries.end(), [](const ProjectEntry& left,
                                                                const ProjectEntry& right) {
        return left.relative_path.generic_u8string() < right.relative_path.generic_u8string();
    });
    std::sort(result.text_files.begin(), result.text_files.end(), [](const FileItem& left,
                                                                      const FileItem& right) {
        return left.relative_path.generic_u8string() < right.relative_path.generic_u8string();
    });
    return result;
}

}  // namespace project_describer
