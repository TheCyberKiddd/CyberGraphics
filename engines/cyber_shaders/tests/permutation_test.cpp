#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/compiler.h>
#include <cyber/shaders/permutation.h>
#include <cyber/test/test.h>

#include <cstdio>

using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;
using cyber::shaders::CompileHlsl;
using cyber::shaders::CompileOptions;
using cyber::shaders::DiagnosticKind;
using cyber::shaders::HashKeywordSet;
using cyber::shaders::kPermutationHashSeed;
using cyber::shaders::ScanKeywords;
using cyber::shaders::Stage;

namespace
{

constexpr char kTinted[] =
    "shader pixel Cube {\n"
    "    keyword TINT;\n"
    "    keyword DEBUG_UV;\n"
    "    input float4 position @builtin(position);\n"
    "    input float2 uv @location(0);\n"
    "    texture2d albedo @register(t0);\n"
    "    sampler albedoSampler @register(s0);\n"
    "    when TINT {\n"
    "        uniform float4 tint @register(b0);\n"
    "    }\n"
    "    when DEBUG_UV {\n"
    "        return float4(input.uv, 0.0, 1.0);\n"
    "    } else {\n"
    "        when TINT {\n"
    "            return mul(sample(albedo, albedoSampler, input.uv), tint);\n"
    "        } else {\n"
    "            return sample(albedo, albedoSampler, input.uv);\n"
    "        }\n"
    "    }\n"
    "}\n";

[[nodiscard]] Span<const StringView> Keywords(const StringView* names, u32 count)
{
    return Span<const StringView>(names, count);
}

}  // namespace

CG_TEST(shaders_permutation_hash_is_order_independent_and_stable)
{
    const StringView ab[] = {"A", "B"};
    const StringView ba[] = {"B", "A"};
    const StringView aab[] = {"A", "B", "A"};
    const StringView a[] = {"A"};
    CG_EXPECT_EQ(HashKeywordSet(Span<const StringView>{}), kPermutationHashSeed);
    CG_EXPECT_EQ(HashKeywordSet(Keywords(ab, 2)), HashKeywordSet(Keywords(ba, 2)));
    CG_EXPECT_EQ(HashKeywordSet(Keywords(ab, 2)), HashKeywordSet(Keywords(aab, 3)));
    CG_EXPECT_NE(HashKeywordSet(Keywords(ab, 2)), HashKeywordSet(Keywords(a, 1)));
    CG_EXPECT_NE(HashKeywordSet(Keywords(a, 1)), kPermutationHashSeed);
    // "AB" alone must not collide with {"A", "B"}: names are hashed one by one.
    const StringView joined[] = {"AB"};
    CG_EXPECT_NE(HashKeywordSet(Keywords(joined, 1)), HashKeywordSet(Keywords(ab, 2)));
}

CG_TEST(shaders_permutation_selects_when_branches)
{
    HeapAllocator allocator;
    {
        auto none = CompileHlsl("cube_pixel.cgs", kTinted, allocator);
        CG_REQUIRE(static_cast<bool>(none));
        CG_EXPECT_EQ(none.Value().permutation.mask, u32{0});
        CG_EXPECT_EQ(none.Value().permutation.hash, kPermutationHashSeed);
        CG_EXPECT_EQ(none.Value().reflection.keywordCount, u32{2});
        CG_EXPECT_EQ(none.Value().reflection.resourceCount, u32{2});
        CG_EXPECT_TRUE(none.Value().hlsl.View().Contains('*') == false);

        const StringView tint[] = {"TINT"};
        CompileOptions options{};
        options.keywords = Keywords(tint, 1);
        auto tinted = CompileHlsl("cube_pixel.cgs", kTinted, options, allocator);
        CG_REQUIRE(static_cast<bool>(tinted));
        CG_EXPECT_EQ(tinted.Value().permutation.mask, u32{1});
        CG_EXPECT_EQ(tinted.Value().permutation.hash, HashKeywordSet(Keywords(tint, 1)));
        CG_EXPECT_EQ(tinted.Value().reflection.resourceCount, u32{3});
        CG_EXPECT_TRUE(tinted.Value().hlsl.View().Contains('*'));

        const StringView both[] = {"DEBUG_UV", "TINT"};
        options.keywords = Keywords(both, 2);
        auto debug = CompileHlsl("cube_pixel.cgs", kTinted, options, allocator);
        CG_REQUIRE(static_cast<bool>(debug));
        CG_EXPECT_EQ(debug.Value().permutation.mask, u32{3});
    }
    CG_EXPECT_EQ(allocator.Stats().used, cyber::core::usize{0});
}

CG_TEST(shaders_permutation_rejects_undeclared_keyword)
{
    HeapAllocator allocator;
    const StringView missing[] = {"FOG"};
    CompileOptions options{};
    options.keywords = Keywords(missing, 1);
    auto result = CompileHlsl("cube_pixel.cgs", kTinted, options, allocator);
    CG_REQUIRE(!result);
    CG_EXPECT_EQ(result.Error().kind, DiagnosticKind::Permutation);
}

CG_TEST(shaders_permutation_reports_syntax_errors_in_inactive_branches)
{
    constexpr char source[] = "shader pixel A {\n"
                              "    keyword OFF;\n"
                              "    input float2 uv @location(0);\n"
                              "    when OFF {\n"
                              "        return float4(1.0, 1.0, 1.0 1.0);\n"
                              "    }\n"
                              "    return float4(0.0, 0.0, 0.0, 1.0);\n"
                              "}\n";
    HeapAllocator allocator;
    auto result = CompileHlsl("inactive.cgs", source, allocator);
    CG_REQUIRE(!result);
    CG_EXPECT_EQ(result.Error().kind, DiagnosticKind::Syntax);
    CG_EXPECT_EQ(result.Error().line, u32{5});
    CG_EXPECT_EQ(result.Error().column, u32{37});
}

CG_TEST(shaders_permutation_inactive_branches_skip_type_checks)
{
    // The disabled branch names an undeclared uniform; only the selected branch
    // is type-checked, so the default permutation compiles.
    constexpr char source[] = "shader pixel A {\n"
                              "    keyword FANCY;\n"
                              "    input float2 uv @location(0);\n"
                              "    when FANCY {\n"
                              "        return missingUniform;\n"
                              "    } else {\n"
                              "        return float4(0.0, 0.0, 0.0, 1.0);\n"
                              "    }\n"
                              "}\n";
    HeapAllocator allocator;
    CG_EXPECT_TRUE(static_cast<bool>(CompileHlsl("lazy.cgs", source, allocator)));
    const StringView fancy[] = {"FANCY"};
    CompileOptions options{};
    options.keywords = Keywords(fancy, 1);
    auto selected = CompileHlsl("lazy.cgs", source, options, allocator);
    CG_REQUIRE(!selected);
    CG_EXPECT_EQ(selected.Error().kind, DiagnosticKind::Type);
    CG_EXPECT_EQ(selected.Error().line, u32{5});
}

CG_TEST(shaders_permutation_validates_keyword_declarations)
{
    const char* const cases[] = {
        "shader pixel A { keyword K; keyword K; input float2 uv @location(0); "
        "return float4(1.0, 1.0, 1.0, 1.0); }",
        "shader pixel A { input float2 uv @location(0); when K { } "
        "return float4(1.0, 1.0, 1.0, 1.0); }",
        "shader pixel A { keyword K; input float2 uv @location(0); when K { keyword J; } "
        "return float4(1.0, 1.0, 1.0, 1.0); }",
        "shader pixel A { keyword K; input float2 uv @location(0); when K "
        "return float4(1.0, 1.0, 1.0, 1.0); }",
    };
    HeapAllocator allocator;
    for (const char* source : cases)
    {
        auto result = CompileHlsl("keywords.cgs", StringView(source), allocator);
        CG_EXPECT_FALSE(static_cast<bool>(result));
    }

    char source[1024]{};
    int offset = std::snprintf(source, sizeof(source), "shader pixel A {\n");
    for (u32 index = 0; index < 9; ++index)
    {
        offset += std::snprintf(source + offset, sizeof(source) - static_cast<size_t>(offset),
                                "    keyword K%u;\n", index);
    }
    std::snprintf(source + offset, sizeof(source) - static_cast<size_t>(offset),
                  "    input float2 uv @location(0);\n"
                  "    return float4(1.0, 1.0, 1.0, 1.0);\n}\n");
    auto tooMany = CompileHlsl("limit.cgs", StringView(source), allocator);
    CG_REQUIRE(!tooMany);
    CG_EXPECT_EQ(tooMany.Error().kind, DiagnosticKind::Unsupported);
    CG_EXPECT_EQ(tooMany.Error().line, u32{10});
}

CG_TEST(shaders_permutation_scans_keywords)
{
    HeapAllocator allocator;
    auto scanned = ScanKeywords("cube_pixel.cgs", kTinted, allocator);
    CG_REQUIRE(static_cast<bool>(scanned));
    CG_EXPECT_EQ(scanned.Value().stage, Stage::Pixel);
    CG_REQUIRE(scanned.Value().count == 2);
    CG_EXPECT_EQ(scanned.Value().names[0].View(), StringView("TINT"));
    CG_EXPECT_EQ(scanned.Value().names[1].View(), StringView("DEBUG_UV"));

    auto broken = ScanKeywords("broken.cgs", "shader pixel A { keyword ; }", allocator);
    CG_REQUIRE(!broken);
    CG_EXPECT_EQ(broken.Error().column, u32{26});
    CG_EXPECT_EQ(allocator.Stats().used, cyber::core::usize{0});
}

CG_TEST(shaders_permutation_every_subset_compiles)
{
    HeapAllocator allocator;
    const StringView names[] = {"TINT", "DEBUG_UV"};
    u64 hashes[4]{};
    for (u32 mask = 0; mask < 4; ++mask)
    {
        StringView enabled[2]{};
        u32 count = 0;
        for (u32 bit = 0; bit < 2; ++bit)
        {
            if ((mask & (1U << bit)) != 0)
            {
                enabled[count++] = names[bit];
            }
        }
        CompileOptions options{};
        options.keywords = Keywords(enabled, count);
        auto compiled = CompileHlsl("cube_pixel.cgs", kTinted, options, allocator);
        CG_REQUIRE(static_cast<bool>(compiled));
        CG_EXPECT_EQ(compiled.Value().permutation.mask, mask);
        hashes[mask] = compiled.Value().permutation.hash;
        for (u32 prior = 0; prior < mask; ++prior)
        {
            CG_EXPECT_NE(hashes[prior], hashes[mask]);
        }
    }
}
