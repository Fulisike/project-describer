#include "utils/encoding.hpp"

#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif

namespace project_describer {
namespace {

bool StartsWith(const std::vector<std::uint8_t>& data,
                const std::initializer_list<std::uint8_t> prefix) {
    return data.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), data.begin());
}

bool IsContinuation(const std::uint8_t byte) {
    return (byte & 0xC0) == 0x80;
}

bool IsValidUtf8(const std::vector<std::uint8_t>& data) {
    for (std::size_t index = 0; index < data.size();) {
        const std::uint8_t first = data[index];
        if (first < 0x80) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        std::uint32_t code_point = 0;
        if ((first & 0xE0) == 0xC0) {
            length = 2;
            code_point = first & 0x1F;
        } else if ((first & 0xF0) == 0xE0) {
            length = 3;
            code_point = first & 0x0F;
        } else if ((first & 0xF8) == 0xF0) {
            length = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }

        if (index + length > data.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            if (!IsContinuation(data[index + offset])) {
                return false;
            }
            code_point = (code_point << 6) | (data[index + offset] & 0x3F);
        }

        const std::uint32_t minimum = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
        if (code_point < minimum || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        index += length;
    }

    return true;
}

void AppendUtf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::optional<bool> DetectUtf16LittleEndian(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4 || data.size() % 2 != 0) {
        return std::nullopt;
    }

    const std::size_t sample = std::min<std::size_t>(data.size(), 512);
    std::size_t even_zeroes = 0;
    std::size_t odd_zeroes = 0;
    for (std::size_t index = 0; index < sample; index += 2) {
        even_zeroes += data[index] == 0;
        odd_zeroes += data[index + 1] == 0;
    }

    const std::size_t pairs = sample / 2;
    if (odd_zeroes * 100 >= pairs * 35 && odd_zeroes > even_zeroes * 2) {
        return true;
    }
    if (even_zeroes * 100 >= pairs * 35 && even_zeroes > odd_zeroes * 2) {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> DecodeUtf16(const std::vector<std::uint8_t>& data,
                                        const std::size_t offset,
                                        const bool little_endian) {
    if ((data.size() - offset) % 2 != 0) {
        return std::nullopt;
    }

    std::string output;
    output.reserve(data.size());
    for (std::size_t index = offset; index < data.size(); index += 2) {
        const auto read_unit = [&](const std::size_t position) {
            return little_endian
                ? static_cast<std::uint16_t>(data[position] | (data[position + 1] << 8))
                : static_cast<std::uint16_t>((data[position] << 8) | data[position + 1]);
        };

        std::uint32_t code_point = read_unit(index);
        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
            if (index + 3 >= data.size()) {
                return std::nullopt;
            }
            const std::uint32_t low = read_unit(index + 2);
            if (low < 0xDC00 || low > 0xDFFF) {
                return std::nullopt;
            }
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
            index += 2;
        } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
            return std::nullopt;
        }
        AppendUtf8(output, code_point);
    }
    return output;
}

std::optional<std::string> DecodeUtf32(const std::vector<std::uint8_t>& data,
                                        const std::size_t offset,
                                        const bool little_endian) {
    if ((data.size() - offset) % 4 != 0) {
        return std::nullopt;
    }

    std::string output;
    output.reserve(data.size());
    for (std::size_t index = offset; index < data.size(); index += 4) {
        const std::uint32_t code_point = little_endian
            ? static_cast<std::uint32_t>(data[index]) |
                  (static_cast<std::uint32_t>(data[index + 1]) << 8) |
                  (static_cast<std::uint32_t>(data[index + 2]) << 16) |
                  (static_cast<std::uint32_t>(data[index + 3]) << 24)
            : (static_cast<std::uint32_t>(data[index]) << 24) |
                  (static_cast<std::uint32_t>(data[index + 1]) << 16) |
                  (static_cast<std::uint32_t>(data[index + 2]) << 8) |
                  static_cast<std::uint32_t>(data[index + 3]);
        if (code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return std::nullopt;
        }
        AppendUtf8(output, code_point);
    }
    return output;
}

#ifdef _WIN32
std::optional<std::string> DecodeSystemAnsi(const std::vector<std::uint8_t>& data) {
    if (data.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }

    const int input_size = static_cast<int>(data.size());
    const int wide_size = MultiByteToWideChar(GetACP(), MB_ERR_INVALID_CHARS,
                                              reinterpret_cast<const char*>(data.data()),
                                              input_size, nullptr, 0);
    if (wide_size <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(GetACP(), MB_ERR_INVALID_CHARS,
                            reinterpret_cast<const char*>(data.data()), input_size,
                            wide.data(), wide_size) <= 0) {
        return std::nullopt;
    }

    const int utf8_size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                               wide.data(), wide_size, nullptr, 0,
                                               nullptr, nullptr);
    if (utf8_size <= 0) {
        return std::nullopt;
    }
    std::string utf8(static_cast<std::size_t>(utf8_size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_size,
                            utf8.data(), utf8_size, nullptr, nullptr) <= 0) {
        return std::nullopt;
    }
    return utf8;
}
#endif

bool HasTooManyControlBytes(const std::vector<std::uint8_t>& data) {
    const std::size_t controls = static_cast<std::size_t>(std::count_if(
        data.begin(), data.end(), [](const std::uint8_t byte) {
            return byte < 0x09 || (byte > 0x0D && byte < 0x20);
        }));
    return controls * 100 > data.size() * 4;
}

}  // namespace

bool EncodingUtils::IsLikelyText(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return true;
    }
    if (StartsWith(data, {0xFF, 0xFE, 0x00, 0x00}) ||
        StartsWith(data, {0x00, 0x00, 0xFE, 0xFF}) ||
        StartsWith(data, {0xFF, 0xFE}) || StartsWith(data, {0xFE, 0xFF}) ||
        DetectUtf16LittleEndian(data).has_value()) {
        const auto decoded = DecodeToUtf8(data);
        // Keep malformed text on the decoding-error path, but inspect valid
        // Unicode for the same binary controls as ordinary UTF-8 text.
        if (!decoded) return true;
        const std::vector<std::uint8_t> utf8(decoded->utf8.begin(), decoded->utf8.end());
        return std::find(utf8.begin(), utf8.end(), 0) == utf8.end() &&
               !HasTooManyControlBytes(utf8);
    }
    if (std::find(data.begin(), data.end(), 0) != data.end()) {
        return false;
    }
    return !HasTooManyControlBytes(data);
}

std::optional<DecodedText> EncodingUtils::DecodeToUtf8(const std::vector<std::uint8_t>& data) {
    if (data.empty()) {
        return DecodedText{TextEncoding::kUtf8, {}};
    }
    if (StartsWith(data, {0xFF, 0xFE, 0x00, 0x00})) {
        const auto text = DecodeUtf32(data, 4, true);
        return text ? std::optional<DecodedText>{{TextEncoding::kUtf32Le, *text}} : std::nullopt;
    }
    if (StartsWith(data, {0x00, 0x00, 0xFE, 0xFF})) {
        const auto text = DecodeUtf32(data, 4, false);
        return text ? std::optional<DecodedText>{{TextEncoding::kUtf32Be, *text}} : std::nullopt;
    }
    if (StartsWith(data, {0xFF, 0xFE})) {
        const auto text = DecodeUtf16(data, 2, true);
        return text ? std::optional<DecodedText>{{TextEncoding::kUtf16Le, *text}} : std::nullopt;
    }
    if (StartsWith(data, {0xFE, 0xFF})) {
        const auto text = DecodeUtf16(data, 2, false);
        return text ? std::optional<DecodedText>{{TextEncoding::kUtf16Be, *text}} : std::nullopt;
    }
    if (StartsWith(data, {0xEF, 0xBB, 0xBF})) {
        std::vector<std::uint8_t> without_bom(data.begin() + 3, data.end());
        if (!IsValidUtf8(without_bom)) {
            return std::nullopt;
        }
        return DecodedText{TextEncoding::kUtf8Bom,
                           std::string(without_bom.begin(), without_bom.end())};
    }
    if (const auto utf16_order = DetectUtf16LittleEndian(data)) {
        const auto text = DecodeUtf16(data, 0, *utf16_order);
        return text ? std::optional<DecodedText>{{*utf16_order ? TextEncoding::kUtf16Le : TextEncoding::kUtf16Be,
                                                   *text}}
                    : std::nullopt;
    }
    if (IsValidUtf8(data)) {
        return DecodedText{TextEncoding::kUtf8, std::string(data.begin(), data.end())};
    }
#ifdef _WIN32
    if (const auto ansi = DecodeSystemAnsi(data)) {
        return DecodedText{TextEncoding::kSystemAnsi, *ansi};
    }
#endif
    return std::nullopt;
}

}  // namespace project_describer
