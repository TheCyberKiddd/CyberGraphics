#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace cyber::test
{

struct Options
{
    enum class Format : std::uint8_t
    {
        Console,
        Tap,
        Junit,
    };

    std::string filter;
    std::uint64_t seed = 0;
    std::uint32_t repeat = 1;
    Format format = Format::Console;
    bool list = false;
    bool help = false;
    bool shuffle = false;
};

struct ParsedOptions
{
    Options options;
    std::string error;
    bool ok = true;
};

struct TestCase
{
    const char* name = nullptr;
    const char* file = nullptr;
    int line = 0;
    void (*function)() = nullptr;
};

using TestFunction = void (*)();

class Registrar
{
public:
    Registrar(const char* name, const char* file, int line, TestFunction function);
};

void RegisterTest(const char* name, const char* file, int line, TestFunction function);
[[nodiscard]] std::size_t TestCount() noexcept;
[[nodiscard]] const TestCase* TestAt(std::size_t index) noexcept;

[[nodiscard]] ParsedOptions ParseOptions(std::span<const std::string_view> arguments);
[[nodiscard]] bool MatchesFilter(std::string_view name, std::string_view filter) noexcept;
[[nodiscard]] std::string XmlEscape(std::string_view text);
[[nodiscard]] std::uint64_t Seed() noexcept;

void Expect(bool condition, std::string_view expression, const char* file, int line);
void Fail(std::string_view message, const char* file, int line);
void Require(bool condition, std::string_view expression, const char* file, int line);

int RunAll(const Options& options);
void PrintUsage();

inline bool CStringEquals(const char* lhs, const char* rhs) noexcept
{
    if (lhs == rhs)
    {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr)
    {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

}  // namespace cyber::test

#define CG_TEST(name)                                                                              \
    static void name();                                                                            \
    static const ::cyber::test::Registrar name##_registrar(#name, __FILE__, __LINE__, &name);      \
    static void name()

#define CG_EXPECT_TRUE(expression)                                                                 \
    ::cyber::test::Expect(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define CG_EXPECT_FALSE(expression)                                                                \
    ::cyber::test::Expect(!static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define CG_EXPECT_EQ(actual, expected)                                                             \
    do                                                                                             \
    {                                                                                              \
        const auto& cgActual = (actual);                                                           \
        const auto& cgExpected = (expected);                                                       \
        ::cyber::test::Expect(cgActual == cgExpected, #actual " == " #expected, __FILE__,          \
                              __LINE__);                                                           \
    } while (false)

#define CG_EXPECT_NE(actual, expected)                                                             \
    do                                                                                             \
    {                                                                                              \
        const auto& cgActual = (actual);                                                           \
        const auto& cgExpected = (expected);                                                       \
        ::cyber::test::Expect(cgActual != cgExpected, #actual " != " #expected, __FILE__,          \
                              __LINE__);                                                           \
    } while (false)

#define CG_EXPECT_STREQ(actual, expected)                                                          \
    ::cyber::test::Expect(::cyber::test::CStringEquals((actual), (expected)),                      \
                          #actual " == " #expected, __FILE__, __LINE__)

#define CG_FAIL(message) ::cyber::test::Fail((message), __FILE__, __LINE__)

#define CG_REQUIRE(expression)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if (!static_cast<bool>(expression))                                                        \
        {                                                                                          \
            ::cyber::test::Require(false, #expression, __FILE__, __LINE__);                        \
            return;                                                                                \
        }                                                                                          \
    } while (false)
