#include "test_framework.hpp"

#include <cstdint>
#include <vector>

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
