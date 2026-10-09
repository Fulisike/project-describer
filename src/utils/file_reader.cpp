#include "utils/file_reader.hpp"

#include <fstream>
#include <limits>
#include <vector>

namespace project_describer {

FileReadResult FileReader::ReadTextFile(const std::filesystem::path& path,
                                       std::uintmax_t max_file_size) {
    FileReadResult result;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        result.error_message = "Cannot open file";
        return result;
    }
    const auto end = input.tellg();
    if (end < 0) {
        result.error_message = "Cannot determine file size";
        return result;
    }
    const auto file_size = static_cast<std::uintmax_t>(end);
    if (file_size > max_file_size) {
        result.error_message = "File exceeds size limit";
        return result;
    }
    if (file_size > static_cast<std::uintmax_t>((std::numeric_limits<std::size_t>::max)()) ||
        file_size > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)())) {
        result.error_message = "File is too large for this process";
        return result;
    }
    input.seekg(0);
    if (!input) {
        result.error_message = "Cannot seek file";
        return result;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            result.error_message = "Could not read the complete file";
            return result;
        }
    }

    if (!EncodingUtils::IsLikelyText(bytes)) {
        result.error_message = "Binary or non-text content";
        return result;
    }
    auto decoded = EncodingUtils::DecodeToUtf8(bytes);
    if (!decoded) {
        result.error_message = "Unsupported or invalid text encoding";
        return result;
    }

    result.success = true;
    result.text = std::move(*decoded);
    return result;
}

}  // namespace project_describer
