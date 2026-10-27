#include "test_framework.hpp"

#include <filesystem>

#include "utils/logging.hpp"

int main() {
    project_describer::log::Initialize(
        project_describer::log::Mode::kCli,
        std::filesystem::temp_directory_path());
    for (const auto& test_case : test::Cases()) {
        try {
            test_case.run();
        } catch (const std::exception& error) {
            ++test::Failures();
            std::cerr << test_case.name << ": unexpected exception: " << error.what() << '\n';
        }
    }
    project_describer::log::Shutdown();
    if (test::Failures() == 0) {
        std::cout << test::Cases().size() << " tests passed\n";
        return 0;
    }
    std::cerr << test::Failures() << " expectations failed\n";
    return 1;
}
