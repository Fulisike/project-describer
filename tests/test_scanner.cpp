#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "core/scanner.hpp"

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
