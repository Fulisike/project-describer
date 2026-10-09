#include "core/scanner.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

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

bool IsGeneratedReport(const std::filesystem::path& path,
                       const std::filesystem::path& output_directory,
                       const std::filesystem::path& canonical_output) {
    const auto name = path.filename().string();
    constexpr std::string_view prefix = "describer";
    constexpr std::string_view suffix = ".txt";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)) {
        return false;
    }
    const auto number = std::string_view(name).substr(
        prefix.size(), name.size() - prefix.size() - suffix.size());
    if (!std::all_of(number.begin(), number.end(),
                     [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return false;
    }
    // Most files never require a canonical-path query. The output is resolved once.
    std::error_code error;
    const auto lexical_parent = std::filesystem::absolute(path.parent_path(), error).lexically_normal();
#ifdef _WIN32
    if (!error && _wcsicmp(lexical_parent.c_str(), output_directory.c_str()) == 0) {
#else
    if (!error && lexical_parent == output_directory) {
#endif
        return true;
    }
    error.clear();
    const auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
#ifdef _WIN32
    return !error && _wcsicmp(parent.c_str(), canonical_output.c_str()) == 0;
#else
    return !error && parent == canonical_output;
#endif
}

bool IsLinkedDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return std::filesystem::is_symlink(path, error);
#endif
}

struct ReadJob {
    std::filesystem::path source;
    std::filesystem::path relative;
};

// Only the enumerator touches directory state. Readers own independent result buffers.
class TextReaders {
public:
    explicit TextReaders(const ScannerConfig& config)
        : max_file_size_(config.max_file_size),
          results_(std::clamp<std::size_t>(config.worker_count == 0
              ? std::clamp(std::thread::hardware_concurrency(), 2u, 4u) : config.worker_count,
              1, 8)) {
        if (results_.size() == 1) {
            return;
        }
        try {
            for (std::size_t index = 0; index < results_.size(); ++index) {
                threads_.emplace_back([this, index] { Run(results_[index]); });
            }
        } catch (...) {
            Close();
            throw;
        }
    }

    ~TextReaders() { Close(); }

    void Add(ReadJob job) {
        if (threads_.empty()) {
            Read(std::move(job), results_.front());
            return;
        }
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return failure_ || jobs_.size() < 64; });
        if (failure_) {
            std::rethrow_exception(failure_);
        }
        jobs_.push_back(std::move(job));
        lock.unlock();
        ready_.notify_all();
    }

    void Finish(ScanResult& result) {
        Close();
        if (failure_) {
            std::rethrow_exception(failure_);
        }
        for (auto& buffer : results_) {
            result.text_files.insert(result.text_files.end(),
                std::make_move_iterator(buffer.text_files.begin()),
                std::make_move_iterator(buffer.text_files.end()));
            result.warnings.insert(result.warnings.end(),
                std::make_move_iterator(buffer.warnings.begin()),
                std::make_move_iterator(buffer.warnings.end()));
        }
    }

private:
    void Read(ReadJob job, ScanResult& buffer) {
        auto read = FileReader::ReadTextFile(job.source, max_file_size_);
        if (read.success) {
            buffer.text_files.emplace_back(std::move(job.relative),
                read.text.source_encoding, std::move(read.text.utf8));
        } else if (read.error_message != "Binary or non-text content") {
            const auto warning = "Skipped " + job.source.filename().string() +
                                 ": " + read.error_message;
            LOG_WARN("{}", warning);
            buffer.warnings.push_back(warning);
        }
    }

    void Run(ScanResult& buffer) {
        try {
            for (;;) {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return failure_ || finished_ || !jobs_.empty(); });
                if (failure_ || jobs_.empty()) {
                    return;
                }
                auto job = std::move(jobs_.front());
                jobs_.pop_front();
                lock.unlock();
                ready_.notify_all();
                Read(std::move(job), buffer);
            }
        } catch (...) {
            {
                std::lock_guard lock(mutex_);
                failure_ = std::current_exception();
            }
            ready_.notify_all();
        }
    }

    void Close() {
        {
            std::lock_guard lock(mutex_);
            finished_ = true;
        }
        ready_.notify_all();
        threads_.clear();  // jthread joins before buffers or synchronization are destroyed.
    }

    std::uintmax_t max_file_size_;
    std::vector<ScanResult> results_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<ReadJob> jobs_;
    bool finished_ = false;
    std::exception_ptr failure_;
    std::vector<std::jthread> threads_;
};

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

        const auto output = std::filesystem::absolute(output_directory).lexically_normal();
        const auto canonical_output = std::filesystem::weakly_canonical(output, ec);
        ec.clear();
        TextReaders readers(config);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const auto& entry = *it;
            const auto& path = entry.path();

            const auto relative = path.lexically_relative(project_root);
            const bool is_directory = entry.is_directory(ec);
            if (ec) {
                result.warnings.push_back("Cannot inspect entry: " + path.string());
                ec.clear();
                it.disable_recursion_pending();
                continue;
            }
            if (!is_directory && IsGeneratedReport(path, output, canonical_output)) {
                ++result.scanned_files;
                continue;
            }

            // Build ProjectEntry regardless of file/dir
            {
                ProjectEntry pe;
                pe.relative_path = relative;
                pe.is_directory = is_directory;
                result.entries.push_back(std::move(pe));
            }

            if (is_directory) {
                if (ShouldSkipDirectory(path) || IsLinkedDirectory(path)) {
                    it.disable_recursion_pending();
                    LOG_DEBUG("Skipping directory: {}", path.string());
                }
                continue;
            }

            ++result.scanned_files;
            // Never read special files or follow file links outside the selected tree.
            if (entry.is_symlink(ec) || !entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            // Skip dotfiles and known binary extensions
            const auto filename = path.filename().string();
            if (!filename.empty() && filename.front() == '.') {
                continue;
            }
            if (HasBinaryExtension(path)) {
                continue;
            }

            // Size guard
            std::error_code size_ec;
            const auto size = entry.file_size(size_ec);
            if (size_ec) {
                LOG_WARN("Cannot read file size: {}", path.string());
                continue;
            }
            if (size > config.max_file_size) {
                LOG_WARN("Skipping large file ({} > {} bytes): {}",
                         size, config.max_file_size, path.string());
                result.warnings.push_back("Skipping large file: " +
                                          path.filename().string());
                continue;
            }

            readers.Add({path, relative});
        }
        if (ec) {
            result.warnings.push_back("Traversal aborted: " + ec.message());
        }
        readers.Finish(result);

        std::sort(result.entries.begin(), result.entries.end(),
                  [](const ProjectEntry& left, const ProjectEntry& right) {
                      return left.relative_path.generic_u8string() <
                             right.relative_path.generic_u8string();
                  });
        std::sort(result.text_files.begin(), result.text_files.end(),
                  [](const FileItem& left, const FileItem& right) {
                      return left.relative_path.generic_u8string() <
                             right.relative_path.generic_u8string();
                  });
        std::sort(result.warnings.begin(), result.warnings.end());

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
