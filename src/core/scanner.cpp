#include "core/scanner.hpp"
#include "utils/file_reader.hpp"

#include <filesystem>
#include <algorithm>
#include <set>

namespace project_describer {

    // 文本文件扩展名列表
    const std::vector<std::string> Scanner::TEXT_EXTENSIONS = {
        // 代码
        ".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx",
        ".java", ".py", ".rb", ".go", ".rs", ".swift",
        ".js", ".ts", ".jsx", ".tsx",
        ".html", ".htm", ".css", ".scss", ".less",
        ".json", ".yaml", ".yml", ".toml", ".xml",
        ".sql", ".sh", ".bash", ".zsh", ".fish",
        ".cmake", ".mk", ".makefile",
        // 文档
        ".md", ".markdown", ".txt", ".text",
        ".rst", ".org",
        // 配置
        ".ini", ".cfg", ".conf", ".config",
        ".env", ".properties",
        ".gitignore", ".gitattributes", ".dockerignore",
        // 其他
        ".log", ".csv", ".tsv",
    };

    bool Scanner::isTextFile(const std::filesystem::path& path)
    {
        // 检查是否为常规文件
        if (!std::filesystem::is_regular_file(path))
        {
            return false;
        }

        // 检查扩展名
        std::string ext = path.extension().string();
        // 转换为小写进行比较
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        return std::find(TEXT_EXTENSIONS.begin(), TEXT_EXTENSIONS.end(), ext) != TEXT_EXTENSIONS.end();
    }

    std::string Scanner::getRelativePath(
        const std::filesystem::path& root,
        const std::filesystem::path& file
    )
    {
        // 使用 std::filesystem::relative 计算相对路径
        std::filesystem::path rel = std::filesystem::relative(file, root);
        return rel.string();
    }

    Scanner::ScanResult Scanner::scan(
        const std::filesystem::path& root_path,
        std::function<void(const std::string&)> progress_callback
    )
    {
        ScanResult result;

        if (!std::filesystem::exists(root_path))
        {
            result.errors.push_back("Root path does not exist: " + root_path.string());
            return result;
        }

        if (!std::filesystem::is_directory(root_path))
        {
            result.errors.push_back("Root path is not a directory: " + root_path.string());
            return result;
        }

        try
        {
            // 使用 recursive_directory_iterator 递归遍历
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                root_path,
                std::filesystem::directory_options::skip_permission_denied))
            {

                result.total_files++;

                if (progress_callback)
                {
                    progress_callback(entry.path().string());
                }

                // 检查是否为文本文件
                if (!isTextFile(entry.path()))
                {
                    continue;
                }

                // 读取文件内容
                FileReadResult read_result = FileReader::readFileAsUtf8(entry.path());
                if (!read_result.success)
                {
                    result.errors.push_back(
                        "Failed to read: " + entry.path().string() +
                        " - " + read_result.error_message
                    );
                    continue;
                }

                // 构造 FileItem
                std::string rel_path = getRelativePath(root_path, entry.path());
                result.files.emplace_back(rel_path, read_result.content);
                result.text_files++;
            }
        }
        catch (const std::exception& e)
        {
            result.errors.push_back("Scan error: " + std::string(e.what()));
        }

        return result;
    }

} // namespace project_describer