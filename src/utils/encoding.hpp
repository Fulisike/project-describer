#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/text_encoding.hpp"

namespace project_describer {

struct DecodedText {
    TextEncoding source_encoding;
    std::string utf8;
};

class EncodingUtils {
public:
    static bool IsLikelyText(const std::vector<std::uint8_t>& data);
    static std::optional<DecodedText> DecodeToUtf8(const std::vector<std::uint8_t>& data);
};

}  // namespace project_describer
