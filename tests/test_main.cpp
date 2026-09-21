#include "test_framework.hpp"

#include <filesystem>

#include "utils/logging.hpp"

int main() {
    project_describer::log::Initialize(
        project_describer::log::Mode::kCli,
        std::filesystem::temp_directory_path().string());
    for (const auto& test_case : test::Cases()) {
        test_case.run();
    }
    project_describer::log::Shutdown();
    if (test::Failures() == 0) {
        std::cout << test::Cases().size() << " tests passed\n";
        return 0;
    }
    std::cerr << test::Failures() << " expectations failed\n";
    return 1;
}
