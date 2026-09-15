#include "core/report_writer.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include "model/text_encoding.hpp"
#include "utils/logging.hpp"

namespace project_describer {
namespace {

std::string DisplayPath(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

std::string DirectoryLine(const ProjectEntry& entry) {
    const auto components = static_cast<std::size_t>(
        std::distance(entry.relative_path.begin(), entry.relative_path.end()));
    std::string line((components > 0 ? components - 1 : 0) * 2, ' ');
    line += DisplayPath(entry.relative_path.filename());
    if (entry.is_directory) {
        line += '/';
    }
    return line;
}

}  // namespace

std::string ReportWriter::Render(const std::filesystem::path& project_root,
                                 const ScanResult& scan_result) {
    LOG_DEBUG("Rendering report for project: {}", project_root.string());
    std::ostringstream report;
    try {
        report << "# Project Description\n\n";
        report << "Project root: " << DisplayPath(project_root) << "\n\n";
        report << "## Directory Structure\n\n";
        report << ".\n";
        for (const auto& entry : scan_result.entries) {
            report << DirectoryLine(entry) << '\n';
        }
        report << "\n## Text File Contents\n";
        report << "Text files included: " << scan_result.text_files.size() << "\n\n";

        for (const auto& file : scan_result.text_files) {
            report << "<<< FILE START: " << DisplayPath(file.relative_path) << " >>>\n";
            report << "File name: " << file.file_name << '\n';
            report << "Detected encoding: " << EncodingName(file.source_encoding) << "\n";
            report << "--- CONTENT ---\n";
            report << file.content;
            if (!file.content.empty() && file.content.back() != '\n') {
                report << '\n';
            }
            report << "<<< FILE END: " << DisplayPath(file.relative_path) << " >>>\n\n";
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
    LOG_DEBUG("Choosing output path in: {}", output_directory.string());
    for (std::size_t index = 0;; ++index) {
        const std::string name = index == 0 ? "describer.txt"
                                            : "describer" + std::to_string(index) + ".txt";
        const auto candidate = output_directory / name;
        std::error_code error;
        if (!std::filesystem::exists(candidate, error)) {
            LOG_DEBUG("Output path chosen: {}", candidate.string());
            return candidate;
        }
    }
}

bool ReportWriter::WriteUtf8(const std::filesystem::path& output_path,
                             const std::string& content,
                             std::string& error_message) {
    LOG_DEBUG("Writing report to: {}", output_path.string());

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = output_path.parent_path() /
        ("." + output_path.filename().string() + "." + std::to_string(stamp) + ".tmp");

    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                error_message = "Cannot create output file: " + temporary.string();
                LOG_ERROR("{}", error_message);
                return false;
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!output) {
                error_message = "Cannot write output file (disk full?)";
                LOG_ERROR("{}", error_message);
                std::error_code cleanup_error;
                std::filesystem::remove(temporary, cleanup_error);
                return false;
            }
        }

        std::error_code rename_error;
        std::filesystem::rename(temporary, output_path, rename_error);
        if (rename_error) {
            std::filesystem::remove(temporary, rename_error);
            error_message = "Cannot finalize output file: " + rename_error.message();
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
             output_path.string(), content.size());
    return true;
}

}  // namespace project_describer
