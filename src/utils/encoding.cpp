#include "utils/encoding.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#include <cerrno>
#include <cstring>
#endif

namespace project_describer {

    // ============== 编码检测 ==============

    TextEncoding EncodingUtils::detectEncoding(const std::vector<uint8_t>& data)
    {
        if (data.empty())
        {
            return TextEncoding::Unknown;
        }

        // 1. 检查 UTF-8 BOM
        if (hasUtf8Bom(data))
        {
            return TextEncoding::UTF8_BOM;
        }

        // 2. 检查是否为纯 ASCII
        if (isAscii(data))
        {
            return TextEncoding::ASCII;
        }

        // 3. 使用系统 API 检查是否为有效的 UTF-8（无 BOM）
        std::string str(data.begin(), data.end());
        if (isValidUtf8System(str))
        {
            return TextEncoding::UTF8;
        }

        // 4. 如果不是有效的 UTF-8，再尝试检测 GBK
        if (isLikelyGbk(data))
        {
            return TextEncoding::GBK;
        }

        return TextEncoding::Unknown;
    }

    TextEncoding EncodingUtils::detectEncoding(const std::string& data)
    {
        std::vector<uint8_t> vec(data.begin(), data.end());
        return detectEncoding(vec);
    }

    bool EncodingUtils::hasUtf8Bom(const std::vector<uint8_t>& data)
    {
        if (data.size() < 3) return false;
        return data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
    }

    bool EncodingUtils::isAscii(const std::vector<uint8_t>& data)
    {
        return std::all_of(data.begin(), data.end(), [](uint8_t c)
            {
                return c <= 0x7F;
            });
    }

    // ============== 使用系统 API 验证 UTF-8 ==============

    bool EncodingUtils::isValidUtf8System(const std::string& str)
    {
        if (str.empty()) return true;

#ifdef _WIN32
        // Windows: 使用 MultiByteToWideChar 并指定 MB_ERR_INVALID_CHARS
        int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);
        return (wide_len != 0);
#else
        // Linux: 使用 iconv 尝试转换
        iconv_t cd = iconv_open("UTF-32LE", "UTF-8");
        if (cd == (iconv_t)-1)
        {
            // 如果无法打开转换器，回退到简单的 UTF-8 检查
            size_t i = 0;
            while (i < str.size())
            {
                uint8_t c = static_cast<uint8_t>(str[i]);
                size_t bytes = 0;
                if ((c & 0x80) == 0)
                {
                    bytes = 1;
                }
                else if ((c & 0xE0) == 0xC0)
                {
                    bytes = 2;
                }
                else if ((c & 0xF0) == 0xE0)
                {
                    bytes = 3;
                }
                else if ((c & 0xF8) == 0xF0)
                {
                    bytes = 4;
                }
                else
                {
                    return false;
                }
                if (i + bytes > str.size()) return false;
                for (size_t j = 1; j < bytes; ++j)
                {
                    if ((static_cast<uint8_t>(str[i + j]) & 0xC0) != 0x80)
                    {
                        return false;
                    }
                }
                i += bytes;
            }
            return true;
        }

        char* in_buf = const_cast<char*>(str.c_str());
        size_t in_bytes = str.size();
        size_t out_bytes = in_bytes * 4;
        std::vector<char> out_buf(out_bytes);
        char* out_ptr = out_buf.data();

        size_t result = iconv(cd, &in_buf, &in_bytes, &out_ptr, &out_bytes);
        iconv_close(cd);

        return (result != (size_t)-1);
#endif
    }

    // ============== GBK 检测 ==============

    bool EncodingUtils::isLikelyGbk(const std::vector<uint8_t>& data, double threshold)
    {
        size_t gbk_count = 0;
        size_t total = data.size();

        for (size_t i = 0; i < total; ++i)
        {
            uint8_t c = data[i];
            if (c >= 0x81 && c <= 0xFE)
            {
                if (i + 1 < total)
                {
                    uint8_t c2 = data[i + 1];
                    if ((c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0x80 && c2 <= 0xFE))
                    {
                        gbk_count++;
                        i++; // 跳过第二个字节
                    }
                }
            }
        }

        return (total > 0 && static_cast<double>(gbk_count) / total > threshold);
    }

    // ============== 编码转换 ==============

    std::string EncodingUtils::stripBom(const std::string& utf8_str)
    {
        if (utf8_str.size() >= 3 &&
            static_cast<uint8_t>(utf8_str[0]) == 0xEF &&
            static_cast<uint8_t>(utf8_str[1]) == 0xBB &&
            static_cast<uint8_t>(utf8_str[2]) == 0xBF)
        {
            return utf8_str.substr(3);
        }
        return utf8_str;
    }

#ifdef _WIN32
    // Windows 平台：使用 MultiByteToWideChar / WideCharToMultiByte

    std::string EncodingUtils::gbkToUtf8(const std::string& gbk_str)
    {
        if (gbk_str.empty()) return {};

        // GBK -> UTF-16
        int wide_len = MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, nullptr, 0);
        if (wide_len <= 0) return gbk_str;

        std::vector<wchar_t> wide_buf(wide_len);
        MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, wide_buf.data(), wide_len);

        // UTF-16 -> UTF-8
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_buf.data(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0) return gbk_str;

        std::vector<char> utf8_buf(utf8_len);
        WideCharToMultiByte(CP_UTF8, 0, wide_buf.data(), -1, utf8_buf.data(), utf8_len, nullptr, nullptr);

        std::string result(utf8_buf.data());
        return result;
    }

    std::string EncodingUtils::utf8ToGbk(const std::string& utf8_str)
    {
        if (utf8_str.empty()) return {};

        // UTF-8 -> UTF-16
        int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
        if (wide_len <= 0) return utf8_str;

        std::vector<wchar_t> wide_buf(wide_len);
        MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, wide_buf.data(), wide_len);

        // UTF-16 -> GBK
        int gbk_len = WideCharToMultiByte(CP_ACP, 0, wide_buf.data(), -1, nullptr, 0, nullptr, nullptr);
        if (gbk_len <= 0) return utf8_str;

        std::vector<char> gbk_buf(gbk_len);
        WideCharToMultiByte(CP_ACP, 0, wide_buf.data(), -1, gbk_buf.data(), gbk_len, nullptr, nullptr);

        std::string result(gbk_buf.data());
        return result;
    }

#else
    // Linux / macOS 平台：使用 iconv

    std::string EncodingUtils::gbkToUtf8(const std::string& gbk_str)
    {
        if (gbk_str.empty()) return {};

        iconv_t cd = iconv_open("UTF-8", "GBK");
        if (cd == (iconv_t)-1) return gbk_str;

        char* in_buf = const_cast<char*>(gbk_str.c_str());
        size_t in_bytes = gbk_str.size();
        size_t out_bytes = in_bytes * 4 + 1;
        std::vector<char> out_buf(out_bytes);
        char* out_ptr = out_buf.data();

        size_t result = iconv(cd, &in_buf, &in_bytes, &out_ptr, &out_bytes);
        iconv_close(cd);

        if (result == (size_t)-1) return gbk_str;

        return std::string(out_buf.data(), out_ptr - out_buf.data());
}

    std::string EncodingUtils::utf8ToGbk(const std::string& utf8_str)
    {
        if (utf8_str.empty()) return {};

        iconv_t cd = iconv_open("GBK", "UTF-8");
        if (cd == (iconv_t)-1) return utf8_str;

        char* in_buf = const_cast<char*>(utf8_str.c_str());
        size_t in_bytes = utf8_str.size();
        size_t out_bytes = in_bytes * 2 + 1;
        std::vector<char> out_buf(out_bytes);
        char* out_ptr = out_buf.data();

        size_t result = iconv(cd, &in_buf, &in_bytes, &out_ptr, &out_bytes);
        iconv_close(cd);

        if (result == (size_t)-1) return utf8_str;

        return std::string(out_buf.data(), out_ptr - out_buf.data());
    }

#endif

} // namespace project_describer