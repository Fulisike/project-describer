#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/report_writer.hpp"

namespace {

class ScopedOutputDirectory {
public:
    ScopedOutputDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("project-describer-output-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~ScopedOutputDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

}  // namespace

TEST(RendersPathsNamesEncodingsAndContents) {
    project_describer::ScanResult scan;
    scan.entries = {{"src", true}, {std::filesystem::path("src") / "main.cpp", false}};
    scan.text_files.emplace_back(std::filesystem::path("src") / "main.cpp",
                                 project_describer::TextEncoding::kUtf8, "int main() {}\n");

    const auto report = project_describer::ReportWriter::Render("demo", scan);
    EXPECT_TRUE(report.find("src/main.cpp") != std::string::npos);
    EXPECT_TRUE(report.find("File name: main.cpp") != std::string::npos);
    EXPECT_TRUE(report.find("Detected encoding: UTF-8") != std::string::npos);
    EXPECT_TRUE(report.find("int main() {}") != std::string::npos);
}

TEST(ChoosesIncrementingOutputNameAndWritesContent) {
    ScopedOutputDirectory temp;
    std::ofstream(temp.path() / "describer.txt") << "existing";
    const auto output_path = project_describer::ReportWriter::ChooseOutputPath(temp.path());
    EXPECT_EQ(output_path.filename(), std::filesystem::path("describer1.txt"));

    std::string error;
    EXPECT_TRUE(project_describer::ReportWriter::WriteUtf8(output_path, "report content", error));
    std::ifstream input(output_path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, std::string("report content"));
}

TEST(LimitsEachFileIndependentlyAndKeepsShortFiles) {
    project_describer::ScanResult scan;
    scan.text_files.emplace_back("lf.txt", project_describer::TextEncoding::kUtf8,
                                 "first\n\nthird\nfourth");
    scan.text_files.emplace_back("crlf.txt", project_describer::TextEncoding::kUtf8,
                                 "alpha\r\nbeta\r\ngamma\r\n");
    scan.text_files.emplace_back("short.txt", project_describer::TextEncoding::kUtf8,
                                 "short without newline");
    scan.text_files.emplace_back("empty.txt", project_describer::TextEncoding::kUtf8, "");

    const auto limited = project_describer::ReportWriter::Render("demo", scan, 2);
    EXPECT_TRUE(limited.find("first\n\n<<< FILE END: lf.txt >>>") != std::string::npos);
    EXPECT_TRUE(limited.find("alpha\r\nbeta\r\n<<< FILE END: crlf.txt >>>") != std::string::npos);
    EXPECT_TRUE(limited.find("short without newline\n<<< FILE END: short.txt >>>") != std::string::npos);
    EXPECT_TRUE(limited.find("third") == std::string::npos);
    EXPECT_TRUE(limited.find("gamma") == std::string::npos);
    EXPECT_TRUE(limited.find("<<< FILE END: empty.txt >>>") != std::string::npos);

    const auto full = project_describer::ReportWriter::Render("demo", scan);
    EXPECT_EQ(project_describer::ReportWriter::Render("demo", scan, 0), full);
    EXPECT_TRUE(full.find("third\nfourth") != std::string::npos);
    EXPECT_TRUE(project_describer::ReportWriter::Render("demo", scan, 100).find(
                    "alpha\r\nbeta\r\ngamma\r\n") != std::string::npos);
    EXPECT_EQ(scan.text_files.front().content, std::string("first\n\nthird\nfourth"));
}
