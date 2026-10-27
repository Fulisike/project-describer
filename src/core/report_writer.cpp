#include "core/report_writer.hpp"

#include <chrono>
#include <atomic>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>

#include "model/text_encoding.hpp"
#include "utils/logging.hpp"
#include "utils/path_utils.hpp"

namespace project_describer {
namespace {

std::string DirectoryLine(const ProjectEntry& entry) {
    const auto components = static_cast<std::size_t>(
        std::distance(entry.relative_path.begin(), entry.relative_path.end()));
    std::string line((components > 0 ? components - 1 : 0) * 2, ' ');
    line += PathToUtf8(entry.relative_path.filename());
    if (entry.is_directory) {
        line += '/';
    }
    return line;
}

std::string_view FirstLines(std::string_view content, std::size_t max_lines) {
    if (max_lines == 0) {
        return content;
    }
    std::size_t end = 0;
    for (std::size_t line = 0; line < max_lines; ++line) {
        end = content.find_first_of("\r\n", end);
        if (end == std::string_view::npos) {
            return content;
        }
        const auto separator = content[end++];
        if (separator == '\r' && end < content.size() && content[end] == '\n') {
            ++end;
        }
    }
    return content.substr(0, end);
}

}  // namespace

std::string ReportWriter::Render(const std::filesystem::path& project_root,
                                 const ScanResult& scan_result,
                                 std::size_t max_lines_per_file) {
    LOG_DEBUG("Rendering report for project: {}", PathToUtf8(project_root));
    std::ostringstream report;
    report.exceptions(std::ios::badbit | std::ios::failbit);
    try {
        report << "# Project Description\n\n";
        report << "Project root: " << PathToUtf8(project_root) << "\n\n";
        report << "## Directory Structure\n\n";
        report << ".\n";
        for (const auto& entry : scan_result.entries) {
            report << DirectoryLine(entry) << '\n';
        }
        report << "\n## Text File Contents\n";
        report << "Text files included: " << scan_result.text_files.size() << "\n\n";
        if (max_lines_per_file != 0) {
            report << "Content limit: first " << max_lines_per_file << " lines per file\n\n";
        }

        for (const auto& file : scan_result.text_files) {
            report << "<<< FILE START: " << PathToUtf8(file.relative_path) << " >>>\n";
            report << "File name: " << file.file_name << '\n';
            report << "Detected encoding: " << EncodingName(file.source_encoding) << "\n";
            report << "--- CONTENT ---\n";
            const auto content = FirstLines(file.content, max_lines_per_file);
            report << content;
            if (!content.empty() && content.back() != '\n') {
                report << '\n';
            }
            report << "<<< FILE END: " << PathToUtf8(file.relative_path) << " >>>\n\n";
        }

        if (!scan_result.warnings.empty()) {
            report << "## Scan Warnings\n\n";
            for (const auto& warning : scan_result.warnings) {
                report << "- " << warning << '\n';
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to render report: {}", e.what());
        throw; // re-throw — caller handles it
    }
    LOG_DEBUG("Report rendered, {} chars", static_cast<std::streamoff>(report.tellp()));
    return report.str();
}

std::filesystem::path ReportWriter::ChooseOutputPath(
    const std::filesystem::path& output_directory) {
    LOG_DEBUG("Choosing output path in: {}", PathToUtf8(output_directory));
    std::error_code error;
    if (!std::filesystem::is_directory(output_directory, error) || error) {
        throw std::filesystem::filesystem_error("Invalid output directory", output_directory,
            error ? error : std::make_error_code(std::errc::not_a_directory));
    }
    for (std::size_t index = 0;; ++index) {
        const std::string name = index == 0 ? "describer.txt"
                                            : "describer" + std::to_string(index) + ".txt";
        const auto candidate = output_directory / name;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            throw std::filesystem::filesystem_error("Cannot inspect output path", candidate, error);
        }
        if (status.type() == std::filesystem::file_type::not_found) {
            LOG_DEBUG("Output path chosen: {}", PathToUtf8(candidate));
            return candidate;
        }
    }
}

bool ReportWriter::WriteUtf8(const std::filesystem::path& output_path,
                             const std::string& content,
                             std::string& error_message) {
    LOG_DEBUG("Writing report to: {}", PathToUtf8(output_path));
    error_message.clear();
    // Claim a private temporary directory atomically so simultaneous writers
    // cannot truncate each other's staging files.
    struct Staging {
        std::filesystem::path directory;
        std::filesystem::path file;
        ~Staging() {
            std::error_code ignored;
            if (!file.empty()) std::filesystem::remove(file, ignored);
            if (!directory.empty()) std::filesystem::remove(directory, ignored);
        }
    } staging;
    try {
        static std::atomic<std::size_t> sequence = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            const auto candidate = output_path.parent_path() /
                (".describer-" + std::to_string(stamp) + "-" +
                 std::to_string(sequence.fetch_add(1)) + ".tmp");
            if (std::filesystem::create_directory(candidate)) {
                staging.directory = candidate;
                staging.file = candidate / "report.tmp";
                break;
            }
        }
        {
            std::ofstream output(staging.file, std::ios::binary | std::ios::trunc);
            if (!output) {
                error_message = "Cannot create output file: " + PathToUtf8(staging.file);
                LOG_ERROR("{}", error_message);
                return false;
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();  // close/flush can fail even after write() succeeds.
            if (!output) {
                error_message = "Cannot write output file (disk full?)";
                LOG_ERROR("{}", error_message);
                return false;
            }
        }

        std::error_code rename_error;
#ifdef _WIN32
        if (!MoveFileExW(staging.file.c_str(), output_path.c_str(), 0)) {
            rename_error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }
#else
        // Unlike POSIX rename(), link creation never replaces an existing name.
        std::filesystem::create_hard_link(staging.file, output_path, rename_error);
#endif
        if (rename_error) {
            error_message = "Cannot finalize output file: " + ErrorToUtf8(rename_error);
            LOG_ERROR("{}", error_message);
            return false;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        error_message = std::string("Filesystem error during write: ") + e.what();
        LOG_ERROR("{}", error_message);
        return false;
    } catch (const std::exception& e) {
        error_message = std::string("Unexpected write error: ") + e.what();
        LOG_ERROR("{}", error_message);
        return false;
    }

    LOG_INFO("Report written successfully: {} ({} bytes)",
             PathToUtf8(output_path), content.size());
    return true;
}

}  // namespace project_describer
