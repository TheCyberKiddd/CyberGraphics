#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <string_view>

using cyber::memory::HeapAllocator;
using cyber::memory::StringView;
using cyber::shaders::CompileHlsl;
using cyber::shaders::DiagnosticKind;
using cyber::shaders::Stage;

namespace
{

constexpr char kVertexSource[] =
    "shader vertex Cube {\n"
    "    input float3 position @location(0);\n"
    "    input float2 uv @location(1);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    output float2 texcoord @location(0);\n"
    "    uniform float4x4 modelViewProjection @register(b0);\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "    output.texcoord = input.uv;\n"
    "}\n";

constexpr char kExpectedVertex[] =
    "struct Input\n"
    "{\n"
    "    float3 position : TEXCOORD0;\n"
    "    float2 uv : TEXCOORD1;\n"
    "};\n"
    "struct Output\n"
    "{\n"
    "    float4 clipPosition : SV_Position;\n"
    "    float2 texcoord : TEXCOORD0;\n"
    "};\n"
    "cbuffer Cube_Uniforms0 : register(b0)\n"
    "{\n"
    "    float4x4 modelViewProjection : packoffset(c0);\n"
    "};\n"
    "Output main(Input input)\n"
    "{\n"
    "    Output output;\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "    output.texcoord = input.uv;\n"
    "    return output;\n"
    "}\n";

constexpr char kPixelSource[] = "shader pixel Cube {\n"
                                "    input float4 position @builtin(position);\n"
                                "    input float2 uv @location(0);\n"
                                "    texture2d albedo @register(t0, space0);\n"
                                "    sampler albedoSampler @register(s0, space0);\n"
                                "    return sample(albedo, albedoSampler, input.uv);\n"
                                "}\n";

constexpr char kExpectedPixel[] = "struct Input\n"
                                  "{\n"
                                  "    float4 position : SV_Position;\n"
                                  "    float2 uv : TEXCOORD0;\n"
                                  "};\n"
                                  "Texture2D<float4> albedo : register(t0);\n"
                                  "SamplerState albedoSampler : register(s0);\n"
                                  "float4 main(Input input) : SV_Target\n"
                                  "{\n"
                                  "    return albedo.Sample(albedoSampler, input.uv);\n"
                                  "}\n";

}  // namespace

CG_TEST(shaders_compiler_emits_vertex_hlsl)
{
    HeapAllocator allocator;
    {
        auto result = CompileHlsl("cube_vertex.cgs", kVertexSource, allocator);
        CG_REQUIRE(static_cast<bool>(result));
        CG_EXPECT_EQ(result.Value().stage, Stage::Vertex);
        CG_EXPECT_EQ(result.Value().hlsl.View(), StringView(kExpectedVertex));
    }
    CG_EXPECT_EQ(allocator.Stats().used, cyber::core::usize{0});
}

CG_TEST(shaders_compiler_emits_pixel_hlsl)
{
    HeapAllocator allocator;
    auto result = CompileHlsl("cube_pixel.cgs", kPixelSource, allocator);
    CG_REQUIRE(static_cast<bool>(result));
    CG_EXPECT_EQ(result.Value().stage, Stage::Pixel);
    CG_EXPECT_EQ(result.Value().hlsl.View(), StringView(kExpectedPixel));
}

CG_TEST(shaders_compiler_reports_type_error_location)
{
    constexpr char source[] = "shader vertex Bad {\n"
                              "    input float3 position @location(0);\n"
                              "    output float4 clipPosition @builtin(position);\n"
                              "    output.clipPosition = input.position;\n"
                              "}\n";
    HeapAllocator allocator;
    auto result = CompileHlsl("bad.cgs", source, allocator);
    CG_EXPECT_FALSE(static_cast<bool>(result));
    if (!result)
    {
        CG_EXPECT_EQ(result.Error().kind, DiagnosticKind::Type);
        CG_EXPECT_EQ(result.Error().sourceName, StringView("bad.cgs"));
        CG_EXPECT_EQ(result.Error().line, cyber::core::u32{4});
        CG_EXPECT_EQ(result.Error().column, cyber::core::u32{5});
        CG_EXPECT_TRUE(result.Error().message != nullptr);
    }
}

CG_TEST(shaders_compiler_rejects_malformed_input)
{
    constexpr char source[] = "shader vertex Bad { ! }";
    HeapAllocator allocator;
    auto result = CompileHlsl("malformed.cgs", source, allocator);
    CG_EXPECT_FALSE(static_cast<bool>(result));
    if (!result)
    {
        CG_EXPECT_EQ(result.Error().kind, DiagnosticKind::Lexical);
        CG_EXPECT_EQ(result.Error().line, cyber::core::u32{1});
        CG_EXPECT_EQ(result.Error().column, cyber::core::u32{21});
    }
}

CG_TEST(shaders_compiler_handles_deterministic_fuzz_inputs)
{
    constexpr char kAlphabet[] = "shader vertex pixel compute input output float2 float3 float4"
                                 " keyword when else store uint3 rwtexture2d threads // "
                                 "{}()[];,:.@=0123456789 \n";
    HeapAllocator allocator;
    cyber::core::u32 random = 0xC0FFEEU;
    for (cyber::core::u32 sample = 0; sample < 256; ++sample)
    {
        char source[96]{};
        const cyber::core::usize size = sample % sizeof(source);
        for (cyber::core::usize index = 0; index < size; ++index)
        {
            random = random * 1664525U + 1013904223U;
            source[index] = kAlphabet[random % (sizeof(kAlphabet) - 1)];
        }
        auto result = CompileHlsl("fuzz.cgs", StringView(source, size), allocator);
        static_cast<void>(result);
    }
    CG_EXPECT_EQ(allocator.Stats().used, cyber::core::usize{0});
}

// Regression: Phase 16 stored three call arguments, so a four-component
// constructor such as float4(1.0, 0.5, 0.25, 1.0) failed with "function
// argument limit exceeded".
CG_TEST(shaders_compiler_accepts_four_argument_constructors)
{
    constexpr char source[] = "shader pixel Solid {\n"
                              "    input float2 uv @location(0);\n"
                              "    return float4(1.0, 0.5, 0.25, 1.0);\n"
                              "}\n";
    HeapAllocator allocator;
    auto result = CompileHlsl("solid.cgs", source, allocator);
    CG_REQUIRE(static_cast<bool>(result));
    CG_EXPECT_TRUE(result.Value().hlsl.View().ToStd().find("float4(1.0, 0.5, 0.25, 1.0)") !=
                   std::string_view::npos);

    constexpr char fiveArguments[] = "shader pixel Solid {\n"
                                     "    input float2 uv @location(0);\n"
                                     "    return float4(1.0, 0.5, 0.25, 1.0, 2.0);\n"
                                     "}\n";
    auto rejected = CompileHlsl("five.cgs", fiveArguments, allocator);
    CG_REQUIRE(!rejected);
    CG_EXPECT_EQ(rejected.Error().kind, DiagnosticKind::Unsupported);
    CG_EXPECT_EQ(rejected.Error().line, cyber::core::u32{3});
}

// Byte-level mutation of valid sources: deletions, duplications, and flips must
// never crash, read out of bounds, or leak.
CG_TEST(shaders_compiler_survives_mutated_sources)
{
    const char* const seeds[] = {kVertexSource, kPixelSource};
    HeapAllocator allocator;
    cyber::core::u32 random = 0xBADC0DEU;
    for (const char* seed : seeds)
    {
        const StringView original(seed);
        for (cyber::core::u32 round = 0; round < 1024; ++round)
        {
            char mutated[1024]{};
            cyber::core::usize size = 0;
            for (cyber::core::usize index = 0;
                 index < original.Size() && size + 2 < sizeof(mutated); ++index)
            {
                random = random * 1664525U + 1013904223U;
                const cyber::core::u32 action = (random >> 16) % 64;
                if (action == 0)
                {
                    continue;  // delete
                }
                mutated[size++] = action == 1 ? static_cast<char>(random >> 24) : original[index];
                if (action == 2)
                {
                    mutated[size++] = original[index];  // duplicate
                }
            }
            auto result = CompileHlsl("mutated.cgs", StringView(mutated, size), allocator);
            if (!result)
            {
                CG_EXPECT_TRUE(result.Error().message != nullptr);
                CG_EXPECT_TRUE(result.Error().line >= 1 && result.Error().column >= 1);
            }
        }
    }
    CG_EXPECT_EQ(allocator.Stats().used, cyber::core::usize{0});
}

// A declared name that HLSL reserves would make the generated HLSL fail inside
// D3DCompile with no source location; the compiler rejects it where it is
// written instead.
CG_TEST(shaders_compiler_rejects_hlsl_reserved_names)
{
    const char* const names[] = {"matrix",    "linear",    "sample",      "point",    "vector",
                                 "float3",    "half4x4",   "min16float2", "main",     "Input",
                                 "SV_Target", "Texture2D", "cbuffer",     "register", "uint"};
    HeapAllocator allocator;
    for (const char* name : names)
    {
        char source[256]{};
        const int written = std::snprintf(source, sizeof(source),
                                          "shader pixel A {\n"
                                          "    input float2 %s @location(0);\n"
                                          "    return float4(1.0, 1.0, 1.0, 1.0);\n"
                                          "}\n",
                                          name);
        CG_REQUIRE(written > 0);
        auto result = CompileHlsl("reserved.cgs", StringView(source), allocator);
        CG_EXPECT_FALSE(static_cast<bool>(result));
        if (!result)
        {
            CG_EXPECT_EQ(result.Error().line, cyber::core::u32{2});
            CG_EXPECT_EQ(result.Error().column, cyber::core::u32{18});
        }
    }
    // Names that only start like a reserved word are fine.
    const char* const allowed[] = {"matrixScale", "float5", "pointLight", "sampleCount", "mainUv"};
    for (const char* name : allowed)
    {
        char source[256]{};
        std::snprintf(source, sizeof(source),
                      "shader pixel A {\n"
                      "    input float2 %s @location(0);\n"
                      "    return float4(1.0, 1.0, 1.0, 1.0);\n"
                      "}\n",
                      name);
        CG_EXPECT_TRUE(
            static_cast<bool>(CompileHlsl("allowed.cgs", StringView(source), allocator)));
    }
    auto shaderName =
        CompileHlsl("reserved.cgs",
                    "shader pixel matrix { input float2 uv @location(0); return float4(1.0, "
                    "1.0, 1.0, 1.0); }",
                    allocator);
    CG_EXPECT_FALSE(static_cast<bool>(shaderName));
}
