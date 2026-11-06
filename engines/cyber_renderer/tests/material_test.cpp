#include <cyber/jobs/job_system.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/material.h>
#include <cyber/rhi/backend.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;
using cyber::renderer::HotGraphicsPipeline;
using cyber::renderer::HotGraphicsPipelineDesc;
using cyber::renderer::MaterialDefinition;
using cyber::renderer::MaterialDefinitionDesc;
using cyber::renderer::MaterialDrawSortKey;
using cyber::renderer::MaterialInstance;
using cyber::renderer::MaterialParameterDesc;
using cyber::renderer::MaterialQueue;
using cyber::renderer::MaterialValue;
using cyber::renderer::SortMaterialDraws;
using cyber::rhi::ColorAttachmentState;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::Format;
using cyber::shaders::CompiledShader;
using cyber::shaders::CompileHlsl;
using cyber::shaders::Reflection;

namespace
{

constexpr char kVertex[] =
    "shader vertex Material {\n"
    "    input float3 position @location(0);\n"
    "    input float2 uv @location(1);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    output float2 texcoord @location(0);\n"
    "    uniform float4x4 modelViewProjection @register(b0);\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "    output.texcoord = input.uv;\n"
    "}\n";

constexpr char kPixel[] = "shader pixel Material {\n"
                          "    input float4 position @builtin(position);\n"
                          "    uniform float4 baseColorFactor @register(b1, space2);\n"
                          "    return baseColorFactor;\n"
                          "}\n";

constexpr char kPixelTextured[] =
    "shader pixel Material {\n"
    "    input float4 position @builtin(position);\n"
    "    input float2 texcoord @location(0);\n"
    "    uniform float4 baseColorFactor @register(b1, space2);\n"
    "    texture2d baseColorTexture @register(t0, space2);\n"
    "    sampler baseColorSampler @register(s0, space2);\n"
    "    return mul(sample(baseColorTexture, baseColorSampler, input.texcoord), baseColorFactor);\n"
    "}\n";

constexpr char kPixelChangedLayout[] = "shader pixel Material {\n"
                                       "    input float4 position @builtin(position);\n"
                                       "    uniform float4 emissiveFactor @register(b1, space2);\n"
                                       "    uniform float4 baseColorFactor @register(b1, space2);\n"
                                       "    return baseColorFactor;\n"
                                       "}\n";

struct Fixture
{
    HeapAllocator heap;
    Device device;
    HotGraphicsPipeline pipeline;
    ColorAttachmentState colorAttachment{};

    [[nodiscard]] bool Open(const char* pixelSource = kPixel)
    {
        DeviceDesc deviceDesc{};
        deviceDesc.backend = cyber::rhi::NullBackend();
        auto createdDevice = Device::Create(deviceDesc, heap);
        if (!createdDevice)
        {
            return false;
        }
        device = std::move(createdDevice.Value());

        colorAttachment.format = Format::RGBA8Unorm;
        HotGraphicsPipelineDesc desc{};
        desc.device = &device;
        desc.state.colorAttachments = Span<const ColorAttachmentState>(&colorAttachment, 1);
        desc.state.depthStencil.depthTestEnabled = false;
        desc.state.depthStencil.depthWriteEnabled = false;
        auto createdPipeline = HotGraphicsPipeline::Create(desc, heap);
        if (!createdPipeline)
        {
            return false;
        }
        pipeline = std::move(createdPipeline.Value());

        auto vertex = CompileHlsl("material_vertex.cgs", StringView(kVertex), heap);
        auto pixel = CompileHlsl("material_pixel.cgs", StringView(pixelSource), heap);
        if (!vertex || !pixel || !pipeline.Rebuild(vertex.Value(), pixel.Value()))
        {
            return false;
        }
        pipeline.BeginFrame(0);
        return pipeline.HasBuilt();
    }

    [[nodiscard]] bool BuildDefinition(const Reflection& vertex, const Reflection& pixel,
                                       MaterialDefinition& definition)
    {
        MaterialValue defaultValue{};
        defaultValue.type = cyber::shaders::ValueType::Float4;
        defaultValue.components = {1.0F, 1.0F, 1.0F, 1.0F};
        MaterialValue minimum{};
        minimum.type = cyber::shaders::ValueType::Float4;
        minimum.components = {0.0F, 0.0F, 0.0F, 0.0F};
        MaterialValue maximum{};
        maximum.type = cyber::shaders::ValueType::Float4;
        maximum.components = {1.0F, 1.0F, 1.0F, 1.0F};
        const MaterialParameterDesc parameter{"baseColorFactor", defaultValue, minimum, maximum};
        const Reflection* stages[] = {&vertex, &pixel};
        MaterialDefinitionDesc desc{};
        desc.name = "PaintedMetal";
        desc.pipeline = &pipeline;
        desc.stages = Span<const Reflection* const>(stages, 2);
        desc.parameters = Span<const MaterialParameterDesc>(&parameter, 1);
        auto created = MaterialDefinition::Create(desc);
        if (!created)
        {
            return false;
        }
        definition = std::move(created.Value());
        return true;
    }
};

}  // namespace

CG_TEST(renderer_material_definitions_match_reflection_and_create_instances)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    auto vertexCompiled = CompileHlsl("material_vertex.cgs", StringView(kVertex), fixture.heap);
    auto pixelCompiled = CompileHlsl("material_pixel.cgs", StringView(kPixel), fixture.heap);
    CG_REQUIRE(static_cast<bool>(vertexCompiled));
    CG_REQUIRE(static_cast<bool>(pixelCompiled));

    MaterialDefinition definition{};
    CG_REQUIRE(fixture.BuildDefinition(vertexCompiled.Value().reflection,
                                       pixelCompiled.Value().reflection, definition));
    CG_EXPECT_EQ(definition.Name(), StringView("PaintedMetal"));
    CG_EXPECT_EQ(definition.ParameterCount(), u32{1});
    CG_EXPECT_EQ(definition.ParameterBlockSizeBytes(), u32{16});
    CG_EXPECT_EQ(definition.ParameterBlockBinding(), u32{0});
    CG_EXPECT_TRUE(definition.Parameter(0) != nullptr);
    CG_EXPECT_TRUE(definition.Parameter(1) == nullptr);
    CG_EXPECT_TRUE(definition.TextureSlot(0) == nullptr);

    auto instanceResult =
        MaterialInstance::Create(definition, fixture.device, fixture.heap,
                                 Span<const cyber::renderer::MaterialTextureView>{});
    CG_REQUIRE(static_cast<bool>(instanceResult));
    MaterialInstance instance = std::move(instanceResult.Value());
    CG_EXPECT_TRUE(instance.BindGroup().IsValid());
    CG_EXPECT_TRUE(instance.IsDirty());
    CG_EXPECT_EQ(instance.Revision(), u64{1});

    auto initial = instance.GetParameter("baseColorFactor");
    CG_REQUIRE(static_cast<bool>(initial));
    CG_EXPECT_EQ(initial.Value().components[0], 1.0F);

    MaterialValue invalid{};
    invalid.type = cyber::shaders::ValueType::Float4;
    invalid.components = {1.1F, 0.0F, 0.0F, 1.0F};
    auto rejected = instance.SetParameter("baseColorFactor", invalid);
    CG_REQUIRE(!rejected);
    CG_EXPECT_EQ(rejected.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_EQ(instance.Revision(), u64{1});

    MaterialValue changed{};
    changed.type = cyber::shaders::ValueType::Float4;
    changed.components = {0.1F, 0.6F, 0.2F, 1.0F};
    CG_REQUIRE(instance.SetParameter("baseColorFactor", changed));
    CG_EXPECT_EQ(instance.Revision(), u64{2});

    cyber::rhi::CommandListDesc copyListDesc{};
    copyListDesc.queue = cyber::rhi::QueueKind::Copy;
    auto copyListResult = fixture.device.CreateCommandList(copyListDesc);
    CG_REQUIRE(static_cast<bool>(copyListResult));
    cyber::rhi::CommandList copyList = std::move(copyListResult.Value());
    CG_REQUIRE(copyList.Begin());
    auto wrongQueue = instance.RecordUpload(copyList);
    CG_REQUIRE(!wrongQueue);
    CG_EXPECT_EQ(wrongQueue.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_TRUE(instance.IsDirty());
    copyList.Reset();
    copyList = cyber::rhi::CommandList{};

    cyber::rhi::CommandListDesc listDesc{};
    auto listResult = fixture.device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(listResult));
    cyber::rhi::CommandList list = std::move(listResult.Value());
    CG_REQUIRE(list.Begin());
    CG_REQUIRE(instance.RecordUpload(list));
    CG_EXPECT_FALSE(instance.IsDirty());
    CG_REQUIRE(list.End());
    const cyber::rhi::CommandListHandle listHandle = list.Handle();
    const cyber::rhi::SubmitDesc submit{cyber::rhi::QueueKind::Graphics,
                                        Span<const cyber::rhi::CommandListHandle>(&listHandle, 1)};
    CG_REQUIRE(fixture.device.Submit(submit));
    CG_REQUIRE(fixture.device.WaitIdle());
    list = cyber::rhi::CommandList{};

    changed.components = {0.3F, 0.2F, 0.4F, 1.0F};
    CG_REQUIRE(instance.SetParameter("baseColorFactor", changed));
    CG_EXPECT_EQ(instance.Revision(), u64{3});
    auto updateListResult = fixture.device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(updateListResult));
    cyber::rhi::CommandList updateList = std::move(updateListResult.Value());
    CG_REQUIRE(updateList.Begin());
    CG_REQUIRE(instance.RecordUpload(updateList));
    CG_EXPECT_FALSE(instance.IsDirty());
    CG_REQUIRE(updateList.End());
    const cyber::rhi::CommandListHandle updateListHandle = updateList.Handle();
    const cyber::rhi::SubmitDesc updateSubmit{
        cyber::rhi::QueueKind::Graphics,
        Span<const cyber::rhi::CommandListHandle>(&updateListHandle, 1)};
    CG_REQUIRE(fixture.device.Submit(updateSubmit));
    CG_REQUIRE(fixture.device.WaitIdle());
    updateList = cyber::rhi::CommandList{};

    auto reloadedPixel =
        CompileHlsl("material_pixel.cgs", StringView(kPixelChangedLayout), fixture.heap);
    CG_REQUIRE(static_cast<bool>(reloadedPixel));
    CG_REQUIRE(fixture.pipeline.Rebuild(vertexCompiled.Value(), reloadedPixel.Value()));
    fixture.pipeline.BeginFrame(1);
    CG_EXPECT_FALSE(instance.BindGroup().IsValid());
    auto staleUpdate = instance.SetParameter("baseColorFactor", changed);
    CG_REQUIRE(!staleUpdate);
    CG_EXPECT_EQ(staleUpdate.Error().code, ErrorCode::InvalidArgument);

    instance = MaterialInstance{};
    fixture.pipeline = HotGraphicsPipeline{};
    CG_EXPECT_EQ(fixture.device.ReportLeaks(), u32{0});
}

CG_TEST(renderer_material_definition_rejects_missing_or_mismatched_reflection)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    auto vertex = CompileHlsl("material_vertex.cgs", StringView(kVertex), fixture.heap);
    auto pixel = CompileHlsl("material_pixel.cgs", StringView(kPixel), fixture.heap);
    CG_REQUIRE(static_cast<bool>(vertex));
    CG_REQUIRE(static_cast<bool>(pixel));
    const Reflection* stages[] = {&vertex.Value().reflection, &pixel.Value().reflection};
    MaterialDefinitionDesc desc{};
    desc.name = "invalid";
    desc.pipeline = &fixture.pipeline;
    desc.stages = Span<const Reflection* const>(stages, 2);

    auto missing = MaterialDefinition::Create(desc);
    CG_REQUIRE(!missing);
    CG_EXPECT_EQ(missing.Error().code, ErrorCode::InvalidArgument);

    MaterialValue value{};
    value.type = cyber::shaders::ValueType::Float;
    value.components[0] = 0.5F;
    const MaterialParameterDesc wrongType{"baseColorFactor", value, value, value};
    desc.parameters = Span<const MaterialParameterDesc>(&wrongType, 1);
    auto mismatch = MaterialDefinition::Create(desc);
    CG_REQUIRE(!mismatch);
    CG_EXPECT_EQ(mismatch.Error().code, ErrorCode::InvalidArgument);

    MaterialValue nonGltfDefault{};
    nonGltfDefault.type = cyber::shaders::ValueType::Float4;
    nonGltfDefault.components = {0.5F, 1.0F, 1.0F, 1.0F};
    MaterialValue minimum{};
    minimum.type = cyber::shaders::ValueType::Float4;
    minimum.components = {0.0F, 0.0F, 0.0F, 0.0F};
    MaterialValue maximum{};
    maximum.type = cyber::shaders::ValueType::Float4;
    maximum.components = {1.0F, 1.0F, 1.0F, 1.0F};
    const MaterialParameterDesc nonGltfParameter{"baseColorFactor", nonGltfDefault, minimum,
                                                 maximum};
    desc.parameters = Span<const MaterialParameterDesc>(&nonGltfParameter, 1);
    auto invalidDefault = MaterialDefinition::Create(desc);
    CG_REQUIRE(!invalidDefault);
    CG_EXPECT_EQ(invalidDefault.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(renderer_material_texture_slots_are_resolved_and_rebound)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open(kPixelTextured));
    auto vertex = CompileHlsl("material_vertex.cgs", StringView(kVertex), fixture.heap);
    auto pixel = CompileHlsl("material_pixel.cgs", StringView(kPixelTextured), fixture.heap);
    CG_REQUIRE(static_cast<bool>(vertex));
    CG_REQUIRE(static_cast<bool>(pixel));

    MaterialValue defaultValue{};
    defaultValue.type = cyber::shaders::ValueType::Float4;
    defaultValue.components = {1.0F, 1.0F, 1.0F, 1.0F};
    MaterialValue minimum{};
    minimum.type = cyber::shaders::ValueType::Float4;
    minimum.components = {0.0F, 0.0F, 0.0F, 0.0F};
    MaterialValue maximum{};
    maximum.type = cyber::shaders::ValueType::Float4;
    maximum.components = {1.0F, 1.0F, 1.0F, 1.0F};
    const MaterialParameterDesc parameter{"baseColorFactor", defaultValue, minimum, maximum};
    const cyber::renderer::MaterialTextureSlotDesc textureSlot{
        "baseColorTexture", {}, cyber::renderer::MaterialColorSpace::Srgb};
    const Reflection* stages[] = {&vertex.Value().reflection, &pixel.Value().reflection};
    auto sampler = fixture.device.CreateSampler(cyber::rhi::SamplerDesc{});
    CG_REQUIRE(static_cast<bool>(sampler));

    MaterialDefinitionDesc desc{};
    desc.name = "Textured";
    desc.pipeline = &fixture.pipeline;
    desc.defaultSampler = sampler.Value();
    desc.stages = Span<const Reflection* const>(stages, 2);
    desc.parameters = Span<const MaterialParameterDesc>(&parameter, 1);
    desc.textureSlots = Span<const cyber::renderer::MaterialTextureSlotDesc>(&textureSlot, 1);
    auto definitionResult = MaterialDefinition::Create(desc);
    CG_REQUIRE(static_cast<bool>(definitionResult));
    MaterialDefinition definition = std::move(definitionResult.Value());

    cyber::rhi::TextureDesc textureDesc{};
    textureDesc.usage = cyber::rhi::TextureUsage::Sampled;
    textureDesc.width = 1;
    textureDesc.height = 1;
    auto texture = fixture.device.CreateTexture(textureDesc);
    CG_REQUIRE(static_cast<bool>(texture));
    cyber::rhi::TextureViewDesc viewDesc{};
    viewDesc.texture = texture.Value();
    viewDesc.format = Format::RGBA8Unorm;
    auto view = fixture.device.CreateTextureView(viewDesc);
    CG_REQUIRE(static_cast<bool>(view));

    const cyber::renderer::MaterialTextureView initialTexture{"baseColorTexture", view.Value()};
    auto instanceResult = MaterialInstance::Create(
        definition, fixture.device, fixture.heap,
        Span<const cyber::renderer::MaterialTextureView>(&initialTexture, 1));
    CG_REQUIRE(static_cast<bool>(instanceResult));
    MaterialInstance instance = std::move(instanceResult.Value());
    CG_EXPECT_TRUE(instance.BindGroup().IsValid());
    CG_REQUIRE(instance.SetTexture("baseColorTexture", view.Value()));
    CG_EXPECT_EQ(instance.Revision(), u64{2});
    auto missing = instance.SetTexture("notADeclaredTexture", view.Value());
    CG_REQUIRE(!missing);
    CG_EXPECT_EQ(missing.Error().code, ErrorCode::NotFound);

    CG_REQUIRE(fixture.device.WaitIdle());
    instance = MaterialInstance{};
    CG_REQUIRE(fixture.device.DestroyTextureView(view.Value()));
    CG_REQUIRE(fixture.device.DestroyTexture(texture.Value()));
    CG_REQUIRE(fixture.device.DestroySampler(sampler.Value()));
    fixture.pipeline = HotGraphicsPipeline{};
    CG_EXPECT_EQ(fixture.device.ReportLeaks(), u32{0});
}

CG_TEST(renderer_material_draw_sort_is_deterministic_for_all_queues)
{
    std::array<MaterialDrawSortKey, 7> draws{{
        {MaterialQueue::Transparent, 9, 3, 5.0F, 6},
        {MaterialQueue::Opaque, 2, 4, 8.0F, 5},
        {MaterialQueue::Opaque, 1, 8, 3.0F, 4},
        {MaterialQueue::Masked, 1, 7, 2.0F, 3},
        {MaterialQueue::Opaque, 2, 4, 1.0F, 2},
        {MaterialQueue::Transparent, 1, 9, 10.0F, 1},
        {MaterialQueue::Opaque, 2, 4, 1.0F, 7},
    }};
    CG_REQUIRE(SortMaterialDraws(Span<MaterialDrawSortKey>(draws.data(), draws.size())));
    CG_EXPECT_EQ(draws[0].submissionSequence, u64{4});
    CG_EXPECT_EQ(draws[1].submissionSequence, u64{2});
    CG_EXPECT_EQ(draws[2].submissionSequence, u64{7});
    CG_EXPECT_EQ(draws[3].submissionSequence, u64{5});
    CG_EXPECT_EQ(draws[4].queue, MaterialQueue::Masked);
    CG_EXPECT_EQ(draws[5].submissionSequence, u64{1});
    CG_EXPECT_EQ(draws[6].submissionSequence, u64{6});

    std::array<MaterialDrawSortKey, 100> many{};
    for (u32 index = 0; index < many.size(); ++index)
    {
        many[index] = {MaterialQueue::Opaque, 0, 0, static_cast<float>(many.size() - index), index};
    }
    CG_REQUIRE(SortMaterialDraws(Span<MaterialDrawSortKey>(many.data(), many.size())));
    for (u32 index = 1; index < many.size(); ++index)
    {
        CG_EXPECT_TRUE(many[index - 1].viewDepth <= many[index].viewDepth);
    }

    const MaterialDrawSortKey before = many[0];
    many[1].viewDepth = std::numeric_limits<float>::quiet_NaN();
    auto invalid = SortMaterialDraws(Span<MaterialDrawSortKey>(many.data(), many.size()));
    CG_REQUIRE(!invalid);
    CG_EXPECT_EQ(invalid.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_EQ(many[0].submissionSequence, before.submissionSequence);
}
