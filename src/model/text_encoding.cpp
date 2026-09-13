#include "model/text_encoding.hpp"

namespace project_describer {

std::string_view EncodingName(const TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::kUtf8:
        return "UTF-8";
    case TextEncoding::kUtf8Bom:
        return "UTF-8 with BOM";
    case TextEncoding::kUtf16Le:
        return "UTF-16 LE";
    case TextEncoding::kUtf16Be:
        return "UTF-16 BE";
    case TextEncoding::kUtf32Le:
        return "UTF-32 LE";
    case TextEncoding::kUtf32Be:
        return "UTF-32 BE";
    case TextEncoding::kSystemAnsi:
        return "System ANSI code page";
    }

    return "Unknown";
}

}  // namespace project_describer
