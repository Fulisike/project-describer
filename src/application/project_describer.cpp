#include "application/project_describer.hpp"

#include <filesystem>

#include "core/report_writer.hpp"
#include "core/scanner.hpp"

namespace project_describer {

int RunProjectDescriber(const CommandLineOptions& options, std::ostream& output,
                        std::ostream& errors) {
    std::error_code error;
    if (!std::filesystem::is_directory(options.project_root, error) || error) {
        errors << "Project root is not a readable directory.\n";
        return 2;
    }

    const auto output_directory = options.output_directory.value_or(options.project_root);
    if (!std::filesystem::is_directory(output_directory, error) || error) {
        errors << "Output directory does not exist or is not a directory.\n";
        return 2;
    }

    const auto scan_result = Scanner::Scan(options.project_root, output_directory);
    const auto report = ReportWriter::Render(options.project_root, scan_result);
    const auto output_path = ReportWriter::ChooseOutputPath(output_directory);
    std::string write_error;
    if (!ReportWriter::WriteUtf8(output_path, report, write_error)) {
        errors << "Could not write description: " << write_error << '\n';
        return 1;
    }

    output << "Wrote: " << output_path.string() << '\n';
    output << "Text files: " << scan_result.text_files.size() << '\n';
    if (!scan_result.warnings.empty()) {
        output << "Warnings: " << scan_result.warnings.size() << '\n';
    }
    return 0;
}

}  // namespace project_describer
