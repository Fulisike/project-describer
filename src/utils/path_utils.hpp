#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace project_describer {

inline std::string PathToUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

inline std::string ErrorToUtf8(const std::error_code& error) {
    const auto text = error.message();
#ifdef _WIN32
    const int length = MultiByteToWideChar(CP_ACP, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length > 0) {
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string utf8(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, utf8.data(), bytes, nullptr, nullptr);
            return utf8;
        }
    }
#endif
    return text;
}

inline bool IsPathLink(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return std::filesystem::is_symlink(path, error) || error;
#endif
}

}  // namespace project_describer
