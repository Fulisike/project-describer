#include "utils/file_reader.hpp"
#include "utils/encoding.hpp"

#include <fstream>
#include <iostream>
#include <filesystem>

namespace project_describer {

    std::vector<uint8_t> FileReader::readRawBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return {};
        }

        file.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    FileReadResult FileReader::readFileAsUtf8(const std::filesystem::path& path)
    {
        FileReadResult result;

        // 1. 读取原始字节
        auto raw_data = readRawBytes(path);
        if (raw_data.empty())
        {
            result.error_message = "File is empty or cannot be read";
            return result;
        }

        // 2. 检测编码
        TextEncoding encoding = EncodingUtils::detectEncoding(raw_data);
        std::string raw_str(raw_data.begin(), raw_data.end());

        switch (encoding)
        {
        case TextEncoding::ASCII:
        case TextEncoding::UTF8:
            result.content = raw_str;
            break;

        case TextEncoding::UTF8_BOM:
            result.content = EncodingUtils::stripBom(raw_str);
            break;

        case TextEncoding::GBK:
            result.content = EncodingUtils::gbkToUtf8(raw_str);
            break;

        case TextEncoding::Unknown:
        default:
            // 尝试作为 UTF-8 处理，使用系统 API 验证
            if (EncodingUtils::isValidUtf8System(raw_str))
            {
                result.content = raw_str;
            }
            else
            {
                // 最后尝试作为 GBK 转换
                result.content = EncodingUtils::gbkToUtf8(raw_str);
            }
            break;
        }

        result.success = true;
        return result;
    }

} // namespace project_describer