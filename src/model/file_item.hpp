#pragma once

#include <filesystem>
#include <string>
#include <utility>

#include "model/text_encoding.hpp"

namespace project_describer {

struct FileItem {
        std::filesystem::path relative_path;
        std::string file_name;
        TextEncoding source_encoding = TextEncoding::kUtf8;
        std::string content;

        FileItem() = default;
        FileItem(std::filesystem::path path, TextEncoding encoding, std::string text)
            : relative_path(std::move(path)),
              source_encoding(encoding),
              content(std::move(text))
        {
            const auto utf8_name = relative_path.filename().generic_u8string();
            file_name.assign(utf8_name.begin(), utf8_name.end());
        }
    };

}  // namespace project_describer
