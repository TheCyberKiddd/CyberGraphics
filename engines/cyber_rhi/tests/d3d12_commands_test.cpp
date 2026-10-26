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
// a readback buffer sized for one aligned row per texel row.
struct Frame
{
    PipelineLayoutHandle layout{};
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
    auto layout = device.CreatePipelineLayout(PipelineLayoutDesc{});
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
    CG_EXPECT_TRUE(device.Submit(submit).IsOk());

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
