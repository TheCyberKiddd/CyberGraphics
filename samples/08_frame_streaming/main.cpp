#include <cyber/core/error.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/bindless_table.h>
#include <cyber/renderer/frame_stream.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/frame_ring.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/texture.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

using cyber::core::Error;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::renderer::BindlessResourceTable;
using cyber::renderer::BindlessResourceTableDesc;
using cyber::renderer::FrameBufferStream;
using cyber::renderer::FrameBufferStreamDesc;
using cyber::rhi::BindGroupHandle;
using cyber::rhi::BindGroupLayoutHandle;
using cyber::rhi::BufferHandle;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListHandle;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::FenceHandle;
using cyber::rhi::FrameRing;
using cyber::rhi::FrameRingDesc;
using cyber::rhi::GraphicsPipelineDesc;
using cyber::rhi::PipelineHandle;
using cyber::rhi::PipelineLayoutHandle;
using cyber::rhi::QueueKind;
using cyber::rhi::ShaderHandle;
using cyber::rhi::TextureHandle;
using cyber::rhi::TextureViewHandle;

namespace
{

constexpr u32 kDrawCount = 10000;
constexpr u32 kWarmupCount = 3;
constexpr u32 kSampleCount = 10;
constexpr u32 kTargetExtent = 256;
constexpr u64 kFrameBudgetMicroseconds = 16667;
constexpr u64 kUniformStreamCapacity = static_cast<u64>(kDrawCount) * 256;
constexpr u64 kObjectStreamCapacity = static_cast<u64>(kDrawCount) * 64;

constexpr char kConventionalVertexShader[] =
    "cbuffer ObjectTransform : register(b0, space3) { float4x4 objectToClip; };\n"
    "struct Input { float2 position : TEXCOORD0; };\n"
    "float4 main(Input input) : SV_Position\n"
    "{\n"
    "    return mul(objectToClip, float4(input.position, 0.0, 1.0));\n"
    "}\n";
constexpr char kBindlessVertexShader[] =
    "cbuffer DrawIndex : register(b0, space0) { uint objectIndex; };\n"
    "StructuredBuffer<float4x4> objects[10000] : register(t10000, space2);\n"
    "struct Input { float2 position : TEXCOORD0; };\n"
    "float4 main(Input input) : SV_Position\n"
    "{\n"
    "    return mul(objects[objectIndex][0], float4(input.position, 0.0, 1.0));\n"
    "}\n";
constexpr char kPixelShader[] =
    "float4 main() : SV_Target { return float4(0.25, 0.75, 1.0, 1.0); }\n";
constexpr float kTriangle[] = {-1.0F, -1.0F, 1.0F, -1.0F, 0.0F, 1.0F};

struct Transform
{
    float matrix[16]{};
};

struct PipelineResources
{
    ShaderHandle vertex{};
    ShaderHandle pixel{};
    PipelineHandle pipeline{};
    PipelineLayoutHandle layout{};
};

struct TimingSamples
{
    u64 recordMicroseconds[kSampleCount]{};
    u64 submitMicroseconds[kSampleCount]{};
    u64 completionMicroseconds[kSampleCount]{};
};

void PrintFailure(const char* operation, const Error& error) noexcept
{
    std::printf("%s failed: %s (%s)\n", operation, cyber::core::ErrorCodeName(error.code),
                error.message != nullptr ? error.message : "no diagnostic");
}

[[nodiscard]] bool ReportDestruction(const char* operation, Status<Error> result) noexcept
{
    if (!result)
    {
        PrintFailure(operation, result.Error());
        return false;
    }
    return true;
}

[[nodiscard]] Span<const Byte> Bytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text), std::strlen(text));
}

[[nodiscard]] bool CreatePipeline(Device& device, BindGroupLayoutHandle groupLayout, bool bindless,
                                  PipelineResources& out) noexcept
{
    cyber::rhi::PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(bindless ? cyber::rhi::BindGroupSlot::PerMaterial
                                                : cyber::rhi::BindGroupSlot::PerDraw)] =
        groupLayout;
    cyber::rhi::PushConstantRange pushRange{};
    if (bindless)
    {
        pushRange.offsetBytes = 0;
        pushRange.sizeBytes = sizeof(u32);
        pushRange.stages = cyber::rhi::ShaderStageBit(cyber::rhi::ShaderStage::Vertex);
        layoutDesc.pushConstants = Span<const cyber::rhi::PushConstantRange>(&pushRange, 1);
    }
    auto layout = device.CreatePipelineLayout(layoutDesc);
    if (!layout)
    {
        PrintFailure("CreatePipelineLayout", layout.Error());
        return false;
    }
    out.layout = layout.Value();

    cyber::rhi::ShaderDesc vertexDesc{};
    vertexDesc.stage = cyber::rhi::ShaderStage::Vertex;
    vertexDesc.format = cyber::rhi::ShaderBytecodeFormat::HlslSource;
    vertexDesc.code = Bytes(bindless ? kBindlessVertexShader : kConventionalVertexShader);
    vertexDesc.entryPoint = "main";
    vertexDesc.label = bindless ? "bindless object vertex shader" : "dynamic object vertex shader";
    auto vertex = device.CreateShader(vertexDesc);
    if (!vertex)
    {
        PrintFailure("CreateShader vertex", vertex.Error());
        return false;
    }
    out.vertex = vertex.Value();

    cyber::rhi::ShaderDesc pixelDesc{};
    pixelDesc.stage = cyber::rhi::ShaderStage::Pixel;
    pixelDesc.format = cyber::rhi::ShaderBytecodeFormat::HlslSource;
    pixelDesc.code = Bytes(kPixelShader);
    pixelDesc.entryPoint = "main";
    pixelDesc.label = "frame streaming pixel shader";
    auto pixel = device.CreateShader(pixelDesc);
    if (!pixel)
    {
        PrintFailure("CreateShader pixel", pixel.Error());
        return false;
    }
    out.pixel = pixel.Value();

    cyber::rhi::VertexBinding vertexBinding{};
    vertexBinding.strideBytes = sizeof(float) * 2;
    cyber::rhi::VertexAttribute attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = cyber::rhi::Format::RG32Float;

    cyber::rhi::ColorAttachmentState colorState{};
    colorState.format = cyber::rhi::Format::RGBA8Unorm;
    GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.layout = out.layout;
    pipelineDesc.vertexShader = out.vertex;
    pipelineDesc.pixelShader = out.pixel;
    pipelineDesc.vertexLayout.bindings = Span<const cyber::rhi::VertexBinding>(&vertexBinding, 1);
    pipelineDesc.vertexLayout.attributes = Span<const cyber::rhi::VertexAttribute>(&attribute, 1);
    pipelineDesc.rasterizer.cullMode = cyber::rhi::CullMode::None;
    pipelineDesc.depthStencil.depthTestEnabled = false;
    pipelineDesc.depthStencil.depthWriteEnabled = false;
    pipelineDesc.depthStencilFormat = cyber::rhi::Format::Count;
    pipelineDesc.colorAttachments = Span<const cyber::rhi::ColorAttachmentState>(&colorState, 1);
    pipelineDesc.label = bindless ? "bindless 10000 draw pipeline" : "dynamic 10000 draw pipeline";
    auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
    if (!pipeline)
    {
        PrintFailure("CreateGraphicsPipeline", pipeline.Error());
        return false;
    }
    out.pipeline = pipeline.Value();
    return true;
}

[[nodiscard]] bool UploadTriangle(Device& device, BufferHandle& outBuffer) noexcept
{
    cyber::rhi::BufferDesc destinationDesc{};
    destinationDesc.usage =
        cyber::rhi::BufferUsage::Vertex | cyber::rhi::BufferUsage::CopyDestination;
    destinationDesc.sizeBytes = sizeof(kTriangle);
    destinationDesc.label = "phase 19 benchmark triangle";
    auto destination = device.CreateBuffer(destinationDesc);
    if (!destination)
    {
        PrintFailure("CreateBuffer triangle", destination.Error());
        return false;
    }
    outBuffer = destination.Value();

    cyber::rhi::BufferDesc stagingDesc{};
    stagingDesc.usage = cyber::rhi::BufferUsage::HostVisible | cyber::rhi::BufferUsage::CopySource;
    stagingDesc.sizeBytes = sizeof(kTriangle);
    auto staging = device.CreateBuffer(stagingDesc);
    if (!staging)
    {
        PrintFailure("CreateBuffer triangle staging", staging.Error());
        return false;
    }
    if (auto written = device.WriteBuffer(
            staging.Value(), 0,
            Span<const Byte>(reinterpret_cast<const Byte*>(kTriangle), sizeof(kTriangle)));
        !written)
    {
        PrintFailure("WriteBuffer triangle", written.Error());
        return false;
    }

    auto createdList = device.CreateCommandList(cyber::rhi::CommandListDesc{});
    if (!createdList)
    {
        PrintFailure("CreateCommandList triangle upload", createdList.Error());
        return false;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin())
    {
        std::printf("Could not begin the triangle upload list.\n");
        return false;
    }
    const cyber::rhi::BufferBarrier barriers[] = {
        {staging.Value(), cyber::rhi::ResourceState::Undefined,
         cyber::rhi::ResourceState::CopySource},
        {outBuffer, cyber::rhi::ResourceState::Undefined,
         cyber::rhi::ResourceState::CopyDestination},
    };
    if (auto transitioned = list.Barrier(Span<const cyber::rhi::BufferBarrier>(barriers, 2));
        !transitioned)
    {
        PrintFailure("Barrier triangle upload", transitioned.Error());
        return false;
    }
    const cyber::rhi::BufferCopy copy{staging.Value(), 0, outBuffer, 0, sizeof(kTriangle)};
    if (auto copied = list.CopyBuffer(copy); !copied)
    {
        PrintFailure("CopyBuffer triangle", copied.Error());
        return false;
    }
    const cyber::rhi::BufferBarrier ready{outBuffer, cyber::rhi::ResourceState::CopyDestination,
                                          cyber::rhi::ResourceState::VertexBuffer};
    if (auto transitioned = list.Barrier(Span<const cyber::rhi::BufferBarrier>(&ready, 1));
        !transitioned || !list.End())
    {
        if (!transitioned)
        {
            PrintFailure("Barrier triangle ready", transitioned.Error());
        }
        return false;
    }
    const CommandListHandle commandList = list.Handle();
    cyber::rhi::SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(&commandList, 1);
    if (auto submitted = device.Submit(submit); !submitted)
    {
        PrintFailure("Submit triangle upload", submitted.Error());
        return false;
    }
    if (auto idle = device.WaitIdle(); !idle)
    {
        PrintFailure("WaitIdle triangle upload", idle.Error());
        return false;
    }
    if (auto destroyed = device.DestroyBuffer(staging.Value()); !destroyed)
    {
        PrintFailure("DestroyBuffer triangle staging", destroyed.Error());
        return false;
    }
    return true;
}

void FillTransforms(Transform* transforms) noexcept
{
    for (u32 index = 0; index < kDrawCount; ++index)
    {
        const u32 column = index % 100;
        const u32 row = index / 100;
        const float centerX = -0.99F + static_cast<float>(column) * 0.02F;
        const float centerY = 0.99F - static_cast<float>(row) * 0.02F;
        Transform& transform = transforms[index];
        transform.matrix[0] = 0.006F;
        transform.matrix[5] = 0.006F;
        transform.matrix[10] = 1.0F;
        transform.matrix[12] = centerX;
        transform.matrix[13] = centerY;
        transform.matrix[15] = 1.0F;
    }
}

[[nodiscard]] bool
RecordAndSubmitFrame(Device& device, FrameRing& ring, FenceHandle fence, FrameBufferStream& stream,
                     const cyber::rhi::FrameSlot& frame, bool bindless, bool& targetReady,
                     TextureHandle target, TextureViewHandle targetView, BufferHandle vertices,
                     PipelineHandle pipeline, BindGroupHandle group, BindlessResourceTable* table,
                     const Transform* transforms, u64* dynamicOffsets, TimingSamples& timings,
                     u32 sampleIndex) noexcept
{
    const auto recordStart = std::chrono::steady_clock::now();
    if (!stream.BeginFrame(ring, frame).IsOk())
    {
        std::printf("Could not begin the stream frame.\n");
        return false;
    }
    if (bindless)
    {
        const auto bytes = Span<const Byte>(reinterpret_cast<const Byte*>(transforms),
                                            sizeof(Transform) * kDrawCount);
        auto allocation = stream.Allocate(bytes);
        if (!allocation)
        {
            PrintFailure("Allocate bindless object data", allocation.Error());
            return false;
        }
        if (!table->SyncFrame(ring, frame))
        {
            std::printf("Could not synchronize the bindless frame table.\n");
            return false;
        }
    }
    else
    {
        for (u32 index = 0; index < kDrawCount; ++index)
        {
            const auto bytes = Span<const Byte>(reinterpret_cast<const Byte*>(&transforms[index]),
                                                sizeof(Transform));
            auto allocation = stream.Allocate(bytes);
            if (!allocation)
            {
                PrintFailure("Allocate dynamic object data", allocation.Error());
                return false;
            }
            dynamicOffsets[index] = allocation.Value().offsetBytes;
        }
    }

    auto createdList = device.CreateCommandList(cyber::rhi::CommandListDesc{});
    if (!createdList)
    {
        PrintFailure("CreateCommandList frame", createdList.Error());
        return false;
    }
    CommandList list = std::move(createdList.Value());
    if (!list.Begin() || !stream.RecordUploads(list))
    {
        std::printf("Could not begin or upload a benchmark frame.\n");
        return false;
    }
    if (!targetReady)
    {
        const cyber::rhi::TextureBarrier toTarget{target, cyber::rhi::SubresourceRange{},
                                                  cyber::rhi::ResourceState::Undefined,
                                                  cyber::rhi::ResourceState::RenderTarget};
        if (auto transitioned = list.Barrier(Span<const cyber::rhi::BufferBarrier>{},
                                             Span<const cyber::rhi::TextureBarrier>(&toTarget, 1));
            !transitioned)
        {
            PrintFailure("Barrier benchmark target", transitioned.Error());
            return false;
        }
        targetReady = true;
    }

    cyber::rhi::ColorAttachment color{};
    color.view = targetView;
    color.loadOp = cyber::rhi::LoadOp::Clear;
    color.storeOp = cyber::rhi::StoreOp::Store;
    color.clearValue = cyber::rhi::ClearColorValue::Of(0.02F, 0.03F, 0.05F, 1.0F);
    cyber::rhi::RenderPassDesc pass{};
    pass.colorAttachments = Span<const cyber::rhi::ColorAttachment>(&color, 1);
    if (!list.BeginRenderPass(pass) ||
        !list.SetViewport(cyber::rhi::Viewport::Of(static_cast<float>(kTargetExtent),
                                                   static_cast<float>(kTargetExtent))) ||
        !list.SetScissor(cyber::rhi::ScissorRect{0, 0, kTargetExtent, kTargetExtent}) ||
        !list.SetPipeline(pipeline) || !list.SetVertexBuffer(0, vertices))
    {
        std::printf("Could not set up the 10,000-draw pass.\n");
        return false;
    }
    if (bindless && !list.SetBindGroup(cyber::rhi::BindGroupSlot::PerMaterial, group))
    {
        std::printf("Could not bind the bindless table.\n");
        return false;
    }

    for (u32 index = 0; index < kDrawCount; ++index)
    {
        if (bindless)
        {
            const Byte* indexBytes = reinterpret_cast<const Byte*>(&index);
            if (!list.PushConstants(cyber::rhi::ShaderStageBit(cyber::rhi::ShaderStage::Vertex), 0,
                                    Span<const Byte>(indexBytes, sizeof(index))) ||
                !list.Draw(cyber::rhi::DrawArguments{3, 1, 0, 0}))
            {
                std::printf("Could not record a bindless draw.\n");
                return false;
            }
        }
        else
        {
            const u64 offset = dynamicOffsets[index];
            if (!list.SetBindGroup(cyber::rhi::BindGroupSlot::PerDraw, group,
                                   Span<const u64>(&offset, 1)) ||
                !list.Draw(cyber::rhi::DrawArguments{3, 1, 0, 0}))
            {
                std::printf("Could not record a dynamic-offset draw.\n");
                return false;
            }
        }
    }
    if (!list.EndRenderPass() || !list.End())
    {
        std::printf("Could not finish the 10,000-draw command list.\n");
        return false;
    }
    const auto recordEnd = std::chrono::steady_clock::now();

    const CommandListHandle listHandle = list.Handle();
    const cyber::rhi::FenceSignal signal{fence, frame.signalValue};
    cyber::rhi::SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(&listHandle, 1);
    submit.signals = Span<const cyber::rhi::FenceSignal>(&signal, 1);
    const auto submitStart = std::chrono::steady_clock::now();
    if (auto submitted = device.Submit(submit); !submitted)
    {
        PrintFailure("Submit benchmark frame", submitted.Error());
        return false;
    }
    const auto submitEnd = std::chrono::steady_clock::now();
    if (auto completed = device.WaitFence(fence, frame.signalValue); !completed)
    {
        PrintFailure("WaitFence benchmark frame", completed.Error());
        return false;
    }
    const auto completeEnd = std::chrono::steady_clock::now();

    const auto ToMicroseconds = [](std::chrono::steady_clock::duration duration) noexcept
    {
        return static_cast<u64>(
            std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
    };
    if (sampleIndex < kSampleCount)
    {
        timings.recordMicroseconds[sampleIndex] = ToMicroseconds(recordEnd - recordStart);
        timings.submitMicroseconds[sampleIndex] = ToMicroseconds(submitEnd - submitStart);
        timings.completionMicroseconds[sampleIndex] = ToMicroseconds(completeEnd - recordStart);
    }
    return true;
}

[[nodiscard]] bool RunPath(Device& device, FrameRing& ring, FenceHandle fence,
                           FrameBufferStream& stream, bool bindless, bool& targetReady,
                           TextureHandle target, TextureViewHandle targetView,
                           BufferHandle vertices, PipelineHandle pipeline, BindGroupHandle group,
                           BindlessResourceTable* table, const Transform* transforms,
                           u64* dynamicOffsets, TimingSamples& timings) noexcept
{
    for (u32 iteration = 0; iteration < kWarmupCount + kSampleCount; ++iteration)
    {
        auto frame = ring.BeginFrame(device);
        if (!frame)
        {
            PrintFailure("FrameRing::BeginFrame", frame.Error());
            return false;
        }
        const u32 sampleIndex = iteration < kWarmupCount ? kSampleCount : iteration - kWarmupCount;
        if (!RecordAndSubmitFrame(device, ring, fence, stream, frame.Value(), bindless, targetReady,
                                  target, targetView, vertices, pipeline, group, table, transforms,
                                  dynamicOffsets, timings, sampleIndex))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] u64 Median(u64* values) noexcept
{
    std::sort(values, values + kSampleCount);
    return (values[kSampleCount / 2 - 1] + values[kSampleCount / 2]) / 2;
}

[[nodiscard]] u64 Percentile95(u64* values) noexcept
{
    std::sort(values, values + kSampleCount);
    return values[(kSampleCount * 95 + 99) / 100 - 1];
}

[[nodiscard]] u64 RecordingAndSubmitP95(TimingSamples& timings) noexcept
{
    return Percentile95(timings.recordMicroseconds) + Percentile95(timings.submitMicroseconds);
}

void PrintTimings(const char* name, TimingSamples& timings) noexcept
{
    const u64 recordMedian = Median(timings.recordMicroseconds);
    const u64 recordP95 = Percentile95(timings.recordMicroseconds);
    const u64 submitMedian = Median(timings.submitMicroseconds);
    const u64 submitP95 = Percentile95(timings.submitMicroseconds);
    const u64 completeMedian = Median(timings.completionMicroseconds);
    const u64 completeP95 = Percentile95(timings.completionMicroseconds);
    const u64 cpuP95 = recordP95 + submitP95;
    std::printf("%s: record median/p95=%llu/%llu us, queue submit=%llu/%llu us, "
                "record+submit p95=%llu us, completion=%llu/%llu us\n",
                name, static_cast<unsigned long long>(recordMedian),
                static_cast<unsigned long long>(recordP95),
                static_cast<unsigned long long>(submitMedian),
                static_cast<unsigned long long>(submitP95), static_cast<unsigned long long>(cpuP95),
                static_cast<unsigned long long>(completeMedian),
                static_cast<unsigned long long>(completeP95));
}

[[nodiscard]] bool WriteReport(const char* adapter, TimingSamples& dynamicTimings,
                               TimingSamples& bindlessTimings, bool hasBindless) noexcept
{
#if defined(CG_DEBUG)
    constexpr const char* kBuildConfiguration = "Debug";
#elif defined(CG_PROFILE)
    constexpr const char* kBuildConfiguration = "Profile";
#elif defined(CG_SHIPPING)
    constexpr const char* kBuildConfiguration = "Shipping";
#else
    constexpr const char* kBuildConfiguration = "Release";
#endif
    std::FILE* file = nullptr;
    if (fopen_s(&file, "phase19-submission-report.txt", "wb") != 0 || file == nullptr)
    {
        std::printf("Could not create phase19-submission-report.txt.\n");
        return false;
    }
    const int written = std::fprintf(
        file,
        "Phase 19 10,000-draw submission measurement\n"
        "adapter=%s\n"
        "build_configuration=%s\n"
        "draws_per_frame=%u\nwarmup_frames_per_path=%u\nsamples_per_path=%u\n"
        "frame_budget_us=%llu\n"
        "measurement=CPU recording includes per-draw stream writes and command recording; "
        "queue submit is timed separately; completion is CPU wall time from record start "
        "until the frame fence is reached.\n"
        "dynamic_path=median_record_us:%llu,p95_record_us:%llu,"
        "median_submit_us:%llu,p95_submit_us:%llu,"
        "p95_record_plus_submit_us:%llu,p95_completion_us:%llu\n",
        adapter, kBuildConfiguration, kDrawCount, kWarmupCount, kSampleCount,
        static_cast<unsigned long long>(kFrameBudgetMicroseconds),
        static_cast<unsigned long long>(Median(dynamicTimings.recordMicroseconds)),
        static_cast<unsigned long long>(Percentile95(dynamicTimings.recordMicroseconds)),
        static_cast<unsigned long long>(Median(dynamicTimings.submitMicroseconds)),
        static_cast<unsigned long long>(Percentile95(dynamicTimings.submitMicroseconds)),
        static_cast<unsigned long long>(RecordingAndSubmitP95(dynamicTimings)),
        static_cast<unsigned long long>(Percentile95(dynamicTimings.completionMicroseconds)));
    if (written < 0)
    {
        std::fclose(file);
        return false;
    }
    if (hasBindless)
    {
        const int bindlessWritten = std::fprintf(
            file,
            "bindless_path=median_record_us:%llu,p95_record_us:%llu,"
            "median_submit_us:%llu,p95_submit_us:%llu,"
            "p95_record_plus_submit_us:%llu,p95_completion_us:%llu\n",
            static_cast<unsigned long long>(Median(bindlessTimings.recordMicroseconds)),
            static_cast<unsigned long long>(Percentile95(bindlessTimings.recordMicroseconds)),
            static_cast<unsigned long long>(Median(bindlessTimings.submitMicroseconds)),
            static_cast<unsigned long long>(Percentile95(bindlessTimings.submitMicroseconds)),
            static_cast<unsigned long long>(RecordingAndSubmitP95(bindlessTimings)),
            static_cast<unsigned long long>(Percentile95(bindlessTimings.completionMicroseconds)));
        if (bindlessWritten < 0)
        {
            std::fclose(file);
            return false;
        }
    }
    else if (std::fprintf(file, "bindless_path=unsupported\n") < 0)
    {
        std::fclose(file);
        return false;
    }
    if (std::fclose(file) != 0)
    {
        std::printf("Could not flush phase19-submission-report.txt.\n");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    bool useWarp = false;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        if (std::strcmp(argv[argumentIndex], "--warp") == 0)
        {
            useWarp = true;
        }
        else
        {
            std::printf("Unknown argument: %s\nUsage: cyber_sample_frame_streaming [--warp]\n",
                        argv[argumentIndex]);
            return 1;
        }
    }

    HeapAllocator heap;
    const cyber::rhi::Backend* backend = cyber::rhi::D3D12Backend();
    const u32 adapterCount = backend != nullptr ? backend->enumerateAdapters(nullptr, 0) : 0;
    if (adapterCount == 0)
    {
        std::printf("No D3D12 backend or adapter is available.\n");
        return 1;
    }

    DeviceDesc deviceDesc{};
    deviceDesc.backend = backend;
    deviceDesc.adapterIndex = useWarp ? adapterCount - 1 : 0;
    auto createdDevice = Device::Create(deviceDesc, heap);
    if (!createdDevice)
    {
        PrintFailure("Device::Create", createdDevice.Error());
        return 1;
    }
    Device device = std::move(createdDevice.Value());
    const bool hasBindless =
        device.Capabilities().features.Has(cyber::rhi::Feature::BindlessResources);
    std::printf("Adapter: %s (%s selection)\n", device.Capabilities().adapter.name,
                useWarp ? "WARP" : "first enumerated");
    if (!hasBindless)
    {
        std::printf("Bindless indexing is unsupported; only the conventional path will run.\n");
    }

    auto fenceResult = device.CreateFence(cyber::rhi::FenceDesc{});
    if (!fenceResult)
    {
        PrintFailure("CreateFence", fenceResult.Error());
        return 1;
    }
    const FenceHandle fence = fenceResult.Value();
    FrameRingDesc ringDesc{};
    ringDesc.fence = fence;
    ringDesc.framesInFlight = 1;
    auto ringResult = FrameRing::Create(ringDesc);
    if (!ringResult)
    {
        PrintFailure("FrameRing::Create", ringResult.Error());
        return 1;
    }
    FrameRing ring = std::move(ringResult.Value());

    cyber::rhi::TextureDesc targetDesc{};
    targetDesc.format = cyber::rhi::Format::RGBA8Unorm;
    targetDesc.usage = cyber::rhi::TextureUsage::RenderTarget;
    targetDesc.width = kTargetExtent;
    targetDesc.height = kTargetExtent;
    targetDesc.label = "phase 19 benchmark target";
    auto targetResult = device.CreateTexture(targetDesc);
    if (!targetResult)
    {
        PrintFailure("CreateTexture target", targetResult.Error());
        return 1;
    }
    const TextureHandle target = targetResult.Value();
    cyber::rhi::TextureViewDesc targetViewDesc{};
    targetViewDesc.texture = target;
    targetViewDesc.format = targetDesc.format;
    auto targetViewResult = device.CreateTextureView(targetViewDesc);
    if (!targetViewResult)
    {
        PrintFailure("CreateTextureView target", targetViewResult.Error());
        return 1;
    }
    const TextureViewHandle targetView = targetViewResult.Value();

    BufferHandle vertices{};
    if (!UploadTriangle(device, vertices))
    {
        return 1;
    }

    auto* transforms =
        static_cast<Transform*>(heap.Allocate(sizeof(Transform) * kDrawCount, alignof(Transform)));
    auto* dynamicOffsets = static_cast<u64*>(heap.Allocate(sizeof(u64) * kDrawCount, alignof(u64)));
    if (transforms == nullptr || dynamicOffsets == nullptr)
    {
        std::printf("Could not allocate benchmark CPU data.\n");
        heap.Deallocate(transforms);
        heap.Deallocate(dynamicOffsets);
        return 1;
    }
    FillTransforms(transforms);

    FrameBufferStreamDesc dynamicStreamDesc{};
    dynamicStreamDesc.framesInFlight = 1;
    dynamicStreamDesc.capacityBytes = kUniformStreamCapacity;
    dynamicStreamDesc.maxAllocationsPerFrame = kDrawCount;
    dynamicStreamDesc.bindingType = cyber::rhi::BindingType::UniformBuffer;
    auto dynamicStreamResult = FrameBufferStream::Create(device, heap, dynamicStreamDesc);
    if (!dynamicStreamResult)
    {
        PrintFailure("Create dynamic stream", dynamicStreamResult.Error());
        return 1;
    }
    FrameBufferStream dynamicStream = std::move(dynamicStreamResult.Value());

    cyber::rhi::BindGroupLayoutEntry dynamicEntry{};
    dynamicEntry.binding = 0;
    dynamicEntry.type = cyber::rhi::BindingType::UniformBuffer;
    dynamicEntry.visibility = cyber::rhi::ShaderStageBit(cyber::rhi::ShaderStage::Vertex);
    dynamicEntry.dynamicOffset = true;
    cyber::rhi::BindGroupLayoutDesc dynamicLayoutDesc{};
    dynamicLayoutDesc.entries = Span<const cyber::rhi::BindGroupLayoutEntry>(&dynamicEntry, 1);
    auto dynamicGroupLayoutResult = device.CreateBindGroupLayout(dynamicLayoutDesc);
    if (!dynamicGroupLayoutResult)
    {
        PrintFailure("Create dynamic bind-group layout", dynamicGroupLayoutResult.Error());
        return 1;
    }
    const BindGroupLayoutHandle dynamicGroupLayout = dynamicGroupLayoutResult.Value();
    PipelineResources dynamicPipeline{};
    if (!CreatePipeline(device, dynamicGroupLayout, false, dynamicPipeline))
    {
        return 1;
    }

    cyber::rhi::BindingResource dynamicResource{};
    dynamicResource.type = cyber::rhi::BindingType::UniformBuffer;
    dynamicResource.buffer = dynamicStream.DestinationBuffer(0);
    dynamicResource.size = sizeof(Transform);
    auto dynamicGroupResult = device.CreateBindGroup(cyber::rhi::BindGroupDesc{
        dynamicGroupLayout, Span<const cyber::rhi::BindingResource>(&dynamicResource, 1),
        "per-draw dynamic transform"});
    if (!dynamicGroupResult)
    {
        PrintFailure("Create dynamic bind group", dynamicGroupResult.Error());
        return 1;
    }
    const BindGroupHandle dynamicGroup = dynamicGroupResult.Value();

    TimingSamples dynamicTimings{};
    bool targetReady = false;
    if (!RunPath(device, ring, fence, dynamicStream, false, targetReady, target, targetView,
                 vertices, dynamicPipeline.pipeline, dynamicGroup, nullptr, transforms,
                 dynamicOffsets, dynamicTimings))
    {
        return 1;
    }
    PrintTimings("Dynamic offsets", dynamicTimings);

    TimingSamples bindlessTimings{};
    BindlessResourceTable table;
    FrameBufferStream objectStream;
    PipelineResources bindlessPipeline{};
    TextureHandle fallbackTexture{};
    TextureViewHandle fallbackView{};
    cyber::rhi::SamplerHandle bindlessSampler{};
    if (hasBindless)
    {
        FrameBufferStreamDesc objectStreamDesc{};
        objectStreamDesc.framesInFlight = 1;
        objectStreamDesc.capacityBytes = kObjectStreamCapacity;
        objectStreamDesc.maxAllocationsPerFrame = 1;
        objectStreamDesc.bindingType = cyber::rhi::BindingType::ReadOnlyStorageBuffer;
        objectStreamDesc.elementStride = sizeof(Transform);
        auto objectStreamResult = FrameBufferStream::Create(device, heap, objectStreamDesc);
        if (!objectStreamResult)
        {
            PrintFailure("Create bindless object stream", objectStreamResult.Error());
            return 1;
        }
        objectStream = std::move(objectStreamResult.Value());

        cyber::rhi::TextureDesc fallbackTextureDesc{};
        fallbackTextureDesc.format = cyber::rhi::Format::RGBA8Unorm;
        fallbackTextureDesc.usage = cyber::rhi::TextureUsage::Sampled;
        fallbackTextureDesc.width = 1;
        fallbackTextureDesc.height = 1;
        auto fallbackTextureResult = device.CreateTexture(fallbackTextureDesc);
        if (!fallbackTextureResult)
        {
            PrintFailure("Create fallback texture", fallbackTextureResult.Error());
            return 1;
        }
        fallbackTexture = fallbackTextureResult.Value();
        cyber::rhi::TextureViewDesc fallbackViewDesc{};
        fallbackViewDesc.texture = fallbackTexture;
        fallbackViewDesc.format = cyber::rhi::Format::RGBA8Unorm;
        auto fallbackViewResult = device.CreateTextureView(fallbackViewDesc);
        if (!fallbackViewResult)
        {
            PrintFailure("Create fallback texture view", fallbackViewResult.Error());
            return 1;
        }
        fallbackView = fallbackViewResult.Value();
        auto samplerResult = device.CreateSampler(cyber::rhi::SamplerDesc{});
        if (!samplerResult)
        {
            PrintFailure("Create bindless sampler", samplerResult.Error());
            return 1;
        }
        bindlessSampler = samplerResult.Value();

        BindlessResourceTableDesc tableDesc{};
        tableDesc.capacity = kDrawCount;
        tableDesc.framesInFlight = 1;
        tableDesc.storageElementStride = sizeof(Transform);
        tableDesc.fallbackTexture = fallbackView;
        tableDesc.fallbackBuffer = objectStream.DestinationBuffer(0);
        tableDesc.sampler = bindlessSampler;
        auto tableResult = BindlessResourceTable::Create(device, heap, tableDesc);
        if (!tableResult)
        {
            PrintFailure("Create bindless resource table", tableResult.Error());
            return 1;
        }
        table = std::move(tableResult.Value());

        for (u32 index = 0; index < kDrawCount; ++index)
        {
            auto descriptor = table.AllocateBuffer(objectStream.DestinationBuffer(0),
                                                   static_cast<u64>(index) * sizeof(Transform),
                                                   sizeof(Transform));
            if (!descriptor || descriptor.Value().index != index)
            {
                if (!descriptor)
                {
                    PrintFailure("Allocate bindless object descriptor", descriptor.Error());
                }
                else
                {
                    std::printf("Bindless object indices are not contiguous.\n");
                }
                return 1;
            }
        }

        if (!CreatePipeline(device, table.Layout(), true, bindlessPipeline))
        {
            return 1;
        }
        if (!RunPath(device, ring, fence, objectStream, true, targetReady, target, targetView,
                     vertices, bindlessPipeline.pipeline, table.Group(0), &table, transforms,
                     dynamicOffsets, bindlessTimings))
        {
            return 1;
        }
        PrintTimings("Bindless indices", bindlessTimings);
    }

    if (auto idle = device.WaitIdle(); !idle)
    {
        PrintFailure("WaitIdle benchmark cleanup", idle.Error());
        return 1;
    }
    const char* adapter = device.Capabilities().adapter.name;
    if (!WriteReport(adapter, dynamicTimings, bindlessTimings, hasBindless))
    {
        std::printf("Could not write the Phase 19 timing report.\n");
        return 1;
    }
    std::printf("Wrote phase19-submission-report.txt\n");

    bool cleanupSucceeded = true;
    cleanupSucceeded =
        ReportDestruction("Destroy dynamic bind group", device.DestroyBindGroup(dynamicGroup)) &&
        cleanupSucceeded;
    cleanupSucceeded =
        ReportDestruction("Destroy dynamic pipeline",
                          device.DestroyGraphicsPipeline(dynamicPipeline.pipeline)) &&
        cleanupSucceeded;
    cleanupSucceeded = ReportDestruction("Destroy dynamic pipeline layout",
                                         device.DestroyPipelineLayout(dynamicPipeline.layout)) &&
                       cleanupSucceeded;
    cleanupSucceeded = ReportDestruction("Destroy dynamic vertex shader",
                                         device.DestroyShader(dynamicPipeline.vertex)) &&
                       cleanupSucceeded;
    cleanupSucceeded = ReportDestruction("Destroy dynamic pixel shader",
                                         device.DestroyShader(dynamicPipeline.pixel)) &&
                       cleanupSucceeded;
    if (bindlessPipeline.pipeline.IsValid())
    {
        cleanupSucceeded =
            ReportDestruction("Destroy bindless pipeline",
                              device.DestroyGraphicsPipeline(bindlessPipeline.pipeline)) &&
            cleanupSucceeded;
        cleanupSucceeded =
            ReportDestruction("Destroy bindless pipeline layout",
                              device.DestroyPipelineLayout(bindlessPipeline.layout)) &&
            cleanupSucceeded;
        cleanupSucceeded = ReportDestruction("Destroy bindless vertex shader",
                                             device.DestroyShader(bindlessPipeline.vertex)) &&
                           cleanupSucceeded;
        cleanupSucceeded = ReportDestruction("Destroy bindless pixel shader",
                                             device.DestroyShader(bindlessPipeline.pixel)) &&
                           cleanupSucceeded;
    }
    cleanupSucceeded =
        ReportDestruction("Destroy bindless table", table.Destroy()) && cleanupSucceeded;
    cleanupSucceeded =
        ReportDestruction("Destroy dynamic stream", dynamicStream.Destroy()) && cleanupSucceeded;
    cleanupSucceeded =
        ReportDestruction("Destroy object stream", objectStream.Destroy()) && cleanupSucceeded;
    cleanupSucceeded = ReportDestruction("Destroy dynamic bind-group layout",
                                         device.DestroyBindGroupLayout(dynamicGroupLayout)) &&
                       cleanupSucceeded;
    if (bindlessSampler.IsValid())
    {
        cleanupSucceeded =
            ReportDestruction("Destroy fallback sampler", device.DestroySampler(bindlessSampler)) &&
            cleanupSucceeded;
        cleanupSucceeded =
            ReportDestruction("Destroy fallback view", device.DestroyTextureView(fallbackView)) &&
            cleanupSucceeded;
        cleanupSucceeded =
            ReportDestruction("Destroy fallback texture", device.DestroyTexture(fallbackTexture)) &&
            cleanupSucceeded;
    }
    cleanupSucceeded = ReportDestruction("Destroy vertex buffer", device.DestroyBuffer(vertices)) &&
                       cleanupSucceeded;
    cleanupSucceeded =
        ReportDestruction("Destroy target view", device.DestroyTextureView(targetView)) &&
        cleanupSucceeded;
    cleanupSucceeded = ReportDestruction("Destroy target texture", device.DestroyTexture(target)) &&
                       cleanupSucceeded;
    heap.Deallocate(transforms);
    heap.Deallocate(dynamicOffsets);
    cleanupSucceeded =
        ReportDestruction("Destroy frame fence", device.DestroyFence(fence)) && cleanupSucceeded;
    return cleanupSucceeded ? 0 : 1;
}
