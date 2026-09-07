#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace project_describer {

    enum class TextEncoding {
        Unknown,
        UTF8,
        UTF8_BOM,
        GBK,
        ASCII,
    };

    class EncodingUtils {
    public:
        static TextEncoding detectEncoding(const std::vector<uint8_t>& data);
        static TextEncoding detectEncoding(const std::string& data);

        static std::string gbkToUtf8(const std::string& gbk_str);
        static std::string utf8ToGbk(const std::string& utf8_str);
        static std::string stripBom(const std::string& utf8_str);

        // 使用系统 API 验证是否为有效的 UTF-8
        static bool isValidUtf8System(const std::string& str);

    private:
        static bool hasUtf8Bom(const std::vector<uint8_t>& data);
        static bool isAscii(const std::vector<uint8_t>& data);
        static bool isLikelyGbk(const std::vector<uint8_t>& data, double threshold = 0.35);
    };

} // namespace project_describer