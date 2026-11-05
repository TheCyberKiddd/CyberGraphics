#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

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
    "    float4x4 modelViewProjection;\n"
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
    constexpr char kAlphabet[] = "shader vertex pixel input output float2 float3 float4"
                                 "{}()[];,:.@=0123456789 \n";
    HeapAllocator allocator;
    cyber::core::u32 random = 0xC0FFEEU;
    for (cyber::core::u32 sample = 0; sample < 256; ++sample)
    {
        char source[64]{};
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
