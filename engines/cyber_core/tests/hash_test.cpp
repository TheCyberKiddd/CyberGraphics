#include <cyber/core/hash.h>
#include <cyber/test/test.h>

#include <array>
#include <cstddef>
#include <string>

CG_TEST(hash_empty_input)
{
    constexpr cyber::core::u64 kExpected = 0xEF46DB3751D8E999ULL;
    CG_EXPECT_EQ(cyber::core::HashBytes(nullptr, 0), kExpected);
    CG_EXPECT_EQ(cyber::core::HashString(""), kExpected);
    CG_EXPECT_EQ(cyber::core::HashString(std::string_view{}), kExpected);
}

CG_TEST(hash_canonical_vectors)
{
    CG_EXPECT_EQ(cyber::core::HashString("a"), 0xD24EC4F1A98C6E5BULL);
    CG_EXPECT_EQ(cyber::core::HashString("abc"), 0x44BC2CF5AD770999ULL);
    CG_EXPECT_EQ(cyber::core::HashString("message digest"), 0x066ED728FCEEB3BEULL);
    CG_EXPECT_EQ(cyber::core::HashString("0123456789abcdef0123456789abcdef"),
                 0x642A94958E71E6C5ULL);
    CG_EXPECT_EQ(cyber::core::HashString("The quick brown fox jumps over the lazy dog"),
                 0x0B242D361FDA71BCULL);
}

CG_TEST(hash_seed_vector)
{
    CG_EXPECT_EQ(cyber::core::HashString("abc", 1), 0xBEA9CA8199328908ULL);
    CG_EXPECT_NE(cyber::core::HashString("abc", 0), cyber::core::HashString("abc", 1));
}

CG_TEST(hash_long_input)
{
    const std::string text(1000, 'x');
    CG_EXPECT_EQ(cyber::core::HashString(text), 0x4CB9A3B69CB700E1ULL);

    std::array<std::byte, 1024> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<std::byte>(index & 0xFFU);
    }
    CG_EXPECT_EQ(cyber::core::HashBytes(std::span<const std::byte>(bytes)), 0x6F3914F18FE4DF57ULL);
}

CG_TEST(hash_unaligned_input)
{
    std::array<char, 16> buffer{};
    buffer[1] = 'a';
    buffer[2] = 'b';
    buffer[3] = 'c';
    CG_EXPECT_EQ(cyber::core::HashBytes(buffer.data() + 1, 3), cyber::core::HashString("abc"));
}

CG_TEST(hash_combine)
{
    const auto first = cyber::core::HashCombine(0, 1);
    const auto second = cyber::core::HashCombine(0, 2);
    CG_EXPECT_EQ(first, cyber::core::HashCombine(0, 1));
    CG_EXPECT_NE(first, second);
    CG_EXPECT_NE(cyber::core::HashCombine(1, 0), cyber::core::HashCombine(0, 0));
}
