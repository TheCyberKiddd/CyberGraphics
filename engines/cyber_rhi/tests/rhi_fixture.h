#pragma once

// Shared fixture for the Phase 13 tests.
//
// A draw needs a device, two shaders, a pipeline layout, a bind group, a vertex
// buffer, a color target, and a depth target. Building that per test would bury
// the rules each test is about, so it is built once here from the null backend
// and every test starts from a working frame.

#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>

#include <cstring>
#include <utility>

namespace cyber::rhi::test
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

// Opens a device on the validation backend.
inline bool OpenDevice(Device& out, HeapAllocator& heap) noexcept
{
    DeviceDesc desc{};
    desc.backend = NullBackend();
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

// A span over a string literal's bytes, which is all a shader needs here.
inline Span<const Byte> AsBytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text), std::strlen(text));
}

inline ShaderDesc MakeShader(ShaderStage stage) noexcept
{
    ShaderDesc desc{};
    desc.stage = stage;
    desc.format = ShaderBytecodeFormat::HlslSource;
    desc.code = AsBytes("void main() {}");
    desc.entryPoint = "main";
    return desc;
}

// A device with one of everything a single draw needs.
struct Fixture
{
    HeapAllocator heap;
    Device device;
    FenceHandle fence;

    ShaderHandle vertexShader;
    ShaderHandle pixelShader;
    ShaderHandle computeShader;

    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    BufferHandle uniformBuffer;
    BufferHandle indirectBuffer;

    TextureHandle colorTexture;
    TextureViewHandle colorView;
    TextureHandle depthTexture;
    TextureViewHandle depthView;

    BindGroupLayoutHandle groupLayout;
    BindGroupHandle group;
    PipelineLayoutHandle pipelineLayout;

    PipelineHandle graphicsPipeline;
    PipelineHandle computePipeline;

    u32 width = 64;
    u32 height = 64;
    Format colorFormat = Format::RGBA8Unorm;

    // Builds the device and every object except the textures, whose format and
    // extent `Build` takes.
    bool Build(u32 targetWidth = 64, u32 targetHeight = 64,
               Format targetFormat = Format::RGBA8Unorm) noexcept;

    // Creates a sampled texture with a view over all of it.
    bool MakeSampledTexture(TextureHandle* texture, TextureViewHandle* view, u32 textureWidth = 16,
                            u32 textureHeight = 16) noexcept;

    // Records a pass over the fixture's targets with a full viewport and scissor.
    bool BeginPass(CommandList& list) noexcept;

    // The descriptor of a graphics pipeline the fixture's pass accepts, without
    // the vertex layout and attachment spans, which only live during `Build`.
    GraphicsPipelineDesc GraphicsDesc() noexcept;

    // Transitions the fixture's targets into the states a pass needs.
    bool TransitionTargets(CommandList& list) noexcept;
};

inline bool Fixture::Build(u32 targetWidth, u32 targetHeight, Format targetFormat) noexcept
{
    width = targetWidth;
    height = targetHeight;
    colorFormat = targetFormat;

    if (!OpenDevice(device, heap))
    {
        return false;
    }
    auto fenceResult = device.CreateFence(FenceDesc{});
    if (!fenceResult)
    {
        return false;
    }
    fence = fenceResult.Value();

    auto vertex = device.CreateShader(MakeShader(ShaderStage::Vertex));
    auto pixel = device.CreateShader(MakeShader(ShaderStage::Pixel));
    auto compute = device.CreateShader(MakeShader(ShaderStage::Compute));
    if (!vertex || !pixel || !compute)
    {
        return false;
    }
    vertexShader = vertex.Value();
    pixelShader = pixel.Value();
    computeShader = compute.Value();

    BufferDesc vertices{};
    vertices.usage = BufferUsage::Vertex;
    vertices.sizeBytes = 256;
    vertices.elementStride = 12;
    auto createdVertices = device.CreateBuffer(vertices);
    if (!createdVertices)
    {
        return false;
    }
    vertexBuffer = createdVertices.Value();

    BufferDesc indices{};
    indices.usage = BufferUsage::Index;
    indices.sizeBytes = 256;
    auto createdIndices = device.CreateBuffer(indices);
    if (!createdIndices)
    {
        return false;
    }
    indexBuffer = createdIndices.Value();

    BufferDesc uniform{};
    uniform.usage = BufferUsage::Uniform;
    uniform.sizeBytes = 256;
    auto createdUniform = device.CreateBuffer(uniform);
    if (!createdUniform)
    {
        return false;
    }
    uniformBuffer = createdUniform.Value();

    BufferDesc indirect{};
    indirect.usage = BufferUsage::Indirect;
    indirect.sizeBytes = 64;
    auto createdIndirect = device.CreateBuffer(indirect);
    if (!createdIndirect)
    {
        return false;
    }
    indirectBuffer = createdIndirect.Value();

    TextureDesc color{};
    color.dimension = TextureDimension::Dim2D;
    color.format = colorFormat;
    color.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    color.width = width;
    color.height = height;
    auto createdColor = device.CreateTexture(color);
    if (!createdColor)
    {
        return false;
    }
    colorTexture = createdColor.Value();

    TextureViewDesc colorViewDesc{};
    colorViewDesc.texture = colorTexture;
    colorViewDesc.format = colorFormat;
    colorViewDesc.dimension = TextureDimension::Dim2D;
    auto createdColorView = device.CreateTextureView(colorViewDesc);
    if (!createdColorView)
    {
        return false;
    }
    colorView = createdColorView.Value();

    TextureDesc depth{};
    depth.dimension = TextureDimension::Dim2D;
    depth.format = Format::D32Float;
    depth.usage = TextureUsage::DepthStencil;
    depth.width = width;
    depth.height = height;
    auto createdDepth = device.CreateTexture(depth);
    if (!createdDepth)
    {
        return false;
    }
    depthTexture = createdDepth.Value();

    TextureViewDesc depthViewDesc{};
    depthViewDesc.texture = depthTexture;
    depthViewDesc.format = Format::D32Float;
    depthViewDesc.dimension = TextureDimension::Dim2D;
    depthViewDesc.range.aspect = TextureAspect::Depth;
    auto createdDepthView = device.CreateTextureView(depthViewDesc);
    if (!createdDepthView)
    {
        return false;
    }
    depthView = createdDepthView.Value();

    const BindGroupLayoutEntry entry{0, BindingType::UniformBuffer,
                                     ShaderStageBit(ShaderStage::Vertex), 1};
    BindGroupLayoutDesc groupLayoutDesc{};
    groupLayoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto createdGroupLayout = device.CreateBindGroupLayout(groupLayoutDesc);
    if (!createdGroupLayout)
    {
        return false;
    }
    groupLayout = createdGroupLayout.Value();

    BindingResource resource{};
    resource.type = BindingType::UniformBuffer;
    resource.buffer = uniformBuffer;
    resource.offset = 0;
    resource.size = 256;
    BindGroupDesc groupDesc{};
    groupDesc.layout = groupLayout;
    groupDesc.resources = Span<const BindingResource>(&resource, 1);
    auto createdGroup = device.CreateBindGroup(groupDesc);
    if (!createdGroup)
    {
        return false;
    }
    group = createdGroup.Value();

    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerMaterial)] = groupLayout;
    auto createdLayout = device.CreatePipelineLayout(layoutDesc);
    if (!createdLayout)
    {
        return false;
    }
    pipelineLayout = createdLayout.Value();

    VertexBinding binding{};
    binding.strideBytes = 12;
    VertexAttribute attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = Format::R32G32B32Float;
    VertexLayout vertexLayout{};
    vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
    vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);
    ColorAttachmentState attachmentState{};
    attachmentState.format = colorFormat;

    GraphicsPipelineDesc graphics = GraphicsDesc();
    graphics.vertexLayout = vertexLayout;
    graphics.colorAttachments = Span<const ColorAttachmentState>(&attachmentState, 1);
    auto createdGraphics = device.CreateGraphicsPipeline(graphics);
    if (!createdGraphics)
    {
        return false;
    }
    graphicsPipeline = createdGraphics.Value();

    ComputePipelineDesc computeDesc{};
    computeDesc.layout = pipelineLayout;
    computeDesc.computeShader = computeShader;
    auto createdCompute = device.CreateComputePipeline(computeDesc);
    if (!createdCompute)
    {
        return false;
    }
    computePipeline = createdCompute.Value();
    return true;
}

inline GraphicsPipelineDesc Fixture::GraphicsDesc() noexcept
{
    GraphicsPipelineDesc desc{};
    desc.layout = pipelineLayout;
    desc.vertexShader = vertexShader;
    desc.pixelShader = pixelShader;
    desc.topology = PrimitiveTopology::TriangleList;
    desc.depthStencilFormat = Format::D32Float;
    return desc;
}

inline bool Fixture::MakeSampledTexture(TextureHandle* texture, TextureViewHandle* view,
                                        u32 textureWidth, u32 textureHeight) noexcept
{
    TextureDesc desc{};
    desc.dimension = TextureDimension::Dim2D;
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
    desc.width = textureWidth;
    desc.height = textureHeight;
    auto created = device.CreateTexture(desc);
    if (!created)
    {
        return false;
    }
    *texture = created.Value();

    TextureViewDesc viewDesc{};
    viewDesc.texture = *texture;
    viewDesc.format = Format::RGBA8Unorm;
    viewDesc.dimension = TextureDimension::Dim2D;
    auto createdView = device.CreateTextureView(viewDesc);
    if (!createdView)
    {
        return false;
    }
    *view = createdView.Value();
    return true;
}

inline bool Fixture::TransitionTargets(CommandList& list) noexcept
{
    const TextureBarrier barriers[2] = {
        TextureBarrier{colorTexture, SubresourceRange{}, ResourceState::Undefined,
                       ResourceState::RenderTarget},
        TextureBarrier{depthTexture, SubresourceRange{0, 0, 0, 0, TextureAspect::Depth},
                       ResourceState::Undefined, ResourceState::DepthWrite},
    };
    return list.Barrier({}, Span<const TextureBarrier>(barriers, 2)).IsOk();
}

inline bool Fixture::BeginPass(CommandList& list) noexcept
{
    ColorAttachment attachment{};
    attachment.view = colorView;
    attachment.loadOp = LoadOp::Clear;
    attachment.storeOp = StoreOp::Store;
    attachment.clearValue = ClearColorValue::Of(0.05f, 0.05f, 0.08f, 1.0f);

    RenderPassDesc pass{};
    pass.colorAttachments = Span<const ColorAttachment>(&attachment, 1);
    pass.hasDepthStencil = true;
    pass.depthStencil.view = depthView;
    pass.depthStencil.depthLoadOp = LoadOp::Clear;
    pass.depthStencil.depthStoreOp = StoreOp::Store;
    pass.depthStencil.clearValue = ClearDepthStencilValue::Of(1.0f);

    if (!list.BeginRenderPass(pass))
    {
        return false;
    }
    if (!list.SetViewport(Viewport::Of(static_cast<float>(width), static_cast<float>(height))))
    {
        return false;
    }
    return list.SetScissor(ScissorRect{0, 0, width, height}).IsOk();
}

}  // namespace cyber::rhi::test
