#pragma once

#include <filesystem>
#include <string>

#include "utils/encoding.hpp"

namespace project_describer {

struct FileReadResult {
    bool success = false;
    DecodedText text{TextEncoding::kUtf8, {}};
    std::string error_message;
};

class FileReader {
public:
    static FileReadResult ReadTextFile(const std::filesystem::path& path);
};

}  // namespace project_describer
