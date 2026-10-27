#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "application/command_line.hpp"
#include "application/project_describer.hpp"
#include "utils/logging.hpp"
#include "utils/user_notifier.hpp"

namespace {

void CliErrorMessage(const std::string& title, const std::string& detail) {
    std::cerr << "\n*** " << title << " ***\n" << detail << "\n\n";
}

void CliInfoMessage(const std::string& message) {
    std::cout << message << '\n';
}

int Run(const int argc, const char* const argv[]) {
    // Initialize logging — file goes next to the executable
    project_describer::log::Initialize(
        project_describer::log::Mode::kCli,
        std::filesystem::current_path());

    project_describer::SetUserErrorCallback(&CliErrorMessage);
    project_describer::SetUserInfoCallback(&CliInfoMessage);

    const auto parsed = project_describer::ParseCommandLine(argc, argv);
    if (parsed.status == project_describer::ParseStatus::kHelpRequested) {
        std::cout << project_describer::UsageText();
        return 0;
    }
    if (parsed.status == project_describer::ParseStatus::kError) {
        std::cerr << "Argument error: " << parsed.message << "\n\n";
        std::cerr << project_describer::UsageText();
        return 2;
    }
    return project_describer::RunProjectDescriber(*parsed.options, std::cout, std::cerr);
}

#ifdef _WIN32
std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
}
#endif

}  // namespace

#ifdef _WIN32
int wmain(const int argc, wchar_t* argv[]) {
    try {
        std::vector<std::string> utf8_arguments;
        utf8_arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index) {
            utf8_arguments.push_back(WideToUtf8(argv[index]));
        }
        std::vector<const char*> raw_arguments;
        raw_arguments.reserve(utf8_arguments.size());
        for (const auto& argument : utf8_arguments) {
            raw_arguments.push_back(argument.c_str());
        }
        return Run(argc, raw_arguments.data());
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}
#else
int main(const int argc, char* argv[]) {
    try {
        return Run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}
#endif
