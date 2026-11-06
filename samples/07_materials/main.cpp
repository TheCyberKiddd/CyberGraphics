#include <cyber/core/error.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/filesystem.h>
#include <cyber/platform/platform.h>
#include <cyber/platform/window.h>
#include <cyber/renderer/material.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/swapchain.h>
#include <cyber/shaders/compiler.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

using cyber::core::Error;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;
using cyber::platform::Key;
using cyber::platform::Window;
using cyber::platform::WindowDesc;
using cyber::platform::WindowEvent;
using cyber::platform::WindowEventType;
using cyber::renderer::HotGraphicsPipeline;
using cyber::renderer::HotGraphicsPipelineDesc;
using cyber::renderer::MaterialDefinition;
using cyber::renderer::MaterialDefinitionDesc;
using cyber::renderer::MaterialDrawSortKey;
using cyber::renderer::MaterialInstance;
using cyber::renderer::MaterialParameterDesc;
using cyber::renderer::MaterialQueue;
using cyber::renderer::MaterialRenderState;
using cyber::renderer::MaterialValue;
using cyber::renderer::SortMaterialDraws;
using cyber::rhi::Backend;
using cyber::rhi::BufferBarrier;
using cyber::rhi::BufferCopy;
using cyber::rhi::BufferDesc;
using cyber::rhi::BufferHandle;
using cyber::rhi::BufferUsage;
using cyber::rhi::ClearColorValue;
using cyber::rhi::ColorAttachment;
using cyber::rhi::ColorAttachmentState;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListDesc;
using cyber::rhi::CommandListHandle;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::Format;
using cyber::rhi::LoadOp;
using cyber::rhi::PresentMode;
using cyber::rhi::QueueKind;
using cyber::rhi::RenderPassDesc;
using cyber::rhi::ResourceState;
using cyber::rhi::ScissorRect;
using cyber::rhi::StoreOp;
using cyber::rhi::SubmitDesc;
using cyber::rhi::SubresourceRange;
using cyber::rhi::SwapChain;
using cyber::rhi::SwapChainDesc;
using cyber::rhi::TextureBarrier;
using cyber::rhi::TextureHandle;
using cyber::rhi::Viewport;
using cyber::shaders::CompiledShader;
using cyber::shaders::CompileHlsl;
using cyber::shaders::Reflection;

namespace
{

constexpr u32 kMaterialCount = 100;
constexpr u32 kLiveMaterialIndex = 50;
constexpr u32 kOpaqueEnd = 70;
constexpr u32 kMaskedEnd = 85;

constexpr char kVertexSource[] =
    "shader vertex MaterialGrid {\n"
    "    input float2 position @location(0);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    uniform float4x4 objectToClip @register(b0);\n"
    "    output.clipPosition = mul(objectToClip, float4(input.position, 0.0, 1.0));\n"
    "}\n";

constexpr char kPixelSource[] = "shader pixel MaterialGrid {\n"
                                "    input float4 position @builtin(position);\n"
                                "    uniform float4 baseColorFactor @register(b1, space2);\n"
                                "    return baseColorFactor;\n"
                                "}\n";

constexpr float kQuadVertices[] = {
    -1.0F, -1.0F, 1.0F, -1.0F, 1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, -1.0F, 1.0F,
};

void PrintFailure(const char* operation, const Error& error) noexcept
{
    std::printf("%s failed: %s (%s)\n", operation, cyber::core::ErrorCodeName(error.code),
                error.message != nullptr ? error.message : "no diagnostic");
}

[[nodiscard]] bool SetMaterialParameter(MaterialInstance& instance,
                                        const MaterialValue& value) noexcept
{
    auto result = instance.SetParameter("baseColorFactor", value);
    if (!result)
    {
        PrintFailure("SetParameter", result.Error());
        return false;
    }
    return true;
}

[[nodiscard]] u32 PipelineIndex(MaterialQueue queue) noexcept
{
    return static_cast<u32>(queue);
}

[[nodiscard]] const char* QueueName(MaterialQueue queue) noexcept
{
    switch (queue)
    {
    case MaterialQueue::Opaque:
        return "opaque";
    case MaterialQueue::Masked:
        return "masked";
    case MaterialQueue::Transparent:
        return "transparent";
    case MaterialQueue::Count:
        break;
    }
    return "invalid";
}

[[nodiscard]] bool UploadVertices(Device& device, BufferHandle& outBuffer) noexcept
{
    BufferDesc vertexDesc{};
    vertexDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    vertexDesc.sizeBytes = sizeof(kQuadVertices);
    vertexDesc.label = "material sample quad vertices";
    auto vertices = device.CreateBuffer(vertexDesc);
    if (!vertices)
    {
        PrintFailure("CreateBuffer vertices", vertices.Error());
        return false;
    }
    outBuffer = vertices.Value();

    BufferDesc stagingDesc{};
    stagingDesc.usage = BufferUsage::HostVisible | BufferUsage::CopySource;
    stagingDesc.sizeBytes = sizeof(kQuadVertices);
    stagingDesc.label = "material sample vertex staging";
    auto staging = device.CreateBuffer(stagingDesc);
    if (!staging)
    {
        PrintFailure("CreateBuffer staging", staging.Error());
        return false;
    }
    const BufferHandle stagingBuffer = staging.Value();
    const auto write = device.WriteBuffer(
        stagingBuffer, 0,
        Span<const Byte>(reinterpret_cast<const Byte*>(kQuadVertices), sizeof(kQuadVertices)));
    if (!write)
    {
        PrintFailure("WriteBuffer", write.Error());
        return false;
    }

    CommandListDesc listDesc{};
    listDesc.label = "material sample vertex upload";
    auto createdList = device.CreateCommandList(listDesc);
    if (!createdList)
    {
        PrintFailure("CreateCommandList vertex upload", createdList.Error());
        return false;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin())
    {
        std::printf("Could not begin vertex upload command list.\n");
        return false;
    }
    const BufferBarrier barriers[] = {
        {stagingBuffer, ResourceState::Undefined, ResourceState::CopySource},
        {outBuffer, ResourceState::Undefined, ResourceState::CopyDestination},
    };
    if (auto transitioned = list.Barrier(Span<const BufferBarrier>(barriers, 2)); !transitioned)
    {
        PrintFailure("Barrier vertex upload", transitioned.Error());
        return false;
    }
    const BufferCopy copy{stagingBuffer, 0, outBuffer, 0, sizeof(kQuadVertices)};
    if (auto copied = list.CopyBuffer(copy); !copied)
    {
        PrintFailure("CopyBuffer vertices", copied.Error());
        return false;
    }
    const BufferBarrier ready{outBuffer, ResourceState::CopyDestination,
                              ResourceState::VertexBuffer};
    if (auto transitioned = list.Barrier(Span<const BufferBarrier>(&ready, 1)); !transitioned)
    {
        PrintFailure("Barrier vertex buffer", transitioned.Error());
        return false;
    }
    if (!list.End())
    {
        std::printf("Could not finish vertex upload command list.\n");
        return false;
    }
    const CommandListHandle handle = list.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(&handle, 1);
    submit.label = "material sample vertex upload";
    if (auto submitted = device.Submit(submit); !submitted)
    {
        PrintFailure("Submit vertex upload", submitted.Error());
        return false;
    }
    if (auto idle = device.WaitIdle(); !idle)
    {
        PrintFailure("WaitIdle vertex upload", idle.Error());
        return false;
    }
    if (auto destroyed = device.DestroyBuffer(stagingBuffer); !destroyed)
    {
        PrintFailure("DestroyBuffer staging", destroyed.Error());
        return false;
    }
    return true;
}

[[nodiscard]] std::array<float, 4> ColorFor(u32 index) noexcept
{
    return {0.15F + static_cast<float>((index * 37U) % 80U) / 100.0F,
            0.15F + static_cast<float>((index * 53U) % 80U) / 100.0F,
            0.15F + static_cast<float>((index * 71U) % 80U) / 100.0F,
            index >= kMaskedEnd ? 0.55F : 1.0F};
}

[[nodiscard]] bool DrawFrame(Device& device, SwapChain& swapChain,
                             std::array<HotGraphicsPipeline, 3>& pipelines,
                             std::array<MaterialInstance, kMaterialCount>& instances,
                             Span<const MaterialDrawSortKey> order, BufferHandle vertices,
                             u32 width, u32 height, bool& outSubmitted) noexcept
{
    outSubmitted = false;
    auto acquired = swapChain.AcquireNextImage();
    if (!acquired)
    {
        if (acquired.Error().code == cyber::core::ErrorCode::NotFound)
        {
            return true;
        }
        PrintFailure("AcquireNextImage", acquired.Error());
        return false;
    }

    CommandListDesc listDesc{};
    listDesc.label = "material sample frame";
    auto createdList = device.CreateCommandList(listDesc);
    if (!createdList)
    {
        PrintFailure("CreateCommandList", createdList.Error());
        return false;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin())
    {
        std::printf("Could not begin material sample command list.\n");
        return false;
    }

    for (MaterialInstance& instance : instances)
    {
        if (instance.IsDirty())
        {
            auto uploaded = instance.RecordUpload(list);
            if (!uploaded)
            {
                PrintFailure("RecordUpload", uploaded.Error());
                return false;
            }
        }
    }

    const TextureHandle backbuffer = acquired.Value().texture;
    const TextureBarrier toRenderTarget{backbuffer, SubresourceRange{}, ResourceState::Present,
                                        ResourceState::RenderTarget};
    if (auto transitioned = list.Barrier(Span<const BufferBarrier>{},
                                         Span<const TextureBarrier>(&toRenderTarget, 1));
        !transitioned)
    {
        PrintFailure("Barrier backbuffer", transitioned.Error());
        return false;
    }
    ColorAttachment color{};
    color.view = acquired.Value().view;
    color.loadOp = LoadOp::Clear;
    color.storeOp = StoreOp::Store;
    color.clearValue = ClearColorValue::Of(0.025F, 0.035F, 0.07F, 1.0F);
    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&color, 1);
    if (!list.BeginRenderPass(pass) ||
        !list.SetViewport(Viewport::Of(static_cast<float>(width), static_cast<float>(height))) ||
        !list.SetScissor(ScissorRect{0, 0, width, height}))
    {
        std::printf("Could not begin the material sample render pass.\n");
        return false;
    }

    u32 activePipeline = 3;
    for (cyber::core::usize drawIndex = 0; drawIndex < order.Size(); ++drawIndex)
    {
        const MaterialDrawSortKey& draw = order[drawIndex];
        const u32 materialIndex = static_cast<u32>(draw.materialIdentity);
        const u32 pipelineIndex = PipelineIndex(draw.queue);
        if (materialIndex >= kMaterialCount || pipelineIndex >= pipelines.size())
        {
            std::printf("Material sort key contains an out-of-range identity.\n");
            return false;
        }
        if (activePipeline != pipelineIndex)
        {
            const auto pipeline = pipelines[pipelineIndex].Current();
            if (!pipeline.IsValid() || !list.SetPipeline(pipeline))
            {
                std::printf("Could not bind a material queue pipeline.\n");
                return false;
            }
            activePipeline = pipelineIndex;
        }
        if (!list.SetVertexBuffer(0, vertices) ||
            !list.SetBindGroup(cyber::rhi::BindGroupSlot::PerMaterial,
                               instances[materialIndex].BindGroup()))
        {
            std::printf("Could not bind material draw resources.\n");
            return false;
        }

        const u32 row = materialIndex / 10;
        const u32 column = materialIndex % 10;
        const float centerX = -0.9F + static_cast<float>(column) * 0.2F;
        const float centerY = 0.9F - static_cast<float>(row) * 0.2F;
        const std::array<float, 16> objectToClip = {
            0.08F, 0.0F, 0.0F, 0.0F, 0.0F,    0.08F,   0.0F, 0.0F,
            0.0F,  0.0F, 1.0F, 0.0F, centerX, centerY, 0.0F, 1.0F,
        };
        const auto* bytes = reinterpret_cast<const Byte*>(objectToClip.data());
        if (!list.PushConstants(cyber::rhi::ShaderStageBit(cyber::rhi::ShaderStage::Vertex), 0,
                                Span<const Byte>(bytes, sizeof(objectToClip))) ||
            !list.Draw(cyber::rhi::DrawArguments{6, 1, 0, 0}))
        {
            std::printf("Could not draw a material instance.\n");
            return false;
        }
    }
    if (!list.EndRenderPass())
    {
        std::printf("Could not end the material sample render pass.\n");
        return false;
    }
    const TextureBarrier toPresent{backbuffer, SubresourceRange{}, ResourceState::RenderTarget,
                                   ResourceState::Present};
    if (auto transitioned =
            list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(&toPresent, 1));
        !transitioned)
    {
        PrintFailure("Barrier present", transitioned.Error());
        return false;
    }
    if (!list.End())
    {
        std::printf("Could not finish material sample command list.\n");
        return false;
    }
    const CommandListHandle handle = list.Handle();
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(&handle, 1);
    submit.label = "material sample frame";
    if (auto submitted = device.Submit(submit); !submitted)
    {
        PrintFailure("Submit", submitted.Error());
        return false;
    }
    if (auto presented = swapChain.Present(); !presented)
    {
        PrintFailure("Present", presented.Error());
        return false;
    }
    if (auto idle = device.WaitIdle(); !idle)
    {
        PrintFailure("WaitIdle", idle.Error());
        return false;
    }
    outSubmitted = true;
    return true;
}

[[nodiscard]] bool WriteReport(HeapAllocator& allocator, StringView executableDirectory,
                               Span<const MaterialDrawSortKey> order,
                               const std::array<MaterialInstance, kMaterialCount>& instances,
                               bool liveChange, u64 liveChangeFrame) noexcept
{
    String report(allocator);
    if (!report.TryAppend("Phase 18 material demonstration\n") ||
        !report.TryAppend("definitions=100\ninstances=100\nsorted_draws=100\n") ||
        !report.TryAppend(liveChange ? "live_parameter_change=passed\n"
                                     : "live_parameter_change=pending\n"))
    {
        std::printf("Could not allocate material sample report.\n");
        return false;
    }
    char line[160]{};
    for (cyber::core::usize drawIndex = 0; drawIndex < order.Size(); ++drawIndex)
    {
        const MaterialDrawSortKey& draw = order[drawIndex];
        const int length = std::snprintf(
            line, sizeof(line), "%s pipeline=%llu material=%llu depth=%.3f sequence=%llu\n",
            QueueName(draw.queue), static_cast<unsigned long long>(draw.pipelineIdentity),
            static_cast<unsigned long long>(draw.materialIdentity), draw.viewDepth,
            static_cast<unsigned long long>(draw.submissionSequence));
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(line) ||
            !report.TryAppend(StringView(line, static_cast<cyber::core::usize>(length))))
        {
            std::printf("Could not append material sort report.\n");
            return false;
        }
    }
    char revision[96]{};
    const int revisionLength =
        std::snprintf(revision, sizeof(revision), "material_50_revision=%llu\n",
                      static_cast<unsigned long long>(instances[kLiveMaterialIndex].Revision()));
    if (revisionLength <= 0 || static_cast<std::size_t>(revisionLength) >= sizeof(revision) ||
        !report.TryAppend(StringView(revision, static_cast<cyber::core::usize>(revisionLength))))
    {
        std::printf("Could not append material revision report.\n");
        return false;
    }
    char changeFrame[64]{};
    const int changeFrameLength =
        std::snprintf(changeFrame, sizeof(changeFrame), "live_change_frame=%llu\n",
                      static_cast<unsigned long long>(liveChangeFrame));
    if (changeFrameLength <= 0 ||
        static_cast<std::size_t>(changeFrameLength) >= sizeof(changeFrame) ||
        !report.TryAppend(
            StringView(changeFrame, static_cast<cyber::core::usize>(changeFrameLength))))
    {
        std::printf("Could not append material change-frame report.\n");
        return false;
    }
    String path(allocator);
    if (!path.TryAssign(executableDirectory) || !path.TryAppend("/material-report.txt"))
    {
        std::printf("Could not create material report path.\n");
        return false;
    }
    const StringView text = report.View();
    auto written = cyber::platform::WriteFile(
        path.CStr(), Span<const Byte>(reinterpret_cast<const Byte*>(text.Data()), text.Size()));
    if (!written)
    {
        PrintFailure("WriteFile material report", written.Error());
        return false;
    }
    std::printf("Material report: %s\n", path.CStr());
    return true;
}

[[nodiscard]] bool ParseArguments(int argc, char** argv, bool& selfTest) noexcept
{
    selfTest = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--self-test") == 0 && !selfTest)
        {
            selfTest = true;
        }
        else
        {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    bool selfTest = false;
    if (!ParseArguments(argc, argv, selfTest))
    {
        std::printf("usage: cyber_sample_materials [--self-test]\n");
        return 2;
    }
    if (!cyber::platform::IsWindowSystemSupported())
    {
        std::printf("The material sample requires a window system.\n");
        return 1;
    }
    const Backend* backend = cyber::rhi::D3D12Backend();
    if (backend == nullptr || backend->enumerateAdapters(nullptr, 0) == 0)
    {
        std::printf("No D3D12 backend or adapter is available.\n");
        return 1;
    }

    HeapAllocator heap;
    WindowDesc windowDesc{};
    windowDesc.title = "CyberGraphics - material system";
    windowDesc.width = 1000;
    windowDesc.height = 720;
    auto createdWindow = Window::Create(windowDesc, heap);
    if (!createdWindow)
    {
        PrintFailure("Window::Create", createdWindow.Error());
        return 1;
    }
    Window window = std::move(createdWindow.Value());

    DeviceDesc deviceDesc{};
    deviceDesc.backend = backend;
    auto createdDevice = Device::Create(deviceDesc, heap);
    if (!createdDevice)
    {
        PrintFailure("Device::Create", createdDevice.Error());
        return 1;
    }
    Device device = std::move(createdDevice.Value());

    SwapChainDesc swapChainDesc{};
    swapChainDesc.surface.nativeWindow = window.NativeHandle();
    swapChainDesc.surface.width = static_cast<u32>(window.Width());
    swapChainDesc.surface.height = static_cast<u32>(window.Height());
    swapChainDesc.format = Format::BGRA8Unorm;
    swapChainDesc.imageCount = 2;
    swapChainDesc.presentMode = PresentMode::Fifo;
    swapChainDesc.label = "Material sample swapchain";
    auto createdSwapChain = device.CreateSwapChain(swapChainDesc);
    if (!createdSwapChain)
    {
        PrintFailure("CreateSwapChain", createdSwapChain.Error());
        return 1;
    }
    SwapChain swapChain = std::move(createdSwapChain.Value());

    auto vertexResult = CompileHlsl("material_grid_vertex.cgs", StringView(kVertexSource), heap);
    auto pixelResult = CompileHlsl("material_grid_pixel.cgs", StringView(kPixelSource), heap);
    if (!vertexResult || !pixelResult)
    {
        if (!vertexResult)
        {
            std::printf("Vertex shader compile failed at %u:%u: %s\n", vertexResult.Error().line,
                        vertexResult.Error().column,
                        vertexResult.Error().message != nullptr ? vertexResult.Error().message
                                                                : "unknown diagnostic");
        }
        if (!pixelResult)
        {
            std::printf("Pixel shader compile failed at %u:%u: %s\n", pixelResult.Error().line,
                        pixelResult.Error().column,
                        pixelResult.Error().message != nullptr ? pixelResult.Error().message
                                                               : "unknown diagnostic");
        }
        return 1;
    }
    CompiledShader vertex = std::move(vertexResult.Value());
    CompiledShader pixel = std::move(pixelResult.Value());
    const Reflection* stages[] = {&vertex.reflection, &pixel.reflection};

    std::array<HotGraphicsPipeline, 3> pipelines{};
    for (u32 queue = 0; queue < pipelines.size(); ++queue)
    {
        ColorAttachmentState colorState{};
        colorState.format = Format::BGRA8Unorm;
        if (queue == PipelineIndex(MaterialQueue::Transparent))
        {
            colorState.blend.enabled = true;
            colorState.blend.color.source = cyber::rhi::BlendFactor::SourceAlpha;
            colorState.blend.color.destination = cyber::rhi::BlendFactor::OneMinusSourceAlpha;
            colorState.blend.alpha.source = cyber::rhi::BlendFactor::One;
            colorState.blend.alpha.destination = cyber::rhi::BlendFactor::OneMinusSourceAlpha;
        }
        HotGraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.device = &device;
        pipelineDesc.state.colorAttachments = Span<const ColorAttachmentState>(&colorState, 1);
        pipelineDesc.state.rasterizer.cullMode = cyber::rhi::CullMode::None;
        pipelineDesc.state.depthStencil.depthTestEnabled = false;
        pipelineDesc.state.depthStencil.depthWriteEnabled = false;
        pipelineDesc.label = queue == 0   ? "Opaque material pipeline"
                             : queue == 1 ? "Masked material pipeline"
                                          : "Transparent material pipeline";
        auto createdPipeline = HotGraphicsPipeline::Create(pipelineDesc, heap);
        if (!createdPipeline)
        {
            PrintFailure("HotGraphicsPipeline::Create", createdPipeline.Error());
            return 1;
        }
        pipelines[queue] = std::move(createdPipeline.Value());
        if (auto rebuilt = pipelines[queue].Rebuild(vertex, pixel); !rebuilt)
        {
            PrintFailure("HotGraphicsPipeline::Rebuild", rebuilt.Error());
            return 1;
        }
        pipelines[queue].BeginFrame(0);
        if (!pipelines[queue].HasBuilt())
        {
            const Error* error = pipelines[queue].LastError();
            if (error != nullptr)
            {
                PrintFailure("Pipeline build", *error);
            }
            return 1;
        }
    }

    MaterialValue minimum{};
    minimum.type = cyber::shaders::ValueType::Float4;
    minimum.components = {0.0F, 0.0F, 0.0F, 0.0F};
    MaterialValue maximum{};
    maximum.type = cyber::shaders::ValueType::Float4;
    maximum.components = {1.0F, 1.0F, 1.0F, 1.0F};
    MaterialValue defaultColor{};
    defaultColor.type = cyber::shaders::ValueType::Float4;
    defaultColor.components = {1.0F, 1.0F, 1.0F, 1.0F};
    auto definitionsStorage = std::unique_ptr<std::array<MaterialDefinition, kMaterialCount>>(
        new (std::nothrow) std::array<MaterialDefinition, kMaterialCount>{});
    auto instancesStorage = std::unique_ptr<std::array<MaterialInstance, kMaterialCount>>(
        new (std::nothrow) std::array<MaterialInstance, kMaterialCount>{});
    auto drawOrderStorage = std::unique_ptr<std::array<MaterialDrawSortKey, kMaterialCount>>(
        new (std::nothrow) std::array<MaterialDrawSortKey, kMaterialCount>{});
    if (!definitionsStorage || !instancesStorage || !drawOrderStorage)
    {
        std::printf("Could not allocate material sample data.\n");
        return 1;
    }
    auto& definitions = *definitionsStorage;
    auto& instances = *instancesStorage;
    auto& drawOrder = *drawOrderStorage;
    for (u32 index = 0; index < kMaterialCount; ++index)
    {
        const MaterialQueue queue =
            index < kOpaqueEnd
                ? MaterialQueue::Opaque
                : (index < kMaskedEnd ? MaterialQueue::Masked : MaterialQueue::Transparent);
        char name[32]{};
        const int length = std::snprintf(name, sizeof(name), "GridMaterial_%03u", index);
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(name))
        {
            std::printf("Could not format material name.\n");
            return 1;
        }
        MaterialValue color{};
        color.type = cyber::shaders::ValueType::Float4;
        color.components = ColorFor(index);
        const MaterialParameterDesc parameter{"baseColorFactor", defaultColor, minimum, maximum};
        MaterialRenderState renderState{};
        renderState.cull = cyber::renderer::MaterialCullMode::None;
        renderState.blend = queue == MaterialQueue::Transparent
                                ? cyber::renderer::MaterialBlendMode::Alpha
                                : cyber::renderer::MaterialBlendMode::Off;
        renderState.depthTest = false;
        renderState.depthWrite = false;
        MaterialDefinitionDesc definitionDesc{};
        definitionDesc.name = StringView(name, static_cast<cyber::core::usize>(length));
        definitionDesc.queue = queue;
        definitionDesc.renderState = renderState;
        definitionDesc.pipeline = &pipelines[PipelineIndex(queue)];
        definitionDesc.stages = Span<const Reflection* const>(stages, 2);
        definitionDesc.parameters = Span<const MaterialParameterDesc>(&parameter, 1);
        auto definition = MaterialDefinition::Create(definitionDesc);
        if (!definition)
        {
            PrintFailure("MaterialDefinition::Create", definition.Error());
            return 1;
        }
        definitions[index] = std::move(definition.Value());
        auto instance = MaterialInstance::Create(
            definitions[index], device, heap, Span<const cyber::renderer::MaterialTextureView>{});
        if (!instance)
        {
            PrintFailure("MaterialInstance::Create", instance.Error());
            return 1;
        }
        instances[index] = std::move(instance.Value());
        if (auto set = instances[index].SetParameter("baseColorFactor", color); !set)
        {
            PrintFailure("SetParameter initial color", set.Error());
            return 1;
        }
        drawOrder[index] = MaterialDrawSortKey{queue, PipelineIndex(queue), index,
                                               1.0F + static_cast<float>(index % 10U), index};
    }
    if (auto sorted =
            SortMaterialDraws(Span<MaterialDrawSortKey>(drawOrder.data(), drawOrder.size()));
        !sorted)
    {
        PrintFailure("SortMaterialDraws", sorted.Error());
        return 1;
    }

    BufferHandle vertexBuffer{};
    if (!UploadVertices(device, vertexBuffer))
    {
        return 1;
    }
    auto executableDirectory = cyber::platform::ExecutableDirectory(heap);
    if (!executableDirectory)
    {
        PrintFailure("ExecutableDirectory", executableDirectory.Error());
        return 1;
    }

    std::printf(
        "Rendering 100 material instances across opaque, masked, and transparent queues.\n");
    std::printf("Press T to change material 50 on the next frame; press Escape to close.\n");
    if (selfTest)
    {
        std::printf("Self-test mode changes material 50 at frame 20 and exits after frame 40.\n");
    }
    u64 frameIndex = 0;
    u32 renderedFrames = 0;
    bool liveChange = false;
    bool liveChangePending = false;
    u64 liveChangeFrame = 0;
    bool parameterIsAlternate = false;
    bool parameterNeedsUpdate = false;
    bool failed = false;
    while (window.IsOpen() && !failed)
    {
        window.BeginFrame();
        window.PollEvents();
        WindowEvent event{};
        while (window.PollEvent(event))
        {
            if (event.type == WindowEventType::Close ||
                (event.type == WindowEventType::KeyDown && event.key == Key::Escape))
            {
                window.RequestClose();
            }
            else if (event.type == WindowEventType::KeyDown && event.key == Key::T)
            {
                parameterIsAlternate = !parameterIsAlternate;
                parameterNeedsUpdate = true;
            }
            else if (event.type == WindowEventType::Resize && event.width > 0 && event.height > 0)
            {
                const auto info = swapChain.Info();
                if (info.width != static_cast<u32>(event.width) ||
                    info.height != static_cast<u32>(event.height))
                {
                    if (auto resized = swapChain.Resize(static_cast<u32>(event.width),
                                                        static_cast<u32>(event.height));
                        !resized)
                    {
                        PrintFailure("SwapChain::Resize", resized.Error());
                        failed = true;
                    }
                }
            }
        }
        if (selfTest && renderedFrames == 20 && !liveChange && !parameterNeedsUpdate)
        {
            parameterIsAlternate = true;
            parameterNeedsUpdate = true;
        }
        if (parameterNeedsUpdate)
        {
            MaterialValue changed{};
            changed.type = cyber::shaders::ValueType::Float4;
            changed.components = parameterIsAlternate
                                     ? std::array<float, 4>{1.0F, 0.05F, 0.85F, 1.0F}
                                     : ColorFor(kLiveMaterialIndex);
            if (!SetMaterialParameter(instances[kLiveMaterialIndex], changed))
            {
                failed = true;
            }
            else
            {
                parameterNeedsUpdate = false;
                liveChangePending = true;
                std::printf(
                    "Material 50 parameter revision %llu queued for frame %llu.\n",
                    static_cast<unsigned long long>(instances[kLiveMaterialIndex].Revision()),
                    static_cast<unsigned long long>(frameIndex));
            }
        }
        if (failed || !window.IsOpen())
        {
            break;
        }
        const auto swapInfo = swapChain.Info();
        if (swapInfo.width == 0 || swapInfo.height == 0)
        {
            continue;
        }
        for (HotGraphicsPipeline& pipeline : pipelines)
        {
            pipeline.BeginFrame(frameIndex);
        }
        bool frameSubmitted = false;
        if (!DrawFrame(device, swapChain, pipelines, instances,
                       Span<const MaterialDrawSortKey>(drawOrder.data(), drawOrder.size()),
                       vertexBuffer, swapInfo.width, swapInfo.height, frameSubmitted))
        {
            failed = true;
            break;
        }
        if (!frameSubmitted)
        {
            continue;
        }
        if (liveChangePending)
        {
            liveChange = true;
            liveChangePending = false;
            liveChangeFrame = frameIndex;
        }
        ++frameIndex;
        ++renderedFrames;
        if (selfTest && renderedFrames >= 40)
        {
            window.RequestClose();
        }
    }

    if (!device.WaitIdle())
    {
        std::printf("Could not wait for the material sample GPU work.\n");
        failed = true;
    }
    if (selfTest && (!liveChange || instances[kLiveMaterialIndex].Revision() != 3))
    {
        std::printf("The live material parameter assertion failed.\n");
        failed = true;
    }
    if (!WriteReport(heap, executableDirectory.Value().View(),
                     Span<const MaterialDrawSortKey>(drawOrder.data(), drawOrder.size()), instances,
                     liveChange, liveChangeFrame))
    {
        failed = true;
    }
    if (vertexBuffer.IsValid())
    {
        if (auto destroyed = device.DestroyBuffer(vertexBuffer); !destroyed)
        {
            PrintFailure("DestroyBuffer vertices", destroyed.Error());
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
