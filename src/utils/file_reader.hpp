#pragma once

#include <filesystem>
#include <limits>
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
    static FileReadResult ReadTextFile(
        const std::filesystem::path& path,
        std::uintmax_t max_file_size = (std::numeric_limits<std::uintmax_t>::max)());
};

}  // namespace project_describer
