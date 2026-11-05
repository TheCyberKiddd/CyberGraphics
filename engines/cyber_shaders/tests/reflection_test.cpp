#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

#include <cstdio>

using cyber::core::u32;
using cyber::memory::HeapAllocator;
using cyber::memory::StringView;
using cyber::shaders::Builtin;
using cyber::shaders::CompileHlsl;
using cyber::shaders::DiagnosticKind;
using cyber::shaders::Reflection;
using cyber::shaders::ResourceKind;
using cyber::shaders::Stage;
using cyber::shaders::ValueType;

namespace
{

constexpr char kCubeVertex[] =
    "shader vertex Cube {\n"
    "    input float3 position @location(0);\n"
    "    input float2 uv @location(1);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    output float2 texcoord @location(0);\n"
    "    uniform float4x4 modelViewProjection @register(b0);\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "    output.texcoord = input.uv;\n"
    "}\n";

constexpr char kCubePixel[] = "shader pixel Cube {\n"
                              "    input float4 position @builtin(position);\n"
                              "    input float2 uv @location(0);\n"
                              "    texture2d albedo @register(t0, space0);\n"
                              "    sampler albedoSampler @register(s0, space0);\n"
                              "    return sample(albedo, albedoSampler, input.uv);\n"
                              "}\n";

// Compiles a pixel shader whose only interesting part is a list of uniforms in
// one constant buffer, and returns its reflection.
[[nodiscard]] bool ReflectUniforms(const char* uniforms, Reflection& reflection)
{
    char source[1024]{};
    const int written = std::snprintf(source, sizeof(source),
                                      "shader pixel Packing {\n"
                                      "    input float2 uv @location(0);\n"
                                      "%s"
                                      "    return float4(1.0, 1.0, 1.0, 1.0);\n"
                                      "}\n",
                                      uniforms);
    if (written <= 0)
    {
        return false;
    }
    HeapAllocator allocator;
    auto compiled = CompileHlsl("packing.cgs", StringView(source), allocator);
    if (!compiled)
    {
        return false;
    }
    reflection = compiled.Value().reflection;
    return true;
}

}  // namespace

CG_TEST(shaders_reflection_describes_vertex_io_and_push_block)
{
    HeapAllocator allocator;
    auto compiled = CompileHlsl("cube_vertex.cgs", kCubeVertex, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    const Reflection& reflection = compiled.Value().reflection;
    CG_EXPECT_EQ(reflection.stage, Stage::Vertex);
    CG_REQUIRE(reflection.inputCount == 2);
    CG_EXPECT_EQ(reflection.inputs[0].name.View(), StringView("position"));
    CG_EXPECT_EQ(reflection.inputs[0].type, ValueType::Float3);
    CG_EXPECT_EQ(reflection.inputs[0].location, u32{0});
    CG_EXPECT_EQ(reflection.inputs[1].type, ValueType::Float2);
    CG_EXPECT_EQ(reflection.inputs[1].location, u32{1});
    CG_REQUIRE(reflection.outputCount == 2);
    CG_EXPECT_EQ(reflection.outputs[0].builtin, Builtin::Position);
    CG_EXPECT_EQ(reflection.outputs[1].builtin, Builtin::None);

    CG_REQUIRE(reflection.resourceCount == 1);
    const auto& buffer = reflection.resources[0];
    CG_EXPECT_EQ(buffer.kind, ResourceKind::ConstantBuffer);
    CG_EXPECT_EQ(buffer.name.View(), StringView("Cube_Uniforms0"));
    CG_EXPECT_EQ(buffer.registerIndex, u32{0});
    CG_EXPECT_EQ(buffer.registerSpace, u32{0});
    CG_EXPECT_EQ(buffer.sizeBytes, u32{64});
    CG_REQUIRE(buffer.memberCount == 1);
    CG_EXPECT_EQ(reflection.members[buffer.firstMember].name.View(),
                 StringView("modelViewProjection"));
    CG_EXPECT_EQ(reflection.members[buffer.firstMember].offsetBytes, u32{0});
    CG_EXPECT_EQ(reflection.members[buffer.firstMember].sizeBytes, u32{64});
    CG_EXPECT_EQ(reflection.threadGroupSize[0], u32{0});
}

CG_TEST(shaders_reflection_describes_pixel_resources)
{
    HeapAllocator allocator;
    auto compiled = CompileHlsl("cube_pixel.cgs", kCubePixel, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    const Reflection& reflection = compiled.Value().reflection;
    CG_REQUIRE(reflection.resourceCount == 2);
    CG_EXPECT_EQ(reflection.resources[0].kind, ResourceKind::SampledTexture);
    CG_EXPECT_EQ(reflection.resources[0].name.View(), StringView("albedo"));
    CG_EXPECT_EQ(reflection.resources[1].kind, ResourceKind::Sampler);
    CG_EXPECT_EQ(reflection.resources[1].registerIndex, u32{0});
    CG_EXPECT_EQ(reflection.inputs[0].builtin, Builtin::Position);
    CG_EXPECT_EQ(reflection.memberCount, u32{0});
}

// The cases below are the examples on Microsoft's "Packing rules for constant
// variables" page, plus the matrix rule.
CG_TEST(shaders_reflection_packs_constants_like_hlsl)
{
    Reflection reflection{};
    // float4, float2, float2 -> 2 registers; the second float2 shares c1.
    CG_REQUIRE(ReflectUniforms("    uniform float4 a @register(b1);\n"
                               "    uniform float2 b @register(b1);\n"
                               "    uniform float2 c @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{32});
    CG_EXPECT_EQ(reflection.members[1].offsetBytes, u32{16});
    CG_EXPECT_EQ(reflection.members[2].offsetBytes, u32{24});

    // float2, float4, float2 -> 3 registers.
    CG_REQUIRE(ReflectUniforms("    uniform float2 a @register(b1);\n"
                               "    uniform float4 b @register(b1);\n"
                               "    uniform float2 c @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{48});
    CG_EXPECT_EQ(reflection.members[1].offsetBytes, u32{16});
    CG_EXPECT_EQ(reflection.members[2].offsetBytes, u32{32});

    // float, float2, float -> 1 register.
    CG_REQUIRE(ReflectUniforms("    uniform float a @register(b1);\n"
                               "    uniform float2 b @register(b1);\n"
                               "    uniform float c @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{16});
    CG_EXPECT_EQ(reflection.members[1].offsetBytes, u32{4});
    CG_EXPECT_EQ(reflection.members[2].offsetBytes, u32{12});

    // float, float, float, float2 -> 2 registers; the float2 would straddle.
    CG_REQUIRE(ReflectUniforms("    uniform float a @register(b1);\n"
                               "    uniform float b @register(b1);\n"
                               "    uniform float c @register(b1);\n"
                               "    uniform float2 d @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{32});
    CG_EXPECT_EQ(reflection.members[3].offsetBytes, u32{16});

    // float3, float -> 1 register; float, float, float3 -> 2 registers.
    CG_REQUIRE(ReflectUniforms("    uniform float3 a @register(b1);\n"
                               "    uniform float b @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{16});
    CG_EXPECT_EQ(reflection.members[1].offsetBytes, u32{12});
    CG_REQUIRE(ReflectUniforms("    uniform float a @register(b1);\n"
                               "    uniform float b @register(b1);\n"
                               "    uniform float3 c @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{32});
    CG_EXPECT_EQ(reflection.members[2].offsetBytes, u32{16});

    // A matrix starts a new register even after a lone float.
    CG_REQUIRE(ReflectUniforms("    uniform float a @register(b1);\n"
                               "    uniform float4x4 m @register(b1);\n",
                               reflection));
    CG_EXPECT_EQ(reflection.members[1].offsetBytes, u32{16});
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{80});
}

CG_TEST(shaders_reflection_separates_constant_buffers_by_register)
{
    Reflection reflection{};
    CG_REQUIRE(ReflectUniforms("    uniform float4 a @register(b0);\n"
                               "    uniform float4 b @register(b1, space2);\n"
                               "    uniform float c @register(b0);\n",
                               reflection));
    CG_REQUIRE(reflection.resourceCount == 2);
    CG_EXPECT_EQ(reflection.resources[0].memberCount, u32{2});
    CG_EXPECT_EQ(reflection.resources[0].sizeBytes, u32{32});
    CG_EXPECT_EQ(reflection.resources[1].registerSpace, u32{2});
    CG_EXPECT_EQ(reflection.resources[1].name.View(), StringView("Packing_Uniforms1"));
    CG_EXPECT_EQ(reflection.members[reflection.resources[0].firstMember + 1].name.View(),
                 StringView("c"));
}

CG_TEST(shaders_reflection_emits_matching_packoffsets)
{
    constexpr char source[] = "shader pixel Tint {\n"
                              "    input float2 uv @location(0);\n"
                              "    uniform float a @register(b0);\n"
                              "    uniform float2 b @register(b0);\n"
                              "    uniform float4 tint @register(b0);\n"
                              "    return tint;\n"
                              "}\n";
    constexpr char expected[] = "struct Input\n"
                                "{\n"
                                "    float2 uv : TEXCOORD0;\n"
                                "};\n"
                                "cbuffer Tint_Uniforms0 : register(b0)\n"
                                "{\n"
                                "    float a : packoffset(c0);\n"
                                "    float2 b : packoffset(c0.y);\n"
                                "    float4 tint : packoffset(c1);\n"
                                "};\n"
                                "float4 main(Input input) : SV_Target\n"
                                "{\n"
                                "    return tint;\n"
                                "}\n";
    HeapAllocator allocator;
    auto compiled = CompileHlsl("tint.cgs", source, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    CG_EXPECT_EQ(compiled.Value().hlsl.View(), StringView(expected));
}

CG_TEST(shaders_reflection_compute_thread_groups)
{
    constexpr char source[] = "shader compute Fill @threads(8, 4, 2) {\n"
                              "    input uint3 id @builtin(dispatch_thread_id);\n"
                              "    rwtexture2d target @register(u0, space1);\n"
                              "    uniform float4 color @register(b0);\n"
                              "    store(target, input.id, color);\n"
                              "}\n";
    constexpr char expected[] = "struct Input\n"
                                "{\n"
                                "    uint3 id : SV_DispatchThreadID;\n"
                                "};\n"
                                "RWTexture2D<float4> target : register(u0, space1);\n"
                                "cbuffer Fill_Uniforms0 : register(b0)\n"
                                "{\n"
                                "    float4 color : packoffset(c0);\n"
                                "};\n"
                                "[numthreads(8, 4, 2)]\n"
                                "void main(Input input)\n"
                                "{\n"
                                "    target[input.id.xy] = color;\n"
                                "}\n";
    HeapAllocator allocator;
    auto compiled = CompileHlsl("fill.cgs", source, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    const Reflection& reflection = compiled.Value().reflection;
    CG_EXPECT_EQ(compiled.Value().stage, Stage::Compute);
    CG_EXPECT_EQ(compiled.Value().hlsl.View(), StringView(expected));
    CG_EXPECT_EQ(reflection.threadGroupSize[0], u32{8});
    CG_EXPECT_EQ(reflection.threadGroupSize[1], u32{4});
    CG_EXPECT_EQ(reflection.threadGroupSize[2], u32{2});
    CG_EXPECT_EQ(reflection.inputs[0].builtin, Builtin::DispatchThreadId);
    CG_EXPECT_EQ(reflection.inputs[0].type, ValueType::Uint3);
    CG_REQUIRE(reflection.resourceCount == 2);
    CG_EXPECT_EQ(reflection.resources[0].kind, ResourceKind::StorageTexture);
    CG_EXPECT_EQ(reflection.resources[0].registerSpace, u32{1});
}

CG_TEST(shaders_reflection_rejects_invalid_compute)
{
    const char* const cases[] = {
        // Thread group above 1024 threads.
        "shader compute A @threads(32, 32, 2) { input uint3 id @builtin(dispatch_thread_id); "
        "rwtexture2d t @register(u0); store(t, input.id, float4(1.0, 1.0, 1.0, 1.0)); }",
        // Z above 64.
        "shader compute A @threads(1, 1, 65) { input uint3 id @builtin(dispatch_thread_id); "
        "rwtexture2d t @register(u0); store(t, input.id, float4(1.0, 1.0, 1.0, 1.0)); }",
        // Zero dimension.
        "shader compute A @threads(0, 1, 1) { input uint3 id @builtin(dispatch_thread_id); "
        "rwtexture2d t @register(u0); store(t, input.id, float4(1.0, 1.0, 1.0, 1.0)); }",
        // Missing @threads.
        "shader compute A { input uint3 id @builtin(dispatch_thread_id); "
        "rwtexture2d t @register(u0); store(t, input.id, float4(1.0, 1.0, 1.0, 1.0)); }",
        // @threads on a pixel shader.
        "shader pixel A @threads(1, 1, 1) { input float2 uv @location(0); "
        "return float4(1.0, 1.0, 1.0, 1.0); }",
        // No store.
        "shader compute A @threads(1, 1, 1) { input uint3 id @builtin(dispatch_thread_id); }",
        // Store into a sampled texture.
        "shader compute A @threads(1, 1, 1) { input uint3 id @builtin(dispatch_thread_id); "
        "texture2d t @register(t0); store(t, input.id, float4(1.0, 1.0, 1.0, 1.0)); }",
        // Storage texture outside compute.
        "shader pixel A { input float2 uv @location(0); rwtexture2d t @register(u0); "
        "return float4(1.0, 1.0, 1.0, 1.0); }",
        // uint3 outside a compute builtin.
        "shader pixel A { input uint3 id @location(0); return float4(1.0, 1.0, 1.0, 1.0); }",
    };
    HeapAllocator allocator;
    for (const char* source : cases)
    {
        auto compiled = CompileHlsl("bad.cgs", StringView(source), allocator);
        CG_EXPECT_FALSE(static_cast<bool>(compiled));
        if (!compiled)
        {
            CG_EXPECT_TRUE(compiled.Error().message != nullptr);
            CG_EXPECT_TRUE(compiled.Error().kind == DiagnosticKind::Type ||
                           compiled.Error().kind == DiagnosticKind::Syntax);
        }
    }
    CG_EXPECT_EQ(allocator.Stats().used, cyber::core::usize{0});
}

CG_TEST(shaders_reflection_rejects_long_names)
{
    char source[256]{};
    std::snprintf(source, sizeof(source),
                  "shader pixel A { input float2 %s @location(0); "
                  "return float4(1.0, 1.0, 1.0, 1.0); }",
                  "a234567890123456789012345678901234567890123456789012345678901234");
    HeapAllocator allocator;
    auto compiled = CompileHlsl("long.cgs", StringView(source), allocator);
    CG_REQUIRE(!compiled);
    CG_EXPECT_EQ(compiled.Error().kind, DiagnosticKind::Unsupported);
    CG_EXPECT_EQ(compiled.Error().column, u32{31});
}
