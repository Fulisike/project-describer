#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/scanner.hpp"
#include "utils/file_reader.hpp"

namespace {

class ScopedDirectory {
public:
    ScopedDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("project-describer-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }

    ~ScopedDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void Write(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

}  // namespace

TEST(ScansReadableTextInsteadOfOnlyKnownExtensions) {
    ScopedDirectory temp;
    const auto root = temp.path() / "project";
    std::filesystem::create_directories(root / "nested");
    Write(root / "README.md", "# Demo\n");
    Write(root / "nested" / "NOTICE", "plain text without extension\n");
    Write(root / "binary.bin", std::string("PNG\0\1\2", 6));
    Write(root / "describer.txt", "old generated report");

    const auto scan = project_describer::Scanner::Scan(root, root);
    EXPECT_EQ(scan.text_files.size(), std::size_t{2});
    EXPECT_EQ(scan.scanned_files, std::size_t{4});
    EXPECT_EQ(scan.text_files[0].relative_path, std::filesystem::path("README.md"));
    EXPECT_EQ(scan.text_files[1].relative_path, std::filesystem::path("nested") / "NOTICE");

    bool generated_entry_found = false;
    for (const auto& entry : scan.entries) {
        generated_entry_found = generated_entry_found || entry.relative_path == "describer.txt";
    }
    EXPECT_TRUE(!generated_entry_found);
}

TEST(ParallelScanMatchesSerialAcrossQueueAndEncodingBoundaries) {
    ScopedDirectory temp;
    const auto root = temp.path() / "project";
    for (int index = 0; index < 260; ++index) {
        Write(root / std::to_string(index % 13) / (std::to_string(index) + ".txt"),
              "text " + std::to_string(index) + "\n");
    }
    Write(root / std::filesystem::path(u8"中文.txt"), "\xEF\xBB\xBF\xE4\xB8\xAD\xE6\x96\x87");
    Write(root / "utf16.txt", std::string("\xFF\xFE" "h\0i\0", 6));
    Write(root / "empty.txt", "");
    Write(root / "binary.bin", std::string("PNG\0\1\2", 6));
    Write(root / "invalid.txt", std::string("\xFF\xFE\x01", 3));
    Write(root / "large.txt", std::string(2048, 'x'));
    Write(root / "describer42.txt", "old report");
    Write(root / "node_modules" / "ignored.txt", "ignored");
    Write(root / "nested" / "describer.txt", "ordinary file outside output directory");
    project_describer::ScannerConfig config;
    config.max_file_size = 1024;
    config.worker_count = 1;
    const auto serial = project_describer::Scanner::Scan(root, root, config);
    EXPECT_EQ(serial.text_files.size(), std::size_t{264});
    EXPECT_EQ(serial.warnings.size(), std::size_t{2});
    for (const std::size_t workers : {std::size_t{2}, std::size_t{8}}) {
        config.worker_count = workers;
        const auto parallel = project_describer::Scanner::Scan(root, root, config);
        EXPECT_EQ(parallel.scanned_files, serial.scanned_files);
        EXPECT_EQ(parallel.entries.size(), serial.entries.size());
        EXPECT_EQ(parallel.text_files.size(), serial.text_files.size());
        EXPECT_EQ(parallel.warnings, serial.warnings);
        for (std::size_t index = 0; index < parallel.entries.size(); ++index) {
            EXPECT_EQ(parallel.entries[index].relative_path, serial.entries[index].relative_path);
            EXPECT_EQ(parallel.entries[index].is_directory, serial.entries[index].is_directory);
        }
        for (std::size_t index = 0; index < parallel.text_files.size(); ++index) {
            EXPECT_EQ(parallel.text_files[index].relative_path, serial.text_files[index].relative_path);
            EXPECT_EQ(parallel.text_files[index].content, serial.text_files[index].content);
            EXPECT_EQ(parallel.text_files[index].source_encoding, serial.text_files[index].source_encoding);
        }
    }
}

TEST(ReaderEnforcesSizeLimitAtOpen) {
    ScopedDirectory temp;
    Write(temp.path() / "growing.txt", std::string(2048, 'x'));
    const auto read = project_describer::FileReader::ReadTextFile(temp.path() / "growing.txt", 1024);
    EXPECT_TRUE(!read.success);
    EXPECT_EQ(read.error_message, std::string("File exceeds size limit"));
}

TEST(ScanDoesNotFollowDirectoryJunctionsOrFileLinks) {
    ScopedDirectory temp;
    const auto root = temp.path() / "project";
    Write(root / "inside.txt", "inside");
    Write(temp.path() / "outside" / "outside.txt", "outside");
    std::error_code error;
    std::filesystem::create_directory_symlink(temp.path() / "outside", root / "link", error);
    // Creating symbolic links on Windows may require Developer Mode or elevation.
    if (!error) {
        const auto result = project_describer::Scanner::Scan(root, root);
        EXPECT_EQ(result.text_files.size(), std::size_t{1});
    }
    error.clear();
    std::filesystem::create_symlink(temp.path() / "outside" / "outside.txt",
                                     root / "linked.txt", error);
    if (!error) {
        const auto result = project_describer::Scanner::Scan(root, root);
        EXPECT_EQ(result.text_files.size(), std::size_t{1});
    }
}
