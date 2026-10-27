#include "test_framework.hpp"

#include <chrono>
#include <filesystem>

#include "utils/logging.hpp"

TEST(LoggingSurvivesMissingFileSinkAndUninitializedUse) {
    namespace logging = project_describer::log;
    logging::Shutdown();
    EXPECT_TRUE(logging::Logger() != nullptr);
    LOG_INFO("Logging remains safe before initialization");
    const auto root = std::filesystem::temp_directory_path() /
        ("logging-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
        std::filesystem::path(u8"日志🚀");
    std::filesystem::create_directories(root);
    logging::Initialize(logging::Mode::kCli, root);
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "describer.log"));
    logging::Initialize(logging::Mode::kCli, root);  // Reinitialization must not double-register.
    logging::Shutdown();
    std::filesystem::remove(root / "describer.log");
    std::filesystem::create_directory(root / "describer.log");
    logging::Initialize(logging::Mode::kCli, root);
    LOG_INFO("An unwritable log file must not stop the application");
    logging::Shutdown();
    std::filesystem::remove(root / "describer.log");
    std::filesystem::remove(root);
    std::filesystem::remove(root.parent_path());
    logging::Initialize(logging::Mode::kCli, std::filesystem::temp_directory_path());
}
