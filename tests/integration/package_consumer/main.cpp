#include <cyber/core/build_config.h>
#include <cyber/core/error.h>
#include <cyber/core/hash.h>
#include <cyber/core/log.h>
#include <cyber/core/result.h>
#include <cyber/core/version.h>
#include <cyber/jobs/jobs.h>
#include <cyber/math/math.h>
#include <cyber/memory/memory.h>
#include <cyber/perf/perf.h>
#include <cyber/platform/platform.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/rhi.h>
#include <cyber/software/raster.h>
#include <cyber/software/software.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string_view>
#include <utility>

namespace
{

struct ConsumerCounter
{
    std::atomic<unsigned> value{0};
};

void ConsumerIncrement(void* userData) noexcept
{
    static_cast<ConsumerCounter*>(userData)->value.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

int main()
{
    using namespace cyber::core;

    const Version version = VersionNumber();
    if (version.major != kVersionMajor)
    {
        return 1;
    }

    const char* text = VersionString();
    if (text == nullptr)
    {
        return 2;
    }

    if (std::strcmp(text, kVersionString) != 0)
    {
        return 3;
    }

    if (BuildConfig::kVersion != std::string_view(kVersionString))
    {
        return 4;
    }

    auto result = Result<int, Error>::Ok(21);
    if (!result.IsOk() || result.Value() != 21)
    {
        return 5;
    }

    auto failed = Result<void, Error>::Err(Error{ErrorCode::NotFound, "missing"});
    if (!failed.IsError() || failed.Error().code != ErrorCode::NotFound)
    {
        return 6;
    }

    if (HashString("abc") != 0x44BC2CF5AD770999ULL)
    {
        return 7;
    }

    Logger logger;
    logger.SetLevel(LogLevel::Info);
    if (!logger.IsEnabled(LogChannel::Core, LogLevel::Info))
    {
        return 8;
    }

    using namespace cyber::math;

    const Vec3 cross = Cross(Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    if (LengthSquared(cross - Vec3{0.0f, 0.0f, 1.0f}) > 1.0e-6f)
    {
        return 9;
    }

    const Mat4 transform = Mat4::Translation(Vec3{1.0f, 2.0f, 3.0f}) * Mat4::RotationZ(kHalfPi) *
                           Mat4::Scale(Vec3{2.0f, 2.0f, 2.0f});
    Mat4 inverse;
    if (!transform.TryInverse(inverse))
    {
        return 10;
    }
    const Vec3 origin = Vec3::Zero();
    if (LengthSquared(inverse.TransformPoint(transform.TransformPoint(origin)) - origin) > 1.0e-5f)
    {
        return 11;
    }

    const Quat rotation = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
    if (!ApproxEqual(rotation.Rotate(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}, 1.0e-5f))
    {
        return 12;
    }

    if (Half::FromFloat(0.5f).Bits() != static_cast<u16>(0x3800))
    {
        return 13;
    }

    const Mat4 projection = Mat4::PerspectiveReverseZ(kHalfPi, 1.0f, 0.1f, 10.0f);
    if (std::abs(projection.TransformPoint(Vec3{0.0f, 0.0f, -0.1f}).z - 1.0f) > 1.0e-5f)
    {
        return 14;
    }

    const Aabb box = Aabb::FromMinMax(Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f});
    if (!box.Contains(Vec3::Zero()))
    {
        return 15;
    }

    using namespace cyber::memory;

    HeapAllocator heap;
    DynamicArray<int> values(heap);
    if (!values.TryPushBack(7) || values[0] != 7)
    {
        return 16;
    }

    HashMap<StringView, int> lookup(heap);
    if (!lookup.TryInsert(StringView("key"), 42) || *lookup.Find("key") != 42)
    {
        return 17;
    }

    SlotMap<int> slots(heap);
    const Handle<int> handle = slots.Insert(9);
    if (!handle.IsValid() || *slots.Get(handle) != 9)
    {
        return 18;
    }

    using namespace cyber::platform;

    if (Clock::NowSeconds() <= 0.0)
    {
        return 19;
    }

    FrameTimer timer;
    if (timer.Tick() != 0.0 || timer.FrameCount() != 1)
    {
        return 20;
    }

    cyber::perf::Profiler& profiler = cyber::perf::NullProfilerInstance();
    profiler.BeginScope("consumer");
    profiler.EndScope();
    profiler.Counter("consumer", 1.0);

    using namespace cyber::jobs;

    JobSystem::Desc desc{};
    desc.workerCount = 1;
    auto created = JobSystem::Create(desc, heap);
    if (!created)
    {
        return 21;
    }
    JobSystem jobs = std::move(created.Value());

    ConsumerCounter counter;
    JobDesc job{};
    job.name = "consumer";
    job.function = &ConsumerIncrement;
    job.userData = &counter;
    if (!jobs.SubmitDetached(job))
    {
        return 22;
    }
    jobs.WaitIdle();
    if (counter.value.load() != 1)
    {
        return 23;
    }

    jobs.Shutdown();
    if (jobs.IsRunning())
    {
        return 24;
    }

    {
        using namespace cyber::software;

        FramebufferDesc framebufferDesc{};
        framebufferDesc.width = 2;
        framebufferDesc.height = 2;
        auto createdFramebuffer = Framebuffer::Create(framebufferDesc, heap);
        if (!createdFramebuffer)
        {
            return 25;
        }
        Framebuffer framebuffer = std::move(createdFramebuffer.Value());
        framebuffer.Clear(cyber::math::Color{0.5f, 0.5f, 0.5f, 1.0f});

        u8 packed[16] = {};
        if (!PackPresentPixels(framebuffer, Span<u8>(packed, sizeof(packed))))
        {
            return 26;
        }
        if (packed[0] != 188 || packed[3] != 255)
        {
            return 27;
        }

        auto encoded = EncodeImage(framebuffer, ImageWriteDesc{}, heap);
        if (!encoded || encoded.Value().Size() != 23)
        {
            return 28;
        }

        RasterTriangle triangle{};
        triangle.v0 = RasterVertex{Vec2{-1.0f, -1.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f}};
        triangle.v1 = RasterVertex{Vec2{5.0f, -1.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f}};
        triangle.v2 = RasterVertex{Vec2{-1.0f, 5.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f}};
        RasterStats rasterStats{};
        if (!RasterizeTriangles(framebuffer, Span<const RasterTriangle>(&triangle, 1), nullptr,
                                heap, {}, &rasterStats) ||
            rasterStats.pixels != 4)
        {
            return 31;
        }

        // Phase 8: the vertex pipeline clips, culls, and depth-tests through the
        // same installed target.
        DepthBufferDesc depthDesc{};
        depthDesc.width = framebuffer.Width();
        depthDesc.height = framebuffer.Height();
        auto createdDepth = DepthBuffer::Create(depthDesc, heap);
        if (!createdDepth)
        {
            return 32;
        }
        DepthBuffer depth = std::move(createdDepth.Value());
        depth.Clear(0.0f);

        const Vertex quad[6] = {
            Vertex{cyber::math::Vec3{-1.0f, -1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{1.0f, -1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{1.0f, 1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{-1.0f, -1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{1.0f, 1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
            Vertex{cyber::math::Vec3{-1.0f, 1.0f, 0.5f}, Color{0.0f, 1.0f, 0.0f, 1.0f}},
        };
        PipelineDesc pipelineDesc{};
        pipelineDesc.viewport.width = 2.0f;
        pipelineDesc.viewport.height = 2.0f;
        PipelineStats pipelineStats{};
        if (!DrawTriangles(pipelineDesc, Span<const Vertex>(quad), framebuffer, depth, nullptr,
                           heap, &pipelineStats) ||
            pipelineStats.pixels != 4 || depth.Get(1, 1) != 0.5f)
        {
            return 33;
        }

        FramePacer pacer(60);
        if (pacer.TargetFramesPerSecond() != 60)
        {
            return 29;
        }
        if (!IsGlyphSupported('A') || MeasureText("AB").width != 11)
        {
            return 30;
        }
    }

    // Phase 10: the RHI object model and the null backend arrive through the
    // installed package, including the exported headers and the static library.
    {
        using namespace cyber::rhi;

        const Backend* backend = NullBackend();
        if (backend == nullptr || backend->kind != BackendKind::Null)
        {
            return 34;
        }

        AdapterInfo adapters[2] = {};
        if (EnumerateAdapters(*backend, Span<AdapterInfo>(adapters, 2)) != 1 ||
            !BackendUsesZeroToOneDepth(adapters[0].backend) ||
            !BackendUsesTopLeftTextureOrigin(adapters[0].backend))
        {
            return 35;
        }
        if (FormatName(Format::RGBA8UnormSrgb) == nullptr || !IsSrgbFormat(Format::RGBA8UnormSrgb))
        {
            return 42;
        }

        DeviceDesc deviceDesc{};
        deviceDesc.backend = backend;
        auto createdDevice = Device::Create(deviceDesc, heap);
        if (!createdDevice)
        {
            return 36;
        }
        Device device = std::move(createdDevice.Value());
        if (!device.Queue(QueueKind::Graphics).IsValid())
        {
            return 37;
        }
        if (!device.Capabilities().features.Has(Feature::TimelineFences))
        {
            return 38;
        }

        auto createdFence = device.CreateFence(FenceDesc{});
        if (!createdFence)
        {
            return 39;
        }
        const FenceHandle fence = createdFence.Value();
        if (!device.SignalFence(fence, 2).IsOk() || !device.WaitFence(fence, 2, 0).IsOk())
        {
            return 40;
        }
        if (device.FenceValue(fence) != 2 || !device.DestroyFence(fence).IsOk())
        {
            return 41;
        }
    }

    // Phase 11: resources through the installed package: a buffer with host
    // access, a texture and a view, a sampler, and a barrier checked against
    // tracked state.
    {
        using namespace cyber::rhi;
        using cyber::memory::Byte;

        DeviceDesc deviceDesc{};
        deviceDesc.backend = NullBackend();
        auto createdDevice = Device::Create(deviceDesc, heap);
        if (!createdDevice)
        {
            return 43;
        }
        Device device = std::move(createdDevice.Value());

        BufferDesc bufferDesc{};
        bufferDesc.usage = BufferUsage::HostVisible | BufferUsage::CopySource;
        bufferDesc.sizeBytes = 32;
        auto createdBuffer = device.CreateBuffer(bufferDesc);
        if (!createdBuffer)
        {
            return 44;
        }
        const BufferHandle buffer = createdBuffer.Value();

        const Byte pattern[4] = {static_cast<Byte>(1), static_cast<Byte>(2), static_cast<Byte>(3),
                                 static_cast<Byte>(4)};
        Byte read[4] = {};
        if (!device.WriteBuffer(buffer, 8, Span<const Byte>(pattern, 4)).IsOk() ||
            !device.ReadBuffer(buffer, 8, Span<Byte>(read, 4)).IsOk() || read[0] != pattern[0] ||
            read[3] != pattern[3])
        {
            return 45;
        }

        TextureDesc textureDesc{};
        textureDesc.format = Format::RGBA8Unorm;
        textureDesc.usage = TextureUsage::Sampled | TextureUsage::RenderTarget;
        textureDesc.width = 64;
        textureDesc.height = 64;
        textureDesc.mipLevels = 7;
        auto createdTexture = device.CreateTexture(textureDesc);
        if (!createdTexture)
        {
            return 46;
        }
        const TextureHandle texture = createdTexture.Value();
        if (device.TextureInfoOf(texture).sizeBytes == 0 ||
            device.TextureInfoOf(texture).mipLevels != 7)
        {
            return 47;
        }

        TextureViewDesc viewDesc{};
        viewDesc.texture = texture;
        viewDesc.format = Format::RGBA8UnormSrgb;
        auto createdView = device.CreateTextureView(viewDesc);
        if (!createdView || device.TextureViewInfoOf(createdView.Value()).range.mipLevelCount != 7)
        {
            return 48;
        }

        SamplerDesc samplerDesc{};
        samplerDesc.maxAnisotropy = 4;
        auto createdSampler = device.CreateSampler(samplerDesc);
        if (!createdSampler)
        {
            return 49;
        }

        TextureBarrier barrier{};
        barrier.texture = texture;
        barrier.from = ResourceState::Undefined;
        barrier.to = ResourceState::ShaderRead;
        if (!device.Transition({}, Span<const TextureBarrier>(&barrier, 1)).IsOk() ||
            device.TextureStateOf(texture, 0, 0) != ResourceState::ShaderRead)
        {
            return 50;
        }
        // The tracked state is ShaderRead. A barrier that claims another source
        // is the missing-barrier bug and must be rejected.
        barrier.from = ResourceState::CopySource;
        barrier.to = ResourceState::ShaderRead;
        if (device.Transition({}, Span<const TextureBarrier>(&barrier, 1)).IsOk())
        {
            return 51;
        }

        if (!device.DestroySampler(createdSampler.Value()).IsOk() ||
            !device.DestroyTextureView(createdView.Value()).IsOk() ||
            !device.DestroyTexture(texture).IsOk() || !device.DestroyBuffer(buffer).IsOk())
        {
            return 52;
        }
        if (device.TextureInfoOf(texture).valid || device.BufferInfoOf(buffer).valid)
        {
            return 53;
        }
    }

    // Phase 12: shader modules, the binding model, and a graphics pipeline,
    // including the description hash a pipeline cache is keyed by.
    {
        using namespace cyber::rhi;
        using cyber::memory::Byte;

        DeviceDesc deviceDesc{};
        deviceDesc.backend = NullBackend();
        auto createdDevice = Device::Create(deviceDesc, heap);
        if (!createdDevice)
        {
            return 54;
        }
        Device device = std::move(createdDevice.Value());

        const char source[] =
            "float4 main(float3 p : TEXCOORD0) : SV_Position { return float4(p, 1); }";
        ShaderDesc shaderDesc{};
        shaderDesc.stage = ShaderStage::Vertex;
        shaderDesc.format = ShaderBytecodeFormat::HlslSource;
        shaderDesc.code =
            Span<const Byte>(reinterpret_cast<const Byte*>(source), sizeof(source) - 1);
        shaderDesc.entryPoint = "main";
        auto vertexShader = device.CreateShader(shaderDesc);
        if (!vertexShader)
        {
            return 55;
        }
        shaderDesc.stage = ShaderStage::Pixel;
        auto pixelShader = device.CreateShader(shaderDesc);
        if (!pixelShader)
        {
            return 56;
        }

        BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.type = BindingType::UniformBuffer;
        entry.visibility = ShaderStageBit(ShaderStage::Vertex);
        BindGroupLayoutDesc groupLayoutDesc{};
        groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
        auto groupLayout = device.CreateBindGroupLayout(groupLayoutDesc);
        if (!groupLayout)
        {
            return 57;
        }

        PipelineLayoutDesc pipelineLayoutDesc{};
        pipelineLayoutDesc.groups[static_cast<cyber::core::u32>(BindGroupSlot::PerMaterial)] =
            groupLayout.Value();
        auto pipelineLayout = device.CreatePipelineLayout(pipelineLayoutDesc);
        if (!pipelineLayout)
        {
            return 58;
        }

        BufferDesc uniformDesc{};
        uniformDesc.usage = BufferUsage::Uniform;
        uniformDesc.sizeBytes = 256;
        auto uniforms = device.CreateBuffer(uniformDesc);
        if (!uniforms)
        {
            return 59;
        }

        BindingResource binding{};
        binding.type = BindingType::UniformBuffer;
        binding.buffer = uniforms.Value();
        BindGroupDesc bindGroupDesc{};
        bindGroupDesc.layout = groupLayout.Value();
        bindGroupDesc.resources = Span<const BindingResource>(&binding, 1);
        auto bindGroup = device.CreateBindGroup(bindGroupDesc);
        if (!bindGroup || device.BindGroupInfoOf(bindGroup.Value()).resourceCount != 1)
        {
            return 60;
        }

        VertexBinding vertexBinding{};
        vertexBinding.strideBytes = 12;
        VertexAttribute attribute{};
        attribute.location = 0;
        attribute.binding = 0;
        attribute.format = Format::R32G32B32Float;
        ColorAttachmentState attachment{};
        attachment.format = Format::RGBA8Unorm;

        GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.layout = pipelineLayout.Value();
        pipelineDesc.vertexShader = vertexShader.Value();
        pipelineDesc.pixelShader = pixelShader.Value();
        pipelineDesc.vertexLayout.bindings = Span<const VertexBinding>(&vertexBinding, 1);
        pipelineDesc.vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
        pipelineDesc.depthStencilFormat = Format::D32Float;
        pipelineDesc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);

        auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
        if (!pipeline || !device.PipelineInfoOf(pipeline.Value()).valid ||
            device.PipelineInfoOf(pipeline.Value()).hash == 0 ||
            device.PipelineInfoOf(pipeline.Value()).hash != HashGraphicsPipelineDesc(pipelineDesc))
        {
            return 61;
        }

        if (!device.DestroyGraphicsPipeline(pipeline.Value()).IsOk() ||
            !device.DestroyBindGroup(bindGroup.Value()).IsOk() ||
            !device.DestroyBuffer(uniforms.Value()).IsOk() ||
            !device.DestroyPipelineLayout(pipelineLayout.Value()).IsOk() ||
            !device.DestroyBindGroupLayout(groupLayout.Value()).IsOk() ||
            !device.DestroyShader(vertexShader.Value()).IsOk() ||
            !device.DestroyShader(pixelShader.Value()).IsOk())
        {
            return 62;
        }
    }

    // Phase 13: a recorded frame through the installed package. A command list
    // records barriers, a render pass, and a draw; a submission runs it and
    // signals a fence; a swapchain hands out an image; the frame ring decides
    // which value the frame must signal; the retirement queue holds a texture
    // until that value is reached.
    {
        using namespace cyber::rhi;
        using cyber::memory::Byte;

        DeviceDesc deviceDesc{};
        deviceDesc.backend = NullBackend();
        auto createdDevice = Device::Create(deviceDesc, heap);
        if (!createdDevice)
        {
            return 63;
        }
        Device device = std::move(createdDevice.Value());

        auto createdFence = device.CreateFence(FenceDesc{});
        if (!createdFence)
        {
            return 64;
        }
        const FenceHandle fence = createdFence.Value();

        const char source[] = "void main() {}";
        ShaderDesc shaderDesc{};
        shaderDesc.stage = ShaderStage::Vertex;
        shaderDesc.code =
            Span<const Byte>(reinterpret_cast<const Byte*>(source), sizeof(source) - 1);
        shaderDesc.entryPoint = "main";
        auto vertexShader = device.CreateShader(shaderDesc);
        shaderDesc.stage = ShaderStage::Pixel;
        auto pixelShader = device.CreateShader(shaderDesc);
        if (!vertexShader || !pixelShader)
        {
            return 65;
        }

        BindGroupLayoutEntry entry{};
        entry.type = BindingType::UniformBuffer;
        entry.visibility = ShaderStageBit(ShaderStage::Vertex);
        BindGroupLayoutDesc groupLayoutDesc{};
        groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
        auto groupLayout = device.CreateBindGroupLayout(groupLayoutDesc);

        PipelineLayoutDesc pipelineLayoutDesc{};
        pipelineLayoutDesc.groups[static_cast<cyber::core::u32>(BindGroupSlot::PerMaterial)] =
            groupLayout.Value();
        auto pipelineLayout = device.CreatePipelineLayout(pipelineLayoutDesc);

        BufferDesc uniformDesc{};
        uniformDesc.usage = BufferUsage::Uniform;
        uniformDesc.sizeBytes = 256;
        auto uniforms = device.CreateBuffer(uniformDesc);
        BindingResource binding{};
        binding.type = BindingType::UniformBuffer;
        binding.buffer = uniforms.Value();
        binding.size = 256;
        BindGroupDesc bindGroupDesc{};
        bindGroupDesc.layout = groupLayout.Value();
        bindGroupDesc.resources = Span<const BindingResource>(&binding, 1);
        auto bindGroup = device.CreateBindGroup(bindGroupDesc);
        if (!groupLayout || !pipelineLayout || !uniforms || !bindGroup)
        {
            return 66;
        }

        VertexBinding vertexBinding{};
        vertexBinding.strideBytes = 12;
        VertexAttribute attribute{};
        attribute.format = Format::R32G32B32Float;
        ColorAttachmentState attachment{};
        attachment.format = Format::RGBA8Unorm;
        GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.layout = pipelineLayout.Value();
        pipelineDesc.vertexShader = vertexShader.Value();
        pipelineDesc.pixelShader = pixelShader.Value();
        pipelineDesc.vertexLayout.bindings = Span<const VertexBinding>(&vertexBinding, 1);
        pipelineDesc.vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
        pipelineDesc.depthStencilFormat = Format::D32Float;
        pipelineDesc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
        auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
        if (!pipeline)
        {
            return 67;
        }

        BufferDesc vertexDesc{};
        vertexDesc.usage = BufferUsage::Vertex;
        vertexDesc.sizeBytes = 256;
        auto vertices = device.CreateBuffer(vertexDesc);

        TextureDesc colorDesc{};
        colorDesc.format = Format::RGBA8Unorm;
        colorDesc.usage = TextureUsage::RenderTarget;
        colorDesc.width = 64;
        colorDesc.height = 64;
        auto color = device.CreateTexture(colorDesc);
        TextureViewDesc colorViewDesc{};
        colorViewDesc.texture = color.Value();
        colorViewDesc.format = Format::RGBA8Unorm;
        auto colorView = device.CreateTextureView(colorViewDesc);

        TextureDesc depthDesc{};
        depthDesc.format = Format::D32Float;
        depthDesc.usage = TextureUsage::DepthStencil;
        depthDesc.width = 64;
        depthDesc.height = 64;
        auto depth = device.CreateTexture(depthDesc);
        TextureViewDesc depthViewDesc{};
        depthViewDesc.texture = depth.Value();
        depthViewDesc.format = Format::D32Float;
        depthViewDesc.range.aspect = TextureAspect::Depth;
        auto depthView = device.CreateTextureView(depthViewDesc);
        if (!vertices || !color || !colorView || !depth || !depthView)
        {
            return 68;
        }

        // The value a frame ring would use: frame 0 signals 1.
        FrameRingDesc ringDesc{};
        ringDesc.fence = fence;
        ringDesc.framesInFlight = 2;
        auto createdRing = FrameRing::Create(ringDesc);
        if (!createdRing)
        {
            return 69;
        }
        FrameRing ring = createdRing.Value();
        const auto frame0 = ring.BeginFrame(device, 0);
        if (!frame0 || frame0.Value().slot != 0 || frame0.Value().signalValue != 1)
        {
            return 69;
        }
        const auto frame1 = ring.BeginFrame(device, 0);
        if (!frame1 || frame1.Value().slot != 1 || frame1.Value().signalValue != 2)
        {
            return 70;
        }

        CommandListDesc listDesc{};
        listDesc.queue = QueueKind::Graphics;
        auto createdList = device.CreateCommandList(listDesc);
        if (!createdList)
        {
            return 71;
        }
        CommandList list = std::move(createdList.Value());

        const BufferBarrier bufferBarriers[2] = {
            BufferBarrier{vertices.Value(), ResourceState::Undefined, ResourceState::VertexBuffer},
            BufferBarrier{uniforms.Value(), ResourceState::Undefined,
                          ResourceState::ConstantBuffer},
        };
        const TextureBarrier textureBarriers[2] = {
            TextureBarrier{color.Value(), SubresourceRange{}, ResourceState::Undefined,
                           ResourceState::RenderTarget},
            TextureBarrier{depth.Value(), SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                           ResourceState::Undefined, ResourceState::DepthWrite},
        };
        if (!list.Begin() || !list.Barrier(Span<const BufferBarrier>(bufferBarriers, 2),
                                           Span<const TextureBarrier>(textureBarriers, 2)))
        {
            return 72;
        }

        ColorAttachment colorAttachment{};
        colorAttachment.view = colorView.Value();
        RenderPassDesc passDesc{};
        passDesc.colorAttachments = Span<const ColorAttachment>(&colorAttachment, 1);
        passDesc.hasDepthStencil = true;
        passDesc.depthStencil.view = depthView.Value();
        if (!list.BeginRenderPass(passDesc) ||
            !list.SetViewport(Viewport{0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f}) ||
            !list.SetScissor(ScissorRect{0, 0, 64, 64}) || !list.SetPipeline(pipeline.Value()) ||
            !list.SetVertexBuffer(0, vertices.Value()) ||
            !list.SetBindGroup(BindGroupSlot::PerMaterial, bindGroup.Value()) ||
            !list.Draw(DrawArguments{3, 1, 0, 0}) || !list.EndRenderPass() || !list.End())
        {
            return 73;
        }
        if (list.Stats().draws != 1 || list.Stats().barriers != 4)
        {
            return 74;
        }

        const CommandListHandle handles[1] = {list.Handle()};
        const FenceSignal signal{fence, 1};
        SubmitDesc submitDesc{};
        submitDesc.commandLists = Span<const CommandListHandle>(handles, 1);
        submitDesc.signals = Span<const FenceSignal>(&signal, 1);
        if (!device.Submit(submitDesc).IsOk() || device.FenceValue(fence) != 1 ||
            !device.LastSubmission(QueueKind::Graphics).valid)
        {
            return 75;
        }

        // A pass over a target nobody transitioned is the bug this phase exists
        // to catch, and the installed package must catch it too.
        auto freshColor = device.CreateTexture(colorDesc);
        TextureViewDesc freshViewDesc{};
        freshViewDesc.texture = freshColor.Value();
        freshViewDesc.format = Format::RGBA8Unorm;
        auto freshView = device.CreateTextureView(freshViewDesc);
        ColorAttachment freshAttachment{};
        freshAttachment.view = freshView.Value();
        RenderPassDesc freshPass{};
        freshPass.colorAttachments = Span<const ColorAttachment>(&freshAttachment, 1);
        freshPass.hasDepthStencil = true;
        freshPass.depthStencil.view = depthView.Value();

        CommandList broken = std::move(device.CreateCommandList(listDesc).Value());
        if (!broken.Begin())
        {
            return 76;
        }
        if (broken.BeginRenderPass(freshPass).IsOk())
        {
            return 77;
        }
        broken.Reset();

        // A swapchain over a caller-owned surface.
        static int surfaceToken = 0;
        SwapChainDesc swapDesc{};
        swapDesc.surface.nativeWindow = &surfaceToken;
        swapDesc.surface.width = 320;
        swapDesc.surface.height = 200;
        auto createdSwapChain = device.CreateSwapChain(swapDesc);
        if (!createdSwapChain)
        {
            return 78;
        }
        SwapChain swapchain = std::move(createdSwapChain.Value());
        auto acquired = swapchain.AcquireNextImage();
        if (!acquired || acquired.Value().imageIndex != 0 || !swapchain.Present().IsOk() ||
            !swapchain.Resize(640, 480).IsOk() || swapchain.Info().width != 640)
        {
            return 79;
        }

        // Deferred destruction: the color target is held until the fence passes.
        RetirementQueue retired(heap);
        if (!retired.Retire(color.Value(), fence, 1).IsOk())
        {
            return 80;
        }
        const auto collected = retired.Collect(device);
        if (!collected || collected.Value() != 1 || device.TextureInfoOf(color.Value()).valid)
        {
            return 81;
        }
    }

    // Phase 14: the Direct3D 12 backend is reachable through the installed
    // package, and the installed header's availability query agrees with the
    // linked binary. This is the one place the package's D3D12 link requirements
    // are proved: a `PRIVATE` library entry would fail to link here.
    {
        const cyber::rhi::Backend* d3d12 = cyber::rhi::D3D12Backend();
        if (cyber::rhi::IsD3D12CompiledIn() != (d3d12 != nullptr))
        {
            return 82;
        }
        if (d3d12 != nullptr)
        {
            if (d3d12->kind != cyber::rhi::BackendKind::D3D12 ||
                std::strcmp(d3d12->name, "d3d12") != 0 || d3d12->enumerateAdapters == nullptr)
            {
                return 83;
            }
            if (d3d12->enumerateAdapters(nullptr, 0) < 1)
            {
                return 84;
            }
            cyber::rhi::AdapterInfo adapters[4]{};
            const cyber::core::u32 found = cyber::rhi::EnumerateAdapters(
                *d3d12, cyber::memory::Span<cyber::rhi::AdapterInfo>(adapters, 4));
            if (found < 1 || adapters[0].name[0] == '\0')
            {
                return 85;
            }

            // A device on the last adapter, which is WARP and therefore present
            // on any Windows machine, including a CI runner with no GPU.
            cyber::rhi::DeviceDesc gpuDesc{};
            gpuDesc.backend = d3d12;
            gpuDesc.adapterIndex = d3d12->enumerateAdapters(nullptr, 0) - 1;
            cyber::memory::HeapAllocator gpuHeap;
            auto gpu = cyber::rhi::Device::Create(gpuDesc, gpuHeap);
            if (!gpu)
            {
                return 86;
            }
            {
                cyber::rhi::Device device = std::move(gpu.Value());
                if (!device.Capabilities().features.Has(cyber::rhi::Feature::TimelineFences) ||
                    !device.Queue(cyber::rhi::QueueKind::Graphics).IsValid())
                {
                    return 87;
                }
                auto fence = device.CreateFence(cyber::rhi::FenceDesc{});
                if (!fence || !device.SignalFence(fence.Value(), 2).IsOk() ||
                    !device.WaitFence(fence.Value(), 2).IsOk() ||
                    device.FenceValue(fence.Value()) != 2)
                {
                    return 88;
                }
                if (!device.WaitIdle().IsOk())
                {
                    return 89;
                }
            }
            if (gpuHeap.Stats().used != 0)
            {
                return 90;
            }
        }
    }

    return 0;
}
