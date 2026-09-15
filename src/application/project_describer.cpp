#include "application/project_describer.hpp"

#include <filesystem>

#include "core/report_writer.hpp"
#include "core/scanner.hpp"
#include "utils/logging.hpp"
#include "utils/user_notifier.hpp"

namespace project_describer {

int RunProjectDescriber(const CommandLineOptions& options, std::ostream& output,
                        std::ostream& errors) {
    LOG_INFO("project-describer started");

    try {
        std::error_code ec;

        // Validate project root
        if (!std::filesystem::is_directory(options.project_root, ec) || ec) {
            const auto msg = "Error: Project root is not a readable directory:\n  " +
                             options.project_root.string();
            errors << msg << '\n';
            NotifyUserError("Invalid project root", options.project_root.string());
            return 2;
        }

        // Validate / create output directory
        const auto output_directory = options.output_directory.value_or(options.project_root);
        if (!std::filesystem::is_directory(output_directory, ec) || ec) {
            const auto msg = "Error: Output directory does not exist:\n  " +
                             output_directory.string();
            errors << msg << '\n';
            NotifyUserError("Invalid output directory", output_directory.string());
            return 2;
        }

        // Scan
        LOG_INFO("Step 1/3: Scanning project tree...");
        output << "Scanning project tree...\n";
        const auto scan_result = Scanner::Scan(options.project_root, output_directory);
        output << "  Files scanned:  " << scan_result.scanned_files << '\n';
        output << "  Text files found: " << scan_result.text_files.size() << '\n';
        LOG_INFO("Step 1/3 done: {} text files", scan_result.text_files.size());

        // Render
        LOG_INFO("Step 2/3: Rendering report...");
        output << "Rendering report...\n";
        const auto report = ReportWriter::Render(options.project_root, scan_result);
        LOG_INFO("Step 2/3 done: {} bytes", report.size());

        // Write
        LOG_INFO("Step 3/3: Writing file...");
        output << "Writing report...\n";
        const auto output_path = ReportWriter::ChooseOutputPath(output_directory);
        std::string write_error;
        if (!ReportWriter::WriteUtf8(output_path, report, write_error)) {
            errors << "Error: " << write_error << '\n';
            NotifyUserError("Write failed", write_error);
            return 1;
        }

        output << "\nDone! Wrote: " << output_path.string() << '\n';
        if (!scan_result.warnings.empty()) {
            output << "Warnings (" << scan_result.warnings.size()
                   << " total — see log for details)\n";
        }
        LOG_INFO("All done: {}", output_path.string());
        NotifyUserInfo("Report written to " + output_path.string());
        return 0;

    } catch (const std::bad_alloc& e) {
        LOG_CRITICAL("Out of memory: {}", e.what());
        errors << "Fatal: Out of memory — " << e.what() << '\n';
        NotifyUserError("Out of memory", e.what());
        return 1;
    } catch (const std::exception& e) {
        LOG_CRITICAL("Unhandled exception: {}", e.what());
        errors << "Fatal: " << e.what() << '\n';
        NotifyUserError("Unexpected error", e.what());
        return 1;
    }
}

}  // namespace project_describer
