#pragma once

#include <string_view>

namespace project_describer {

enum class TextEncoding {
    kUtf8,
    kUtf8Bom,
    kUtf16Le,
    kUtf16Be,
    kUtf32Le,
    kUtf32Be,
    kSystemAnsi,
};

std::string_view EncodingName(TextEncoding encoding);

}  // namespace project_describer
