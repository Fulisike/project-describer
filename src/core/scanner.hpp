#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>

#include "model/file_item.hpp"

namespace project_describer {

    /**
     * 目录扫描器
     * 递归遍历目录，过滤出文本文件
     */
    class Scanner {
    public:
        /**
         * 扫描结果
         */
        struct ScanResult {
            std::vector<FileItem> files;   // 所有文本文件
            size_t total_files = 0;        // 扫描到的总文件数
            size_t text_files = 0;         // 文本文件数
            std::vector<std::string> errors; // 错误信息
        };

        /**
         * 扫描目录
         * @param root_path 项目根目录
         * @param progress_callback 进度回调（可选），参数为当前文件路径
         * @return ScanResult
         */
        static ScanResult scan(
            const std::filesystem::path& root_path,
            std::function<void(const std::string&)> progress_callback = nullptr
        );

    private:
        /**
         * 判断文件是否为文本文件（基于扩展名）
         */
        static bool isTextFile(const std::filesystem::path& path);

        /**
         * 获取文件相对于根目录的路径
         */
        static std::string getRelativePath(
            const std::filesystem::path& root,
            const std::filesystem::path& file
        );

        /**
         * 文本文件扩展名列表
         */
        static const std::vector<std::string> TEXT_EXTENSIONS;
    };

} // namespace project_describer