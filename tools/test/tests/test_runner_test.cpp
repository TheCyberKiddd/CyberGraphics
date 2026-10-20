#include <cyber/test/test.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

CG_TEST(test_runner_parse_defaults)
{
    const std::vector<std::string_view> arguments;
    const auto parsed = cyber::test::ParseOptions(arguments);

    CG_EXPECT_TRUE(parsed.ok);
    CG_EXPECT_TRUE(parsed.error.empty());
    CG_EXPECT_TRUE(parsed.options.filter.empty());
    CG_EXPECT_EQ(parsed.options.seed, std::uint64_t{0});
    CG_EXPECT_EQ(parsed.options.repeat, std::uint32_t{1});
    CG_EXPECT_EQ(parsed.options.format, cyber::test::Options::Format::Console);
    CG_EXPECT_FALSE(parsed.options.list);
    CG_EXPECT_FALSE(parsed.options.help);
    CG_EXPECT_FALSE(parsed.options.shuffle);
}

CG_TEST(test_runner_parse_all_options)
{
    const std::array<std::string_view, 9> arguments{
        "--filter=core", "--seed", "42",        "--repeat=3", "--format",
        "tap",           "--list", "--shuffle", "--help",
    };
    const auto parsed = cyber::test::ParseOptions(std::span<const std::string_view>(arguments));

    CG_EXPECT_TRUE(parsed.ok);
    CG_EXPECT_STREQ(parsed.options.filter.c_str(), "core");
    CG_EXPECT_EQ(parsed.options.seed, std::uint64_t{42});
    CG_EXPECT_EQ(parsed.options.repeat, std::uint32_t{3});
    CG_EXPECT_EQ(parsed.options.format, cyber::test::Options::Format::Tap);
    CG_EXPECT_TRUE(parsed.options.list);
    CG_EXPECT_TRUE(parsed.options.shuffle);
    CG_EXPECT_TRUE(parsed.options.help);
}

CG_TEST(test_runner_parse_errors)
{
    {
        const std::array<std::string_view, 1> arguments{"--filter"};
        const auto parsed = cyber::test::ParseOptions(arguments);
        CG_EXPECT_FALSE(parsed.ok);
        CG_EXPECT_TRUE(!parsed.error.empty());
    }
    {
        const std::array<std::string_view, 2> arguments{"--seed", "not-a-number"};
        const auto parsed = cyber::test::ParseOptions(arguments);
        CG_EXPECT_FALSE(parsed.ok);
        CG_EXPECT_TRUE(!parsed.error.empty());
    }
    {
        const std::array<std::string_view, 1> arguments{"--repeat=0"};
        const auto parsed = cyber::test::ParseOptions(arguments);
        CG_EXPECT_FALSE(parsed.ok);
        CG_EXPECT_TRUE(!parsed.error.empty());
    }
    {
        const std::array<std::string_view, 1> arguments{"--repeat=1001"};
        const auto parsed = cyber::test::ParseOptions(arguments);
        CG_EXPECT_FALSE(parsed.ok);
        CG_EXPECT_TRUE(!parsed.error.empty());
    }
    {
        const std::array<std::string_view, 1> arguments{"--format=xml"};
        const auto parsed = cyber::test::ParseOptions(arguments);
        CG_EXPECT_FALSE(parsed.ok);
        CG_EXPECT_TRUE(!parsed.error.empty());
    }
    {
        const std::array<std::string_view, 1> arguments{"--unknown"};
        const auto parsed = cyber::test::ParseOptions(arguments);
        CG_EXPECT_FALSE(parsed.ok);
        CG_EXPECT_TRUE(!parsed.error.empty());
    }
}

CG_TEST(test_runner_filter_matching)
{
    CG_EXPECT_TRUE(cyber::test::MatchesFilter("core_result", ""));
    CG_EXPECT_TRUE(cyber::test::MatchesFilter("core_result", "result"));
    CG_EXPECT_TRUE(cyber::test::MatchesFilter("core_result", "core"));
    CG_EXPECT_FALSE(cyber::test::MatchesFilter("core_result", "math"));
    CG_EXPECT_FALSE(cyber::test::MatchesFilter("core_result", "Result"));
}

CG_TEST(test_runner_xml_escape)
{
    CG_EXPECT_STREQ(cyber::test::XmlEscape("a&b<c>d\"e'f").c_str(),
                    "a&amp;b&lt;c&gt;d&quot;e&apos;f");
    CG_EXPECT_STREQ(cyber::test::XmlEscape("plain text").c_str(), "plain text");
    CG_EXPECT_STREQ(cyber::test::XmlEscape("").c_str(), "");
}

CG_TEST(test_runner_registry)
{
    const std::size_t count = cyber::test::TestCount();
    CG_EXPECT_TRUE(count > 0);
    const cyber::test::TestCase* test = cyber::test::TestAt(0);
    CG_REQUIRE(test != nullptr);
    CG_EXPECT_TRUE(test->name != nullptr);
    CG_EXPECT_TRUE(test->file != nullptr);
    CG_EXPECT_TRUE(test->function != nullptr);
    CG_EXPECT_EQ(cyber::test::TestAt(count), nullptr);
}

CG_TEST(test_runner_cstring_equals)
{
    CG_EXPECT_TRUE(cyber::test::CStringEquals(nullptr, nullptr));
    CG_EXPECT_FALSE(cyber::test::CStringEquals(nullptr, "text"));
    CG_EXPECT_FALSE(cyber::test::CStringEquals("text", nullptr));
    CG_EXPECT_TRUE(cyber::test::CStringEquals("text", "text"));
    CG_EXPECT_FALSE(cyber::test::CStringEquals("text", "other"));
}
