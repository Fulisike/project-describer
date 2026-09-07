#pragma once

#include <string>
#include <vector>

namespace project_describer {

    /**
     * 文件项：表示一个被扫描到的文本文件
     * 包含相对路径和完整内容
     */
    struct FileItem {
        std::string relative_path;   // 相对于项目根目录的路径
        std::string content;         // 文件内容（统一为 UTF-8 编码）

        FileItem() = default;
        FileItem(const std::string& path, const std::string& text)
            : relative_path(path), content(text)
        {
        }
    };

} // namespace project_describer