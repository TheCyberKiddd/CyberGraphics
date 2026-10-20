#include <cyber/core/assert.h>
#include <cyber/test/test.h>

#include <cstring>

CG_TEST(assert_format_with_message)
{
    char buffer[512];
    const auto needed = cyber::core::FormatAssertionMessage(
        buffer, sizeof(buffer), "value != 0", "src/example.cpp", 42, "value must be positive");

    CG_EXPECT_TRUE(needed != cyber::core::usize{0});
    CG_EXPECT_TRUE(std::strstr(buffer, "Assertion failed: value != 0") != nullptr);
    CG_EXPECT_TRUE(std::strstr(buffer, "src/example.cpp:42") != nullptr);
    CG_EXPECT_TRUE(std::strstr(buffer, "value must be positive") != nullptr);
}

CG_TEST(assert_format_without_message)
{
    char buffer[512];
    const auto needed = cyber::core::FormatAssertionMessage(
        buffer, sizeof(buffer), "pointer != nullptr", "src/example.cpp", 7, nullptr);

    CG_EXPECT_TRUE(needed != cyber::core::usize{0});
    CG_EXPECT_TRUE(std::strstr(buffer, "pointer != nullptr") != nullptr);
    CG_EXPECT_TRUE(std::strstr(buffer, "src/example.cpp:7") != nullptr);
    CG_EXPECT_TRUE(std::strstr(buffer, "message:") == nullptr);
}

CG_TEST(assert_format_null_fields)
{
    char buffer[512];
    const auto needed =
        cyber::core::FormatAssertionMessage(buffer, sizeof(buffer), nullptr, nullptr, 1, nullptr);

    CG_EXPECT_TRUE(needed != cyber::core::usize{0});
    CG_EXPECT_TRUE(std::strstr(buffer, "<unknown>") != nullptr);
}

CG_TEST(assert_format_truncates_safely)
{
    char buffer[16];
    const auto needed = cyber::core::FormatAssertionMessage(
        buffer, sizeof(buffer), "a_very_long_expression_name", "a/very/long/path/to/example.cpp",
        123, "a long message");

    CG_EXPECT_TRUE(needed >= sizeof(buffer));
    CG_EXPECT_EQ(buffer[sizeof(buffer) - 1], '\0');
    CG_EXPECT_EQ(std::strlen(buffer), sizeof(buffer) - 1);
}

CG_TEST(assert_format_zero_capacity)
{
    char buffer[4] = {'a', 'b', 'c', '\0'};
    const auto needed =
        cyber::core::FormatAssertionMessage(buffer, 0, "expression", "file.cpp", 1, nullptr);
    CG_EXPECT_EQ(needed, cyber::core::usize{0});
    CG_EXPECT_STREQ(buffer, "abc");
}

CG_TEST(assert_success_and_verify_side_effect)
{
    CG_ASSERT(true);
    int callCount = 0;
    CG_VERIFY(++callCount == 1);
    CG_EXPECT_EQ(callCount, 1);
}
