#include "test_framework.hpp"

#include <cstdint>
#include <vector>
#include <random>

#ifdef _WIN32
#include <windows.h>
#endif

#include "utils/encoding.hpp"

TEST(DecodesUtf8AndUtf8Bom) {
    const std::vector<std::uint8_t> utf8 = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};
    const auto decoded = project_describer::EncodingUtils::DecodeToUtf8(utf8);
    EXPECT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->source_encoding, project_describer::TextEncoding::kUtf8);
    EXPECT_EQ(decoded->utf8, std::string("\xE4\xB8\xAD\xE6\x96\x87"));

    const std::vector<std::uint8_t> bom = {0xEF, 0xBB, 0xBF, 'o', 'k'};
    const auto bom_decoded = project_describer::EncodingUtils::DecodeToUtf8(bom);
    EXPECT_TRUE(bom_decoded.has_value());
    EXPECT_EQ(bom_decoded->source_encoding, project_describer::TextEncoding::kUtf8Bom);
    EXPECT_EQ(bom_decoded->utf8, std::string("ok"));
}

TEST(DecodesUtf16WithoutBomAndUtf32WithBom) {
    const std::vector<std::uint8_t> utf16_le = {'H', 0, 'i', 0, '\n', 0};
    const auto utf16 = project_describer::EncodingUtils::DecodeToUtf8(utf16_le);
    EXPECT_TRUE(utf16.has_value());
    EXPECT_EQ(utf16->source_encoding, project_describer::TextEncoding::kUtf16Le);
    EXPECT_EQ(utf16->utf8, std::string("Hi\n"));

    const std::vector<std::uint8_t> utf32_le = {0xFF, 0xFE, 0, 0, 'A', 0, 0, 0};
    const auto utf32 = project_describer::EncodingUtils::DecodeToUtf8(utf32_le);
    EXPECT_TRUE(utf32.has_value());
    EXPECT_EQ(utf32->source_encoding, project_describer::TextEncoding::kUtf32Le);
    EXPECT_EQ(utf32->utf8, std::string("A"));
}

TEST(RejectsBinaryData) {
    const std::vector<std::uint8_t> binary = {'P', 'N', 'G', 0, 1, 2, 3};
    EXPECT_TRUE(!project_describer::EncodingUtils::IsLikelyText(binary));
}

TEST(RejectsBinaryControlsEvenWithAUnicodeBom) {
    for (const std::vector<std::uint8_t> bytes : {
             std::vector<std::uint8_t>{0xFF, 0xFE, 1, 0, 2, 0, 3, 0},
             std::vector<std::uint8_t>{0xFE, 0xFF, 0, 1, 0, 2, 0, 3},
             std::vector<std::uint8_t>{0, 0, 0xFE, 0xFF, 0, 0, 0, 0}}) {
        EXPECT_TRUE(!project_describer::EncodingUtils::IsLikelyText(bytes));
    }
}

TEST(RejectsMalformedUnicodeAndDecodesSupplementaryCharacters) {
    EXPECT_TRUE(!project_describer::EncodingUtils::DecodeToUtf8({0xFF, 0xFE, 0x00, 0xD8}));
    EXPECT_TRUE(!project_describer::EncodingUtils::DecodeToUtf8({0xFE, 0xFF, 0xDC, 0x00}));
    EXPECT_TRUE(!project_describer::EncodingUtils::DecodeToUtf8({0xEF, 0xBB, 0xBF, 0xC0, 0x80}));
    EXPECT_TRUE(!project_describer::EncodingUtils::DecodeToUtf8({0, 0, 0xFE, 0xFF, 0, 0x11, 0, 0}));
    const auto emoji = project_describer::EncodingUtils::DecodeToUtf8({0xFF, 0xFE, 0x3D, 0xD8, 0x80, 0xDE});
    EXPECT_TRUE(emoji.has_value());
    if (emoji) EXPECT_EQ(emoji->utf8, std::string("\xF0\x9F\x9A\x80"));
}

TEST(RandomizedDecoderInputsAlwaysProduceValidUtf8OrRejection) {
    std::mt19937 generator(20261026);
    for (int iteration = 0; iteration < 10000; ++iteration) {
        std::vector<std::uint8_t> bytes(generator() % 65);
        for (auto& byte : bytes) byte = static_cast<std::uint8_t>(generator());
        const std::vector<std::vector<std::uint8_t>> prefixes = {
            {}, {0xFF, 0xFE}, {0xFE, 0xFF}, {0xEF, 0xBB, 0xBF},
            {0xFF, 0xFE, 0, 0}, {0, 0, 0xFE, 0xFF}};
        const auto& prefix = prefixes[generator() % prefixes.size()];
        bytes.insert(bytes.begin(), prefix.begin(), prefix.end());
        project_describer::EncodingUtils::IsLikelyText(bytes);
        const auto decoded = project_describer::EncodingUtils::DecodeToUtf8(bytes);
#ifdef _WIN32
        if (decoded && !decoded->utf8.empty()) {
            EXPECT_TRUE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, decoded->utf8.data(),
                static_cast<int>(decoded->utf8.size()), nullptr, 0) > 0);
        }
#endif
    }
}
