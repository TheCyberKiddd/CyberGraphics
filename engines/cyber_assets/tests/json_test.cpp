#include <cyber/assets/json.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstring>

namespace
{

using cyber::assets::ImportResult;
using cyber::assets::JsonDocument;
using cyber::assets::JsonParseOptions;
using cyber::assets::JsonType;
using cyber::assets::JsonValue;
using cyber::core::ErrorCode;
using cyber::memory::Allocator;
using cyber::memory::HeapAllocator;
using cyber::memory::StringView;

constexpr StringView kMaterialDocument = R"({
  "asset": {"version": "2.0", "generator": "cyber-test"},
  "meshes": [
    {
      "name": "cube",
      "primitives": [
        {"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}
      ]
    }
  ],
  "materials": [{"name": "mat", "pbrMetallicRoughness": {"metallicFactor": 0.25}}],
  "empty": [],
  "flags": [true, false, null],
  "count": 42,
  "ratio": -1.5e-3
})";

CG_TEST(json_parses_typed_values_and_navigates)
{
    HeapAllocator allocator;
    auto parsed = JsonDocument::Parse(kMaterialDocument, allocator);
    CG_REQUIRE(parsed.IsOk());

    const JsonValue root = parsed.Value().Root();
    CG_REQUIRE(root.IsObject());
    CG_EXPECT_EQ(root.Size(), static_cast<cyber::core::usize>(7));

    const JsonValue asset = root.Member("asset");
    CG_REQUIRE(asset.IsObject());
    CG_EXPECT_TRUE(asset.Member("version").IsString());

    StringView version;
    CG_EXPECT_TRUE(asset.Member("version").TryGetString(version));
    CG_EXPECT_EQ(version.Size(), static_cast<cyber::core::usize>(3));
    CG_EXPECT_TRUE(std::memcmp(version.Data(), "2.0", 3) == 0);

    const JsonValue primitive = root.Member("meshes").Element(0).Member("primitives").Element(0);
    CG_EXPECT_EQ(primitive.Size(), static_cast<cyber::core::usize>(3));

    cyber::core::u32 position = 99;
    CG_EXPECT_TRUE(primitive.Member("attributes").Member("POSITION").TryGetU32(position));
    CG_EXPECT_EQ(position, 0u);

    CG_EXPECT_TRUE(root.Member("empty").IsArray());
    CG_EXPECT_EQ(root.Member("empty").Size(), static_cast<cyber::core::usize>(0));
    CG_EXPECT_TRUE(root.Member("flags").Element(2).IsNull());

    const JsonValue count = root.Member("count");
    CG_EXPECT_TRUE(count.IsInteger());
    CG_EXPECT_NEAR(count.AsNumber(), 42.0, 0.0);

    const JsonValue ratio = root.Member("ratio");
    CG_EXPECT_FALSE(ratio.IsInteger());
    CG_EXPECT_NEAR(ratio.AsNumber(), -0.0015, 1.0e-12);

    // Absent and out-of-range lookups are invalid handles, not crashes.
    CG_EXPECT_FALSE(root.Member("missing").IsValid());
    CG_EXPECT_FALSE(root.Member("flags").Element(3).IsValid());
    CG_EXPECT_FALSE(root.Member("count").Member("nested").IsValid());
}

CG_TEST(json_decodes_escapes_and_surrogate_pairs)
{
    HeapAllocator allocator;
    constexpr StringView text =
        R"({"a":"line\nbreak\ttab\"quote\\slash\/","b":"\u00e9","c":"\ud83d\ude00"})";
    auto parsed = JsonDocument::Parse(text, allocator);
    CG_REQUIRE(parsed.IsOk());
    const JsonValue root = parsed.Value().Root();

    StringView value;
    CG_REQUIRE(root.Member("a").TryGetString(value));
    CG_EXPECT_TRUE(std::memcmp(value.Data(), "line\nbreak\ttab\"quote\\slash/", value.Size()) == 0);

    CG_REQUIRE(root.Member("b").TryGetString(value));
    CG_EXPECT_EQ(value.Size(), static_cast<cyber::core::usize>(2));

    CG_REQUIRE(root.Member("c").TryGetString(value));
    CG_EXPECT_EQ(value.Size(), static_cast<cyber::core::usize>(4));
}

CG_TEST(json_rejects_malformed_documents_with_locations)
{
    HeapAllocator allocator;

    struct Case
    {
        StringView text;
        ErrorCode code;
        bool checkLocation;
    };
    const Case cases[] = {
        {"", ErrorCode::ParseError, false},
        {"   ", ErrorCode::ParseError, false},
        {"{} trailing", ErrorCode::ParseError, true},
        {"{\"a\":1,}", ErrorCode::ParseError, true},
        {"[1,]", ErrorCode::ParseError, true},
        {"{\"a\" 1}", ErrorCode::ParseError, true},
        {"{\"a\":1", ErrorCode::ParseError, false},
        {"[1 2]", ErrorCode::ParseError, false},
        {"01", ErrorCode::ParseError, false},
        {"1.", ErrorCode::ParseError, false},
        {"1e", ErrorCode::ParseError, false},
        {"tru", ErrorCode::ParseError, false},
        {"\"unterminated", ErrorCode::ParseError, false},
        {"\"bad\\qescape\"", ErrorCode::ParseError, false},
        {"\"raw\ncontrol\"", ErrorCode::ParseError, false},
        {"\"\xc3\x28\"", ErrorCode::ParseError, false},
        {"\"\\ud800\"", ErrorCode::ParseError, false},
    };

    for (const Case& testCase : cases)
    {
        auto parsed = JsonDocument::Parse(testCase.text, allocator);
        CG_REQUIRE(parsed.IsError());
        CG_EXPECT_EQ(static_cast<int>(parsed.Error().code), static_cast<int>(testCase.code));
        CG_EXPECT_TRUE(parsed.Error().message != nullptr);
        if (testCase.checkLocation)
        {
            CG_EXPECT_TRUE(parsed.Error().line >= 1);
            CG_EXPECT_TRUE(parsed.Error().column >= 1);
        }
    }
}

CG_TEST(json_enforces_depth_and_node_limits)
{
    HeapAllocator allocator;

    // Depth limit: 130 nested arrays exceed the default limit of 128.
    char deep[512] = {};
    constexpr int kDepth = 130;
    for (int index = 0; index < kDepth; ++index)
    {
        deep[index] = '[';
        deep[2 * kDepth - 1 - index] = ']';
    }
    auto tooDeep = JsonDocument::Parse(StringView(deep, 2 * kDepth), allocator);
    CG_REQUIRE(tooDeep.IsError());
    CG_EXPECT_EQ(static_cast<int>(tooDeep.Error().code), static_cast<int>(ErrorCode::ParseError));

    JsonParseOptions smallNodes;
    smallNodes.maxNodes = 3;
    auto tooMany = JsonDocument::Parse("[1,2,3,4]", allocator, smallNodes);
    CG_REQUIRE(tooMany.IsError());
    CG_EXPECT_EQ(static_cast<int>(tooMany.Error().code), static_cast<int>(ErrorCode::Overflow));

    JsonParseOptions shallow;
    shallow.maxDepth = 1;
    auto limited = JsonDocument::Parse("[[1]]", allocator, shallow);
    CG_REQUIRE(limited.IsError());
}

CG_TEST(json_keeps_duplicate_members_and_reports_the_first)
{
    HeapAllocator allocator;
    auto parsed = JsonDocument::Parse(R"({"a":1,"a":2})", allocator);
    CG_REQUIRE(parsed.IsOk());
    const JsonValue root = parsed.Value().Root();
    CG_EXPECT_EQ(root.Size(), static_cast<cyber::core::usize>(2));
    CG_EXPECT_NEAR(root.Member("a").AsNumber(), 1.0, 0.0);
    CG_EXPECT_TRUE(root.MemberName(1).StartsWith(StringView("a")));
}

CG_TEST(json_survives_a_moved_document)
{
    HeapAllocator allocator;
    auto parsed = JsonDocument::Parse(R"({"value":7})", allocator);
    CG_REQUIRE(parsed.IsOk());

    JsonDocument moved(std::move(parsed.Value()));
    CG_EXPECT_NEAR(moved.Root().Member("value").AsNumber(), 7.0, 0.0);
    CG_EXPECT_EQ(moved.NodeCount(), static_cast<cyber::core::usize>(2));
}

CG_TEST(json_skips_a_utf8_byte_order_mark)
{
    HeapAllocator allocator;
    constexpr char text[] = "\xef\xbb\xbf{\"a\":true}";
    auto parsed = JsonDocument::Parse(StringView(text, sizeof(text) - 1), allocator);
    CG_REQUIRE(parsed.IsOk());
    CG_EXPECT_TRUE(parsed.Value().Root().Member("a").AsBool());
}

CG_TEST(json_number_conversion_matches_known_values)
{
    HeapAllocator allocator;
    auto parsed = JsonDocument::Parse(
        R"([0,-0,1.0,1e2,1e-2,123456789012345678,0.1,3.141592653589793,-2.5E+3])", allocator);
    CG_REQUIRE(parsed.IsOk());
    const JsonValue root = parsed.Value().Root();
    CG_REQUIRE(root.IsArray());
    CG_EXPECT_EQ(root.Size(), static_cast<cyber::core::usize>(9));

    CG_EXPECT_NEAR(root.Element(0).AsNumber(), 0.0, 0.0);
    CG_EXPECT_NEAR(root.Element(3).AsNumber(), 100.0, 0.0);
    CG_EXPECT_NEAR(root.Element(4).AsNumber(), 0.01, 1.0e-15);
    CG_EXPECT_NEAR(root.Element(5).AsNumber(), 123456789012345678.0, 32.0);
    CG_EXPECT_NEAR(root.Element(6).AsNumber(), 0.1, 1.0e-17);
    CG_EXPECT_NEAR(root.Element(7).AsNumber(), 3.141592653589793, 1.0e-15);
    CG_EXPECT_NEAR(root.Element(8).AsNumber(), -2500.0, 1.0e-12);
}

}  // namespace
