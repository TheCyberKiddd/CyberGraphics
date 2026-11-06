#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/reflected_layout.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

#include <utility>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;
using cyber::renderer::BuildReflectedLayout;
using cyber::renderer::BuildVertexLayout;
using cyber::renderer::CreateReflectedLayoutObjects;
using cyber::renderer::DestroyReflectedLayoutObjects;
using cyber::renderer::kNoBinding;
using cyber::renderer::ReflectedLayout;
using cyber::rhi::BindGroupSlot;
using cyber::rhi::BindingType;
using cyber::rhi::Format;
using cyber::rhi::ShaderStage;
using cyber::rhi::ShaderStageBit;
using cyber::shaders::CompiledShader;
using cyber::shaders::CompileHlsl;
using cyber::shaders::Reflection;

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

[[nodiscard]] bool Compile(const char* source, HeapAllocator& allocator, Reflection& out)
{
    auto compiled = CompileHlsl("test.cgs", StringView(source), allocator);
    if (!compiled)
    {
        return false;
    }
    out = compiled.Value().reflection;
    return true;
}

[[nodiscard]] cyber::core::Result<ReflectedLayout, cyber::core::Error> Build(const Reflection& a,
                                                                             const Reflection& b)
{
    const Reflection* stages[] = {&a, &b};
    return BuildReflectedLayout(Span<const Reflection* const>(stages, 2));
}

}  // namespace

CG_TEST(renderer_reflected_layout_matches_the_hand_written_cube_layout)
{
    HeapAllocator allocator;
    Reflection vertex{};
    Reflection pixel{};
    CG_REQUIRE(Compile(kCubeVertex, allocator, vertex));
    CG_REQUIRE(Compile(kCubePixel, allocator, pixel));
    auto layout = Build(vertex, pixel);
    CG_REQUIRE(static_cast<bool>(layout));
    const ReflectedLayout& value = layout.Value();

    // The Phase 14 cube declared exactly this by hand: texture at binding 0 and
    // sampler at binding 1 of PerFrame, visible to the pixel stage, and a
    // 64-byte vertex push-constant range.
    CG_REQUIRE(value.bindingCounts[0] == 2);
    CG_EXPECT_EQ(value.slots[0][0].entry.binding, u32{0});
    CG_EXPECT_EQ(value.slots[0][0].entry.type, BindingType::SampledTexture);
    CG_EXPECT_EQ(value.slots[0][0].entry.visibility, ShaderStageBit(ShaderStage::Pixel));
    CG_EXPECT_EQ(value.slots[0][1].entry.binding, u32{1});
    CG_EXPECT_EQ(value.slots[0][1].entry.type, BindingType::Sampler);
    CG_EXPECT_EQ(value.pushConstants.offsetBytes, u32{0});
    CG_EXPECT_EQ(value.pushConstants.sizeBytes, u32{64});
    CG_EXPECT_EQ(value.pushConstants.stages, ShaderStageBit(ShaderStage::Vertex));
    CG_EXPECT_EQ(value.bindingCounts[1], u32{0});
    CG_EXPECT_TRUE(value.UsesSlot(BindGroupSlot::PerFrame));
    CG_EXPECT_FALSE(value.UsesSlot(BindGroupSlot::PerDraw));
    CG_EXPECT_EQ(value.FindBinding(BindGroupSlot::PerFrame, "albedoSampler"), u32{1});
    CG_EXPECT_EQ(value.FindBinding(BindGroupSlot::PerFrame, "missing"), kNoBinding);
    CG_EXPECT_TRUE(value == Build(vertex, pixel).Value());

    auto vertexLayout = BuildVertexLayout(vertex);
    CG_REQUIRE(static_cast<bool>(vertexLayout));
    CG_EXPECT_EQ(vertexLayout.Value().binding.strideBytes, u32{20});
    CG_REQUIRE(vertexLayout.Value().attributeCount == 2);
    CG_EXPECT_EQ(vertexLayout.Value().attributes[0].format, Format::R32G32B32Float);
    CG_EXPECT_EQ(vertexLayout.Value().attributes[1].format, Format::RG32Float);
    CG_EXPECT_EQ(vertexLayout.Value().attributes[1].offsetBytes, u32{12});
    CG_EXPECT_FALSE(static_cast<bool>(BuildVertexLayout(pixel)));
}

CG_TEST(renderer_reflected_layout_merges_stage_visibility_and_orders_registers)
{
    constexpr char vertexSource[] =
        "shader vertex V {\n"
        "    input float3 position @location(2);\n"
        "    input float2 uv @location(0);\n"
        "    output float4 clipPosition @builtin(position);\n"
        "    output float2 texcoord @location(0);\n"
        "    texture2d heights @register(t1, space2);\n"
        "    uniform float4x4 transform @register(b0);\n"
        "    output.clipPosition = mul(transform, float4(input.position, 1.0));\n"
        "    output.texcoord = input.uv;\n"
        "}\n";
    constexpr char pixelSource[] =
        "shader pixel P {\n"
        "    input float2 uv @location(0);\n"
        "    sampler linearSampler @register(s0, space2);\n"
        "    texture2d heights @register(t1, space2);\n"
        "    texture2d albedo @register(t0, space2);\n"
        "    uniform float4 tint @register(b0);\n"
        "    return mul(sample(albedo, linearSampler, input.uv), tint);\n"
        "}\n";
    HeapAllocator allocator;
    Reflection vertex{};
    Reflection pixel{};
    CG_REQUIRE(Compile(vertexSource, allocator, vertex));
    CG_REQUIRE(Compile(pixelSource, allocator, pixel));
    auto layout = Build(vertex, pixel);
    CG_REQUIRE(static_cast<bool>(layout));
    const auto& slot = layout.Value().slots[2];
    CG_REQUIRE(layout.Value().bindingCounts[2] == 3);
    CG_EXPECT_EQ(slot[0].name.View(), StringView("albedo"));
    CG_EXPECT_EQ(slot[0].entry.visibility, ShaderStageBit(ShaderStage::Pixel));
    CG_EXPECT_EQ(slot[1].name.View(), StringView("heights"));
    CG_EXPECT_EQ(slot[1].entry.visibility,
                 ShaderStageBit(ShaderStage::Vertex) | ShaderStageBit(ShaderStage::Pixel));
    CG_EXPECT_EQ(slot[2].entry.type, BindingType::Sampler);
    CG_EXPECT_EQ(layout.Value().pushConstants.sizeBytes, u32{64});
    CG_EXPECT_EQ(layout.Value().pushConstants.stages,
                 ShaderStageBit(ShaderStage::Vertex) | ShaderStageBit(ShaderStage::Pixel));

    // Vertex attributes follow locations, not declaration order.
    auto vertexLayout = BuildVertexLayout(vertex);
    CG_REQUIRE(static_cast<bool>(vertexLayout));
    CG_EXPECT_EQ(vertexLayout.Value().attributes[0].location, u32{0});
    CG_EXPECT_EQ(vertexLayout.Value().attributes[1].location, u32{2});
    CG_EXPECT_EQ(vertexLayout.Value().attributes[1].offsetBytes, u32{8});
}

CG_TEST(renderer_reflected_layout_rejects_unsupported_bindings)
{
    const char* const pixels[] = {
        // Register gap: t1 without t0.
        "shader pixel P { input float2 uv @location(0); texture2d a @register(t1); "
        "sampler s @register(s0); return sample(a, s, input.uv); }",
        // Constant buffer other than b0/space0.
        "shader pixel P { input float2 uv @location(0); uniform float4 c @register(b1); "
        "return c; }",
        // Push block over 128 bytes.
        "shader pixel P { input float2 uv @location(0); uniform float4x4 a @register(b0); "
        "uniform float4x4 b @register(b0); uniform float4 c @register(b0); return c; }",
        // Space beyond the four slots.
        "shader pixel P { input float2 uv @location(0); texture2d a @register(t0, space4); "
        "sampler s @register(s0, space4); return sample(a, s, input.uv); }",
    };
    HeapAllocator allocator;
    Reflection vertex{};
    CG_REQUIRE(Compile(kCubeVertex, allocator, vertex));
    for (const char* source : pixels)
    {
        Reflection pixel{};
        CG_REQUIRE(Compile(source, allocator, pixel));
        auto layout = Build(vertex, pixel);
        CG_REQUIRE(!layout);
        CG_EXPECT_EQ(layout.Error().code, ErrorCode::Unsupported);
    }

    // The same stage twice is a caller error.
    auto twice = Build(vertex, vertex);
    CG_REQUIRE(!twice);
    CG_EXPECT_EQ(twice.Error().code, ErrorCode::InvalidArgument);

    // A storage texture is rejected until UAV ranges exist.
    constexpr char compute[] = "shader compute C @threads(8, 8, 1) {\n"
                               "    input uint3 id @builtin(dispatch_thread_id);\n"
                               "    rwtexture2d target @register(u0);\n"
                               "    store(target, input.id, float4(1.0, 0.0, 0.0, 1.0));\n"
                               "}\n";
    Reflection computeReflection{};
    CG_REQUIRE(Compile(compute, allocator, computeReflection));
    const Reflection* stages[] = {&computeReflection};
    auto storage = BuildReflectedLayout(Span<const Reflection* const>(stages, 1));
    CG_REQUIRE(!storage);
    CG_EXPECT_EQ(storage.Error().code, ErrorCode::Unsupported);
}

CG_TEST(renderer_reflected_layout_maps_material_constants_to_per_material)
{
    constexpr char pixelSource[] = "shader pixel Material {\n"
                                   "    input float4 position @builtin(position);\n"
                                   "    uniform float4 baseColorFactor @register(b1, space2);\n"
                                   "    return baseColorFactor;\n"
                                   "}\n";
    HeapAllocator allocator;
    Reflection pixel{};
    CG_REQUIRE(Compile(pixelSource, allocator, pixel));
    const Reflection* stages[] = {&pixel};
    auto layout = BuildReflectedLayout(Span<const Reflection* const>(stages, 1));
    CG_REQUIRE(static_cast<bool>(layout));
    const ReflectedLayout& value = layout.Value();

    CG_EXPECT_EQ(value.materialConstantsBinding, u32{0});
    CG_EXPECT_EQ(value.materialConstantsSizeBytes, u32{16});
    CG_EXPECT_EQ(value.bindingCounts[2], u32{1});
    CG_EXPECT_EQ(value.slots[2][0].entry.type, BindingType::UniformBuffer);
    CG_EXPECT_EQ(value.slots[2][0].entry.shaderRegister, u32{1});
    CG_EXPECT_EQ(value.slots[2][0].registerIndex, u32{1});

    constexpr char renamedSource[] = "shader pixel Material {\n"
                                     "    input float4 position @builtin(position);\n"
                                     "    uniform float4 tintFactor @register(b1, space2);\n"
                                     "    return tintFactor;\n"
                                     "}\n";
    Reflection renamed{};
    CG_REQUIRE(Compile(renamedSource, allocator, renamed));
    const Reflection* renamedStage[] = {&renamed};
    auto renamedLayout = BuildReflectedLayout(Span<const Reflection* const>(renamedStage, 1));
    CG_REQUIRE(static_cast<bool>(renamedLayout));
    CG_EXPECT_FALSE(value == renamedLayout.Value());
}

CG_TEST(renderer_reflected_layout_creates_device_objects)
{
    HeapAllocator heap;
    cyber::rhi::DeviceDesc deviceDesc{};
    deviceDesc.backend = cyber::rhi::NullBackend();
    auto device = cyber::rhi::Device::Create(deviceDesc, heap);
    CG_REQUIRE(static_cast<bool>(device));

    Reflection vertex{};
    Reflection pixel{};
    CG_REQUIRE(Compile(kCubeVertex, heap, vertex));
    CG_REQUIRE(Compile(kCubePixel, heap, pixel));
    auto layout = Build(vertex, pixel);
    CG_REQUIRE(static_cast<bool>(layout));
    auto objects = CreateReflectedLayoutObjects(device.Value(), layout.Value(), "cube");
    CG_REQUIRE(static_cast<bool>(objects));
    CG_EXPECT_TRUE(objects.Value().groups[0].IsValid());
    CG_EXPECT_FALSE(objects.Value().groups[1].IsValid());
    CG_EXPECT_TRUE(objects.Value().pipelineLayout.IsValid());
    DestroyReflectedLayoutObjects(device.Value(), objects.Value());
    CG_EXPECT_FALSE(objects.Value().pipelineLayout.IsValid());
    CG_EXPECT_EQ(device.Value().ReportLeaks(), u32{0});
}
