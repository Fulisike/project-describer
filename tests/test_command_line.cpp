#include "test_framework.hpp"

#include "application/command_line.hpp"

TEST(ParsesProjectAndOutputDirectories) {
    const char* arguments[] = {"project-describer", "-p", "demo", "-o", "reports"};
    const auto result = project_describer::ParseCommandLine(5, arguments);

    EXPECT_EQ(result.status, project_describer::ParseStatus::kOk);
    EXPECT_TRUE(result.options.has_value());
    EXPECT_EQ(result.options->project_root, std::filesystem::path("demo"));
    EXPECT_EQ(*result.options->output_directory, std::filesystem::path("reports"));
}

TEST(RequiresProjectDirectory) {
    const char* arguments[] = {"project-describer"};
    const auto result = project_describer::ParseCommandLine(1, arguments);

    EXPECT_EQ(result.status, project_describer::ParseStatus::kError);
}

TEST(RecognizesHelp) {
    const char* arguments[] = {"project-describer", "--help"};
    const auto result = project_describer::ParseCommandLine(2, arguments);

    EXPECT_EQ(result.status, project_describer::ParseStatus::kHelpRequested);
}
