#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace project_describer {

    /**
     * 文件读取结果
     */
    struct FileReadResult {
        bool success = false;
        std::string content;           // UTF-8 编码的内容
        std::string error_message;
    };

    /**
     * 文件读取器
     * 自动检测编码并转换为 UTF-8
     */
    class FileReader {
    public:
        /**
         * 读取文件内容，自动检测编码并转换为 UTF-8
         * @param path 文件路径
         * @return FileReadResult 包含内容或错误信息
         */
        static FileReadResult readFileAsUtf8(const std::filesystem::path& path);

        /**
         * 读取文件原始字节
         */
        static std::vector<uint8_t> readRawBytes(const std::filesystem::path& path);
    };

} // namespace project_describer