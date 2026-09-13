#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "application/command_line.hpp"
#include "application/project_describer.hpp"

namespace {

int Run(const int argc, const char* const argv[]) {
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
}
#else
int main(const int argc, char* argv[]) {
    return Run(argc, argv);
}
#endif
