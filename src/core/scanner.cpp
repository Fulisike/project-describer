#include "core/scanner.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

#include "utils/file_reader.hpp"
#include "utils/logging.hpp"

namespace project_describer {
namespace {

bool ShouldSkipDirectory(const std::filesystem::path& dir) {
    static const std::set<std::string> kSkippedDirs = {
        ".git", ".svn", ".hg", "node_modules", ".venv", "venv", "__pycache__",
        ".idea", ".vscode", ".gradle", "build", "out", "dist",
        "cmake-build-debug", "cmake-build-release", ".cache", ".pytest_cache",
        ".clangd"};
    return kSkippedDirs.count(dir.filename().string()) > 0;
}

bool HasBinaryExtension(const std::filesystem::path& path) {
    static const std::set<std::string> kBinaryExts = {
        ".git", ".vs", ".obj", ".o", ".a", ".lib", ".dll", ".exe",
        ".pdb", ".idb", ".pch", ".ipch", ".log", ".tmp", ".temp",
        ".dmp", ".crash", ".gz", ".zip", ".rar", ".7z", ".tar",
        ".bmp", ".jpg", ".jpeg", ".png", ".gif", ".ico", ".mp4",
        ".mp3", ".wav", ".avi", ".mkv", ".mov",
        ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
        ".pdf", ".ttf", ".otf", ".woff", ".woff2", ".eot",
        ".class", ".jar", ".war", ".pyc", ".pyo",
        ".pak", ".assets", ".dylib", ".dSYM", ".swp", ".swo"};
    return kBinaryExts.count(path.extension().string()) > 0;
}

}  // namespace

ScanResult Scanner::Scan(const std::filesystem::path& project_root,
                         const std::filesystem::path& output_directory,
                         const ScannerConfig& config) {
    LOG_INFO("Scanning project: {}", project_root.string());

    ScanResult result;
    std::error_code ec;

    try {
        // Validate project root
        if (!std::filesystem::is_directory(project_root, ec) || ec) {
            const auto msg = "Project root is not a directory: " + project_root.string();
            LOG_ERROR("{}", msg);
            result.warnings.push_back(msg);
            return result;
        }

        // Recursive traversal
        std::filesystem::recursive_directory_iterator it(
            project_root,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        if (ec) {
            const auto msg = "Cannot start directory traversal: " + ec.message();
            LOG_ERROR("{}", msg);
            result.warnings.push_back(msg);
            return result;
        }

        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                // skip_permission_denied handles most cases, so this is rare.
                LOG_WARN("Aborting traversal at {}: {}",
                         it->path().string(), ec.message());
                result.warnings.push_back("Traversal aborted: " +
                                          it->path().string() + " (" +
                                          ec.message() + ")");
                break;
            }

            const auto& entry = *it;
            const auto& path = entry.path();

            // Build ProjectEntry regardless of file/dir
            {
                ProjectEntry pe;
                pe.relative_path = path.lexically_relative(project_root);
                pe.is_directory = entry.is_directory();
                result.entries.push_back(std::move(pe));
            }

            if (entry.is_directory()) {
                if (ShouldSkipDirectory(path)) {
                    it.disable_recursion_pending();
                    LOG_DEBUG("Skipping directory: {}", path.string());
                }
                continue;
            }

            // Skip dotfiles and known binary extensions
            const auto filename = path.filename().string();
            if (!filename.empty() && filename.front() == '.') {
                ++result.scanned_files;
                continue;
            }
            if (HasBinaryExtension(path)) {
                ++result.scanned_files;
                continue;
            }

            // Size guard
            std::error_code size_ec;
            const auto size = entry.file_size(size_ec);
            if (size_ec) {
                LOG_WARN("Cannot read file size: {}", path.string());
                ++result.scanned_files;
                continue;
            }
            if (size > config.max_file_size) {
                LOG_WARN("Skipping large file ({} > {} bytes): {}",
                         size, config.max_file_size, path.string());
                result.warnings.push_back("Skipping large file: " +
                                          path.filename().string());
                ++result.scanned_files;
                continue;
            }

            // Read and decode text file
            const auto file_result = FileReader::ReadTextFile(path);
            if (!file_result.success) {
                LOG_WARN("Skipping file ({}): {}",
                         file_result.error_message, path.string());
                if (file_result.error_message != "Binary or non-text content") {
                    result.warnings.push_back(
                        "Skipped " + path.filename().string() +
                        ": " + file_result.error_message);
                }
                ++result.scanned_files;
                continue;
            }

            result.text_files.push_back(
                FileItem{path.lexically_relative(project_root),
                         file_result.text.source_encoding,
                         file_result.text.utf8});
            ++result.scanned_files;
        }

        LOG_INFO("Scan complete: {} files scanned, {} text files found",
                 result.scanned_files, result.text_files.size());
        if (!result.warnings.empty()) {
            LOG_WARN("Scan produced {} warnings", result.warnings.size());
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_CRITICAL("Filesystem error during scan: {}", e.what());
        result.warnings.push_back(std::string("Filesystem error: ") + e.what());
    } catch (const std::bad_alloc& e) {
        LOG_CRITICAL("Out of memory during scan: {}", e.what());
        result.warnings.push_back(std::string("Out of memory: ") + e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("Unexpected error during scan: {}", e.what());
        result.warnings.push_back(std::string("Scan error: ") + e.what());
    }

    return result;
}

}  // namespace project_describer
