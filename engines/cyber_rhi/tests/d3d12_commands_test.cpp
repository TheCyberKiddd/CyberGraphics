#include <cyber/core/log.h>
#include <cyber/core/log_sinks.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/swapchain.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <cstring>
#include <utility>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

constexpr u32 kSize = 32;
constexpr u32 kRowPitch = 256;

// A vertex shader whose input is addressed by location, which the backend maps to
// the `TEXCOORD0` semantic.
constexpr char kVertexSource[] = "struct Input { float3 position : TEXCOORD0; };\n"
                                 "float4 main(Input input) : SV_Position\n"
                                 "{\n"
                                 "    return float4(input.position, 1.0);\n"
                                 "}\n";

constexpr char kPixelSource[] = "float4 main(float4 position : SV_Position) : SV_Target\n"
                                "{\n"
                                "    return float4(1.0, 0.5, 0.25, 1.0);\n"
                                "}\n";

[[nodiscard]] Span<const Byte> Bytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text),
                            std::strlen(text) * sizeof(char) / sizeof(Byte));
}

[[nodiscard]] bool OpenD3D12Device(Device& out, HeapAllocator& heap) noexcept
{
    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        return false;
    }
    const u32 count = backend->enumerateAdapters(nullptr, 0);
    if (count == 0)
    {
        return false;
    }
    DeviceDesc desc{};
    desc.backend = backend;
    desc.adapterIndex = count - 1;
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

// Everything one end-to-end draw needs: geometry on the GPU, a color target, and
// a readback buffer sized for one aligned row per texel row. The layout carries
// the one used slot the binding contract requires, with a group the drawer
// binds even though the shaders read nothing from it.
struct Frame
{
    PipelineLayoutHandle layout{};
    BindGroupHandle group{};
    ShaderHandle vertex{};
    ShaderHandle pixel{};
    PipelineHandle pipeline{};
    BufferHandle vertices{};
    TextureHandle color{};
    TextureViewHandle colorView{};
    TextureHandle depth{};
    TextureViewHandle depthView{};
    BufferHandle readback{};
    FenceHandle fence{};
};

[[nodiscard]] bool BuildFrame(Device& device, HeapAllocator& heap, Frame& frame) noexcept
{
    // One used slot backed by a real uniform buffer, so the recording's bind is
    // valid even though the shaders below read nothing from it.
    const BindGroupLayoutEntry entry{0, BindingType::UniformBuffer,
                                     ShaderStageBit(ShaderStage::Pixel), 1};
    BindGroupLayoutDesc groupLayoutDesc{};
    groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto groupLayout = device.CreateBindGroupLayout(groupLayoutDesc);
    if (!groupLayout)
    {
        return false;
    }

    BufferDesc constantsDesc{};
    constantsDesc.usage = BufferUsage::Uniform | BufferUsage::CopyDestination;
    constantsDesc.sizeBytes = 256;
    auto constants = device.CreateBuffer(constantsDesc);
    if (!constants)
    {
        return false;
    }

    BindingResource resource{};
    resource.type = BindingType::UniformBuffer;
    resource.buffer = constants.Value();
    resource.offset = 0;
    resource.size = 256;
    BindGroupDesc groupDesc{};
    groupDesc.layout = groupLayout.Value();
    groupDesc.resources = Span<const BindingResource>(&resource, 1);
    auto group = device.CreateBindGroup(groupDesc);
    if (!group)
    {
        return false;
    }
    frame.group = group.Value();

    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerFrame)] = groupLayout.Value();
    auto layout = device.CreatePipelineLayout(layoutDesc);
    if (!layout)
    {
        return false;
    }
    frame.layout = layout.Value();

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.code = Bytes(kVertexSource);
    vertexDesc.entryPoint = "main";
    auto vertex = device.CreateShader(vertexDesc);
    if (!vertex)
    {
        return false;
    }
    frame.vertex = vertex.Value();

    ShaderDesc pixelDesc{};
    pixelDesc.stage = ShaderStage::Pixel;
    pixelDesc.code = Bytes(kPixelSource);
    pixelDesc.entryPoint = "main";
    auto pixel = device.CreateShader(pixelDesc);
    if (!pixel)
    {
        return false;
    }
    frame.pixel = pixel.Value();

    TextureDesc colorDesc{};
    colorDesc.format = Format::RGBA8Unorm;
    colorDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    colorDesc.width = kSize;
    colorDesc.height = kSize;
    auto color = device.CreateTexture(colorDesc);
    if (!color)
    {
        return false;
    }
    frame.color = color.Value();

    TextureViewDesc colorView{};
    colorView.texture = frame.color;
    colorView.format = Format::RGBA8Unorm;
    auto colorViewResult = device.CreateTextureView(colorView);
    if (!colorViewResult)
    {
        return false;
    }
    frame.colorView = colorViewResult.Value();

    TextureDesc depthDesc{};
    depthDesc.format = Format::D32Float;
    depthDesc.usage = TextureUsage::DepthStencil;
    depthDesc.width = kSize;
    depthDesc.height = kSize;
    auto depth = device.CreateTexture(depthDesc);
    if (!depth)
    {
        return false;
    }
    frame.depth = depth.Value();

    TextureViewDesc depthView{};
    depthView.texture = frame.depth;
    depthView.format = Format::D32Float;
    // A depth-only texture exposes exactly its depth aspect, and the rule
    // requires the description to say so.
    depthView.range.aspect = TextureAspect::Depth;
    auto depthViewResult = device.CreateTextureView(depthView);
    if (!depthViewResult)
    {
        return false;
    }
    frame.depthView = depthViewResult.Value();

    ColorAttachmentState attachment{};
    attachment.format = Format::RGBA8Unorm;
    VertexBinding binding{};
    binding.strideBytes = 12;
    binding.rate = VertexInputRate::PerVertex;
    VertexAttribute attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = Format::R32G32B32Float;
    attribute.offsetBytes = 0;

    GraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.layout = frame.layout;
    pipelineDesc.vertexShader = frame.vertex;
    pipelineDesc.pixelShader = frame.pixel;
    pipelineDesc.vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
    pipelineDesc.vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
    // Winding is a rasterizer rule, not a recording rule: this test is about the
    // commands, so nothing is culled.
    pipelineDesc.rasterizer.cullMode = CullMode::None;
    // Reverse-Z: the near plane is 1 and the far plane is 0, so the pass clears
    // to the far plane and a nearer fragment wins with a greater comparison.
    pipelineDesc.depthStencil.depthCompare = CompareFunction::GreaterEqual;
    pipelineDesc.depthStencilFormat = Format::D32Float;
    pipelineDesc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
    auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
    if (!pipeline)
    {
        return false;
    }
    frame.pipeline = pipeline.Value();

    // A default-heap vertex buffer with a staging copy is the real upload path:
    // host-visible memory is not a valid bind target on this API.
    BufferDesc vertexDescBuffer{};
    vertexDescBuffer.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    vertexDescBuffer.sizeBytes = 512;
    auto vertices = device.CreateBuffer(vertexDescBuffer);
    if (!vertices)
    {
        return false;
    }
    frame.vertices = vertices.Value();

    BufferDesc readbackDesc{};
    readbackDesc.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    readbackDesc.sizeBytes = static_cast<u64>(kRowPitch) * kSize;
    auto readback = device.CreateBuffer(readbackDesc);
    if (!readback)
    {
        return false;
    }
    frame.readback = readback.Value();

    FenceDesc fenceDesc{};
    auto fence = device.CreateFence(fenceDesc);
    if (!fence)
    {
        return false;
    }
    frame.fence = fence.Value();
    (void)heap;
    return true;
}

}  // namespace

CG_TEST(d3d12_commands_reject_recording_out_of_order)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    CommandListDesc listDesc{};
    auto created = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(created));
    CommandList list = std::move(created.Value());

    // Nothing records before `begin`.
    CG_EXPECT_EQ(list.State(), CommandListState::Initial);
    CG_EXPECT_FALSE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_EXPECT_FALSE(list.End().IsOk());

    CG_REQUIRE(list.Begin().IsOk());
    CG_EXPECT_TRUE(list.IsRecording());
    // Recording twice is a caller mistake, not a reset.
    CG_EXPECT_FALSE(list.Begin().IsOk());

    // A draw outside a pass is refused, and nothing is counted.
    CG_EXPECT_FALSE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_EXPECT_EQ(list.Stats().draws, u32{0});

    // Ending with a pass open is refused, and the list stays usable.
    ColorAttachment attachment{};
    attachment.view = TextureViewHandle{};
    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&attachment, 1);
    CG_EXPECT_FALSE(list.BeginRenderPass(pass).IsOk());

    CG_EXPECT_TRUE(list.End().IsOk());
    CG_EXPECT_EQ(list.State(), CommandListState::Ended);

    // Submission of a list that has ended is accepted; a second submit of the
    // same list is not the API's problem, so it is the caller's to avoid.
    SubmitDesc empty{};
    CG_EXPECT_FALSE(device.Submit(empty).IsOk());

    SubmitDesc wrongQueue{};
    wrongQueue.queue = QueueKind::Copy;
    const CommandListHandle handles[] = {list.Handle()};
    wrongQueue.commandLists = Span<const CommandListHandle>(handles, 1);
    CG_EXPECT_FALSE(device.Submit(wrongQueue).IsOk());

    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(handles, 1);
    FenceDesc drainDesc{};
    auto drain = device.CreateFence(drainDesc);
    CG_REQUIRE(static_cast<bool>(drain));
    FenceSignal drained{};
    drained.fence = drain.Value();
    drained.value = 1;
    submit.signals = Span<const FenceSignal>(&drained, 1);
    CG_EXPECT_TRUE(device.Submit(submit).IsOk());
    // The list's allocator belongs to the queue until the submission finishes,
    // so the fence is drained before the list is released.
    CG_REQUIRE(device.WaitFence(drain.Value(), 1).IsOk());

    // A list owns its slot: releasing the list makes its handle stale, and a
    // stale handle cannot be submitted.
    const CommandListHandle stale = list.Handle();
    list = CommandList{};
    submit.commandLists = Span<const CommandListHandle>(&stale, 1);
    CG_EXPECT_FALSE(device.Submit(submit).IsOk());
}

CG_TEST(d3d12_commands_refuse_a_swapchain_without_a_surface)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    SwapChainDesc desc{};
    desc.format = Format::BGRA8UnormSrgb;
    desc.imageCount = 2;
    desc.surface.nativeWindow = nullptr;
    desc.surface.width = 64;
    desc.surface.height = 64;
    CG_EXPECT_FALSE(device.CreateSwapChain(desc).IsOk());

    // A real window handle is required before the extent is even considered.
    desc.surface.nativeWindow = reinterpret_cast<const void*>(static_cast<uintptr_t>(1));
    desc.surface.width = 0;
    CG_EXPECT_FALSE(device.CreateSwapChain(desc).IsOk());
}

// The phase's milestones as separate steps, in the order they fail: bytes the
// upload moved (nothing else can be right if this is wrong), the triangle's
// shade and its clear, and a frame the debug layer has no comment on. The depth
// and winding control experiments that narrowed the missing-draw defect down are
// recorded in the phase report rather than kept here, because they cannot pass or
// fail on their own.

CG_TEST(d3d12_commands_upload_round_trips_vertex_bytes)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    const float positions[9] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};

    BufferDesc vertexDesc{};
    vertexDesc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    vertexDesc.sizeBytes = 512;
    auto vertices = device.CreateBuffer(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertices));

    BufferDesc stagingDesc{};
    stagingDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    stagingDesc.sizeBytes = 512;
    auto staging = device.CreateBuffer(stagingDesc);
    CG_REQUIRE(static_cast<bool>(staging));

    BufferDesc readbackDesc{};
    readbackDesc.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    readbackDesc.sizeBytes = 512;
    auto readback = device.CreateBuffer(readbackDesc);
    CG_REQUIRE(static_cast<bool>(readback));

    CG_REQUIRE(device
                   .WriteBuffer(staging.Value(), 0,
                                Span<const Byte>(reinterpret_cast<const Byte*>(positions),
                                                 sizeof(positions)))
                   .IsOk());

    FenceDesc fenceDesc{};
    auto fence = device.CreateFence(fenceDesc);
    CG_REQUIRE(static_cast<bool>(fence));

    CommandListDesc listDesc{};
    auto created = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(created));
    CommandList list = std::move(created.Value());
    CG_REQUIRE(list.Begin().IsOk());

    const BufferBarrier start[] = {
        {staging.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {vertices.Value(), ResourceState::Undefined, ResourceState::CopyDestination}};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(start, 2), Span<const TextureBarrier>{}).IsOk());
    BufferCopy upload{};
    upload.source = staging.Value();
    upload.destination = vertices.Value();
    upload.sizeBytes = sizeof(positions);
    CG_REQUIRE(list.CopyBuffer(upload).IsOk());

    const BufferBarrier back[] = {
        {vertices.Value(), ResourceState::CopyDestination, ResourceState::CopySource},
        {readback.Value(), ResourceState::Undefined, ResourceState::CopyDestination}};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(back, 2), Span<const TextureBarrier>{}).IsOk());
    BufferCopy out{};
    out.source = vertices.Value();
    out.destination = readback.Value();
    out.sizeBytes = sizeof(positions);
    CG_REQUIRE(list.CopyBuffer(out).IsOk());
    CG_REQUIRE(list.End().IsOk());

    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    const CommandListHandle handles[] = {list.Handle()};
    submit.commandLists = Span<const CommandListHandle>(handles, 1);
    FenceSignal signal{};
    signal.fence = fence.Value();
    signal.value = 1;
    submit.signals = Span<const FenceSignal>(&signal, 1);
    CG_REQUIRE(device.Submit(submit).IsOk());
    CG_REQUIRE(device.WaitFence(fence.Value(), 1).IsOk());

    float result[9] = {};
    CG_REQUIRE(device
                   .ReadBuffer(readback.Value(), 0,
                               Span<Byte>(reinterpret_cast<Byte*>(result), sizeof(result)))
                   .IsOk());
    for (u32 index = 0; index < 9; ++index)
    {
        CG_EXPECT_EQ(result[index], positions[index]);
    }
}

// The phase's first milestone: a colored triangle drawn through the RHI on
// Direct3D 12 and read back out of the attachment it was drawn into.
//
// The test reads three things, because a silent triangle has three possible
// faults: the bytes the upload wrote (did staging reach the vertex buffer), the
// clear color at a corner (did the pass and the readback work at all), and the
// shaded pixel in the middle (did the draw happen).
CG_TEST(d3d12_commands_draw_a_colored_triangle)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    Frame frame{};
    CG_REQUIRE(BuildFrame(device, heap, frame));

    // One triangle covering the middle of the target, in clip space.
    const float positions[9] = {-0.9f, -0.9f, 0.5f, 0.9f, -0.9f, 0.5f, 0.0f, 0.9f, 0.5f};

    BufferDesc stagingDesc{};
    stagingDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
    stagingDesc.sizeBytes = 512;
    auto staging = device.CreateBuffer(stagingDesc);
    CG_REQUIRE(static_cast<bool>(staging));
    CG_REQUIRE(device
                   .WriteBuffer(staging.Value(), 0,
                                Span<const Byte>(reinterpret_cast<const Byte*>(positions),
                                                 sizeof(positions)))
                   .IsOk());

    BufferDesc vertexReadbackDesc{};
    vertexReadbackDesc.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    vertexReadbackDesc.sizeBytes = sizeof(positions);
    auto vertexReadback = device.CreateBuffer(vertexReadbackDesc);
    CG_REQUIRE(static_cast<bool>(vertexReadback));

    CommandListDesc listDesc{};
    auto created = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(created));
    CommandList list = std::move(created.Value());
    CG_REQUIRE(list.Begin().IsOk());

    const BufferBarrier upload[] = {
        {staging.Value(), ResourceState::Undefined, ResourceState::CopySource},
        {frame.vertices, ResourceState::Undefined, ResourceState::CopyDestination},
        {vertexReadback.Value(), ResourceState::Undefined, ResourceState::CopyDestination}};
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>(upload, 3), Span<const TextureBarrier>{}).IsOk());

    BufferCopy copy{};
    copy.source = staging.Value();
    copy.destination = frame.vertices;
    copy.sizeBytes = sizeof(positions);
    CG_REQUIRE(list.CopyBuffer(copy).IsOk());

    const BufferBarrier vertexReady[] = {
        {frame.vertices, ResourceState::CopyDestination, ResourceState::VertexBuffer}};
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(vertexReady, 1), Span<const TextureBarrier>{})
                   .IsOk());

    const TextureBarrier targets[] = {
        TextureBarrier{frame.color, SubresourceRange{}, ResourceState::Undefined,
                       ResourceState::RenderTarget},
        TextureBarrier{frame.depth, SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                       ResourceState::Undefined, ResourceState::DepthWrite},
    };
    CG_REQUIRE(
        list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(targets, 2)).IsOk());

    ColorAttachment attachment{};
    attachment.view = frame.colorView;
    attachment.loadOp = LoadOp::Clear;
    attachment.storeOp = StoreOp::Store;
    attachment.clearValue = ClearColorValue::Of(0.0f, 0.0f, 0.0f, 1.0f);

    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&attachment, 1);
    pass.hasDepthStencil = true;
    pass.depthStencil.view = frame.depthView;
    pass.depthStencil.depthLoadOp = LoadOp::Clear;
    pass.depthStencil.depthStoreOp = StoreOp::Store;
    pass.depthStencil.clearValue = ClearDepthStencilValue::Of(0.0f);
    pass.label = "Geometry Pass";

    CG_REQUIRE(list.BeginRenderPass(pass).IsOk());
    CG_REQUIRE(list.SetDebugMarker("Geometry draw begins").IsOk());
    CG_REQUIRE(list.SetPipeline(frame.pipeline).IsOk());
    CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerFrame, frame.group).IsOk());
    CG_REQUIRE(list.SetViewport(Viewport::Of(static_cast<float>(kSize), static_cast<float>(kSize)))
                   .IsOk());
    CG_REQUIRE(list.SetScissor(ScissorRect{0, 0, kSize, kSize}).IsOk());
    CG_REQUIRE(list.SetVertexBuffer(0, frame.vertices).IsOk());
    CG_REQUIRE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
    CG_REQUIRE(list.EndRenderPass().IsOk());

    const BufferBarrier pixelReady[] = {
        {frame.vertices, ResourceState::VertexBuffer, ResourceState::CopySource}};
    const TextureBarrier colorReady[] = {
        TextureBarrier{frame.color, SubresourceRange{}, ResourceState::RenderTarget,
                       ResourceState::CopySource},
    };
    CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(pixelReady, 1),
                            Span<const TextureBarrier>(colorReady, 1))
                   .IsOk());

    BufferCopy vertexOut{};
    vertexOut.source = frame.vertices;
    vertexOut.destination = vertexReadback.Value();
    vertexOut.sizeBytes = sizeof(positions);
    CG_REQUIRE(list.CopyBuffer(vertexOut).IsOk());

    BufferTextureCopy pixels{};
    pixels.buffer = frame.readback;
    pixels.bufferRowPitch = kRowPitch;
    pixels.texture = frame.color;
    pixels.width = kSize;
    pixels.height = kSize;
    CG_REQUIRE(list.CopyTextureToBuffer(pixels).IsOk());
    CG_REQUIRE(list.End().IsOk());

    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    const CommandListHandle handles[] = {list.Handle()};
    submit.commandLists = Span<const CommandListHandle>(handles, 1);
    FenceSignal signal{};
    signal.fence = frame.fence;
    signal.value = 1;
    submit.signals = Span<const FenceSignal>(&signal, 1);
    CG_REQUIRE(device.Submit(submit).IsOk());
    CG_REQUIRE(device.WaitFence(frame.fence, 1).IsOk());

    // What the upload wrote, read back out of the default-heap buffer.
    float uploaded[9] = {};
    CG_REQUIRE(device
                   .ReadBuffer(vertexReadback.Value(), 0,
                               Span<Byte>(reinterpret_cast<Byte*>(uploaded), sizeof(uploaded)))
                   .IsOk());
    for (u32 index = 0; index < 9; ++index)
    {
        CG_EXPECT_EQ(uploaded[index], positions[index]);
    }

    // The clear at a corner the triangle never reaches.
    u8 corner[4] = {};
    CG_REQUIRE(device.ReadBuffer(frame.readback, 0, Span<Byte>(reinterpret_cast<Byte*>(corner), 4))
                   .IsOk());
    CG_EXPECT_EQ(corner[0], u8{0});
    CG_EXPECT_EQ(corner[1], u8{0});
    CG_EXPECT_EQ(corner[2], u8{0});
    CG_EXPECT_EQ(corner[3], u8{255});

    // The shaded pixel: 1.0, 0.5, 0.25, 1.0 written as RGBA8.
    const u64 center = static_cast<u64>(kSize / 2) * kRowPitch + static_cast<u64>(kSize / 2) * 4;
    u8 pixel[4] = {};
    CG_REQUIRE(
        device.ReadBuffer(frame.readback, center, Span<Byte>(reinterpret_cast<Byte*>(pixel), 4))
            .IsOk());
    if (pixel[0] != 255 || pixel[1] != 128 || pixel[2] != 64 || pixel[3] != 255)
    {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "triangle pixel at %u,%u came back %u,%u,%u,%u, expected 255,128,64,255",
                      kSize / 2, kSize / 2, static_cast<unsigned>(pixel[0]),
                      static_cast<unsigned>(pixel[1]), static_cast<unsigned>(pixel[2]),
                      static_cast<unsigned>(pixel[3]));
        CG_FAIL(message);
    }
}

// ---------------------------------------------------------------------------
// The debug layer
// ---------------------------------------------------------------------------

namespace
{

// A bind that samples a texture, so the descriptor paths the debug layer checks
// are on the frame this test records.
constexpr char kSamplingVertexSource[] = "struct Input\n"
                                         "{\n"
                                         "    float3 position : TEXCOORD0;\n"
                                         "    float2 uv : TEXCOORD1;\n"
                                         "};\n"
                                         "struct Output\n"
                                         "{\n"
                                         "    float4 position : SV_Position;\n"
                                         "    float2 uv : TEXCOORD0;\n"
                                         "};\n"
                                         "Output main(Input input)\n"
                                         "{\n"
                                         "    Output output;\n"
                                         "    output.position = float4(input.position, 1.0);\n"
                                         "    output.uv = input.uv;\n"
                                         "    return output;\n"
                                         "}\n";

constexpr char kSamplingPixelSource[] =
    "Texture2D albedo : register(t0, space0);\n"
    "SamplerState albedoSampler : register(s0, space0);\n"
    "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    return albedo.Sample(albedoSampler, uv);\n"
    "}\n";

// One vertex of the full-screen triangle, in the layout the pipeline declares.
struct SampleVertex
{
    float position[3];
    float uv[2];
};
static_assert(sizeof(SampleVertex) == 20);

constexpr u32 kSampledTexel[4] = {64, 128, 255, 255};
constexpr u32 kSampleTextureSize = 2;

// What the logger reported on the `Rhi` channel, which is where the backend
// drains the debug layer's queue.
struct DebugLayerWatch
{
    u32 records = 0;
    char last[cyber::core::kMaxLogMessageLength]{};
};

void WatchLog(void* userData, const cyber::core::LogRecord& record) noexcept
{
    auto* watch = static_cast<DebugLayerWatch*>(userData);
    if (record.channel != cyber::core::LogChannel::Rhi)
    {
        return;
    }
    watch->records += 1;
    std::snprintf(watch->last, sizeof(watch->last), "%.220s", record.message);
}

}  // namespace

// The build guide counts any debug-layer message as a test failure: the backend
// drains the layer's queue when the device goes away and reports what it found on
// the `Rhi` channel, so the frame below is recorded again with a sink installed
// and the test fails if the layer had anything to say about it.
//
// The frame is a sampling draw on purpose: an upload, both kinds of texture
// barrier, a view, a sampler, a bind group whose descriptors are written into a
// shader-visible heap, a pass, and a readback. Replacing any of those with a
// switch that does nothing would hide exactly the messages this test is for.
CG_TEST(d3d12_commands_leave_the_debug_layer_silent)
{
    HeapAllocator heap;
    DebugLayerWatch watch{};
    cyber::core::CallbackSink sink{&WatchLog, &watch};
    CG_REQUIRE(cyber::core::DefaultLogger().AddSink(&sink));

    {
        Device device;
        if (!OpenD3D12Device(device, heap))
        {
            (void)cyber::core::DefaultLogger().RemoveSink(&sink);
            CG_FAIL("this build has no Direct3D 12 adapter to record on");
            return;
        }

        Frame frame{};
        CG_REQUIRE(BuildFrame(device, heap, frame));

        BufferDesc stagingDesc{};
        stagingDesc.usage = BufferUsage::CopySource | BufferUsage::HostVisible;
        stagingDesc.sizeBytes = 1024;
        auto staging = device.CreateBuffer(stagingDesc);
        CG_REQUIRE(static_cast<bool>(staging));

        TextureDesc textureDesc{};
        textureDesc.format = Format::RGBA8Unorm;
        textureDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        textureDesc.width = kSampleTextureSize;
        textureDesc.height = kSampleTextureSize;
        auto texture = device.CreateTexture(textureDesc);
        CG_REQUIRE(static_cast<bool>(texture));
        TextureViewDesc textureViewDesc{};
        textureViewDesc.texture = texture.Value();
        textureViewDesc.format = Format::RGBA8Unorm;
        auto textureView = device.CreateTextureView(textureViewDesc);
        CG_REQUIRE(static_cast<bool>(textureView));

        SamplerDesc samplerDesc{};
        samplerDesc.minFilter = FilterMode::Nearest;
        samplerDesc.magFilter = FilterMode::Nearest;
        samplerDesc.mipmapFilter = FilterMode::Nearest;
        samplerDesc.addressU = AddressMode::Repeat;
        samplerDesc.addressV = AddressMode::Repeat;
        auto sampler = device.CreateSampler(samplerDesc);
        CG_REQUIRE(static_cast<bool>(sampler));

        BindGroupLayoutEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].type = BindingType::SampledTexture;
        entries[0].visibility = ShaderStageBit(ShaderStage::Pixel);
        entries[1].binding = 1;
        entries[1].type = BindingType::Sampler;
        entries[1].visibility = ShaderStageBit(ShaderStage::Pixel);
        BindGroupLayoutDesc groupLayoutDesc{};
        groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(entries, 2);
        auto groupLayout = device.CreateBindGroupLayout(groupLayoutDesc);
        CG_REQUIRE(static_cast<bool>(groupLayout));

        BindingResource resources[2]{};
        resources[0].type = BindingType::SampledTexture;
        resources[0].textureView = textureView.Value();
        resources[1].type = BindingType::Sampler;
        resources[1].sampler = sampler.Value();
        BindGroupDesc groupDesc{};
        groupDesc.layout = groupLayout.Value();
        groupDesc.resources = Span<const BindingResource>(resources, 2);
        auto group = device.CreateBindGroup(groupDesc);
        CG_REQUIRE(static_cast<bool>(group));

        PipelineLayoutDesc layoutDesc{};
        layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerFrame)] = groupLayout.Value();
        auto layout = device.CreatePipelineLayout(layoutDesc);
        CG_REQUIRE(static_cast<bool>(layout));

        ShaderDesc vertexShaderDesc{};
        vertexShaderDesc.stage = ShaderStage::Vertex;
        vertexShaderDesc.code = Bytes(kSamplingVertexSource);
        vertexShaderDesc.entryPoint = "main";
        auto vertexShader = device.CreateShader(vertexShaderDesc);
        CG_REQUIRE(static_cast<bool>(vertexShader));
        ShaderDesc pixelShaderDesc{};
        pixelShaderDesc.stage = ShaderStage::Pixel;
        pixelShaderDesc.code = Bytes(kSamplingPixelSource);
        pixelShaderDesc.entryPoint = "main";
        auto pixelShader = device.CreateShader(pixelShaderDesc);
        CG_REQUIRE(static_cast<bool>(pixelShader));

        ColorAttachmentState attachment{};
        attachment.format = Format::RGBA8Unorm;
        VertexBinding binding{};
        binding.strideBytes = sizeof(SampleVertex);
        binding.rate = VertexInputRate::PerVertex;
        VertexAttribute attributes[2]{};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = Format::R32G32B32Float;
        attributes[0].offsetBytes = 0;
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = Format::RG32Float;
        attributes[1].offsetBytes = 12;

        GraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.layout = layout.Value();
        pipelineDesc.vertexShader = vertexShader.Value();
        pipelineDesc.pixelShader = pixelShader.Value();
        pipelineDesc.vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
        pipelineDesc.vertexLayout.attributes = Span<const VertexAttribute>(attributes, 2);
        pipelineDesc.rasterizer.cullMode = CullMode::None;
        pipelineDesc.depthStencil.depthCompare = CompareFunction::GreaterEqual;
        pipelineDesc.depthStencilFormat = Format::D32Float;
        pipelineDesc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
        auto pipeline = device.CreateGraphicsPipeline(pipelineDesc);
        CG_REQUIRE(static_cast<bool>(pipeline));

        // The staging buffer leads with the texture's rows, one aligned row per
        // row, and the triangle follows at the next aligned offset.
        for (u32 y = 0; y < kSampleTextureSize; ++y)
        {
            u8 row[kSampleTextureSize * 4];
            for (u32 x = 0; x < kSampleTextureSize; ++x)
            {
                for (u32 channel = 0; channel < 4; ++channel)
                {
                    row[x * 4 + channel] = static_cast<u8>(kSampledTexel[channel]);
                }
            }
            CG_REQUIRE(
                device
                    .WriteBuffer(staging.Value(), static_cast<u64>(y) * kRowPitch,
                                 Span<const Byte>(reinterpret_cast<const Byte*>(row), sizeof(row)))
                    .IsOk());
        }

        // A triangle that covers the target, with uv running 0..1 across it.
        const SampleVertex corners[3] = {
            SampleVertex{{-1.0f, -1.0f, 0.5f}, {0.0f, 0.0f}},
            SampleVertex{{3.0f, -1.0f, 0.5f}, {2.0f, 0.0f}},
            SampleVertex{{-1.0f, 3.0f, 0.5f}, {0.0f, 2.0f}},
        };
        const u64 vertexOffset = 512;
        CG_REQUIRE(device
                       .WriteBuffer(staging.Value(), vertexOffset,
                                    Span<const Byte>(reinterpret_cast<const Byte*>(corners),
                                                     sizeof(corners)))
                       .IsOk());

        CommandListDesc listDesc{};
        auto created = device.CreateCommandList(listDesc);
        CG_REQUIRE(static_cast<bool>(created));
        CommandList list = std::move(created.Value());
        CG_REQUIRE(list.Begin().IsOk());

        const BufferBarrier uploadBuffers[] = {
            {staging.Value(), ResourceState::Undefined, ResourceState::CopySource},
            {frame.vertices, ResourceState::Undefined, ResourceState::CopyDestination}};
        const TextureBarrier uploadTexture[] = {
            TextureBarrier{texture.Value(), SubresourceRange{}, ResourceState::Undefined,
                           ResourceState::CopyDestination},
        };
        CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(uploadBuffers, 2),
                                Span<const TextureBarrier>(uploadTexture, 1))
                       .IsOk());

        BufferTextureCopy texels{};
        texels.buffer = staging.Value();
        texels.bufferRowPitch = kRowPitch;
        texels.texture = texture.Value();
        texels.width = kSampleTextureSize;
        texels.height = kSampleTextureSize;
        CG_REQUIRE(list.CopyBufferToTexture(texels).IsOk());

        BufferCopy vertexCopy{};
        vertexCopy.source = staging.Value();
        vertexCopy.sourceOffset = vertexOffset;
        vertexCopy.destination = frame.vertices;
        vertexCopy.sizeBytes = sizeof(corners);
        CG_REQUIRE(list.CopyBuffer(vertexCopy).IsOk());

        const BufferBarrier ready[] = {
            {frame.vertices, ResourceState::CopyDestination, ResourceState::VertexBuffer},
        };
        const TextureBarrier sampling[] = {
            TextureBarrier{texture.Value(), SubresourceRange{}, ResourceState::CopyDestination,
                           ResourceState::ShaderRead},
        };
        CG_REQUIRE(list.Barrier(Span<const BufferBarrier>(ready, 1),
                                Span<const TextureBarrier>(sampling, 1))
                       .IsOk());

        // The system's own passes own the attachment transitions, so the same
        // transition is recorded twice in a row here on purpose: the second one
        // must be a no-op rather than an equal-state barrier the API rejects.
        const TextureBarrier targets[] = {
            TextureBarrier{frame.color, SubresourceRange{}, ResourceState::Undefined,
                           ResourceState::RenderTarget},
            TextureBarrier{frame.depth, SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                           ResourceState::Undefined, ResourceState::DepthWrite},
        };
        CG_REQUIRE(list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(targets, 2))
                       .IsOk());
        CG_REQUIRE(list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(targets, 2))
                       .IsOk());

        ColorAttachment attachmentState{};
        attachmentState.view = frame.colorView;
        attachmentState.loadOp = LoadOp::Clear;
        attachmentState.storeOp = StoreOp::Store;
        attachmentState.clearValue = ClearColorValue::Of(0.0f, 0.0f, 0.0f, 1.0f);
        RenderPassDesc pass{};
        pass.colorAttachments = Span<const ColorAttachment>(&attachmentState, 1);
        pass.hasDepthStencil = true;
        pass.depthStencil.view = frame.depthView;
        pass.depthStencil.depthLoadOp = LoadOp::Clear;
        pass.depthStencil.depthStoreOp = StoreOp::Store;
        pass.depthStencil.clearValue = ClearDepthStencilValue::Of(0.0f);
        pass.label = "Textured Geometry Pass";
        CG_REQUIRE(list.BeginRenderPass(pass).IsOk());
        CG_REQUIRE(list.SetDebugMarker("Textured geometry draw begins").IsOk());
        CG_REQUIRE(list.SetPipeline(pipeline.Value()).IsOk());
        CG_REQUIRE(list.SetBindGroup(BindGroupSlot::PerFrame, group.Value()).IsOk());
        CG_REQUIRE(
            list.SetViewport(Viewport::Of(static_cast<float>(kSize), static_cast<float>(kSize)))
                .IsOk());
        CG_REQUIRE(list.SetScissor(ScissorRect{0, 0, kSize, kSize}).IsOk());
        CG_REQUIRE(list.SetVertexBuffer(0, frame.vertices).IsOk());
        CG_REQUIRE(list.Draw(DrawArguments{3, 1, 0, 0}).IsOk());
        CG_REQUIRE(list.EndRenderPass().IsOk());

        const TextureBarrier colorReady[] = {
            TextureBarrier{frame.color, SubresourceRange{}, ResourceState::RenderTarget,
                           ResourceState::CopySource},
        };
        CG_REQUIRE(
            list.Barrier(Span<const BufferBarrier>{}, Span<const TextureBarrier>(colorReady, 1))
                .IsOk());
        BufferTextureCopy pixels{};
        pixels.buffer = frame.readback;
        pixels.bufferRowPitch = kRowPitch;
        pixels.texture = frame.color;
        pixels.width = kSize;
        pixels.height = kSize;
        CG_REQUIRE(list.CopyTextureToBuffer(pixels).IsOk());
        CG_REQUIRE(list.End().IsOk());

        SubmitDesc submit{};
        submit.queue = QueueKind::Graphics;
        const CommandListHandle handles[] = {list.Handle()};
        submit.commandLists = Span<const CommandListHandle>(handles, 1);
        FenceSignal signal{};
        signal.fence = frame.fence;
        signal.value = 1;
        submit.signals = Span<const FenceSignal>(&signal, 1);
        CG_REQUIRE(device.Submit(submit).IsOk());
        CG_REQUIRE(device.WaitFence(frame.fence, 1).IsOk());

        // The sampled texel at the centre, which is what the group's descriptors
        // were written for.
        const u64 center =
            static_cast<u64>(kSize / 2) * kRowPitch + static_cast<u64>(kSize / 2) * 4;
        u8 pixel[4] = {};
        CG_REQUIRE(
            device.ReadBuffer(frame.readback, center, Span<Byte>(reinterpret_cast<Byte*>(pixel), 4))
                .IsOk());
        for (u32 channel = 0; channel < 4; ++channel)
        {
            CG_EXPECT_EQ(pixel[channel], static_cast<u8>(kSampledTexel[channel]));
        }
    }

    // The device is gone, so the layer's queue has been drained and anything it
    // held has reached the logger.
    (void)cyber::core::DefaultLogger().RemoveSink(&sink);
    if (watch.records != 0)
    {
        CG_FAIL(watch.last);
    }
}
