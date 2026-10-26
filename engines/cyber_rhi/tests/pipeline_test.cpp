#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

bool OpenDevice(Device& out, HeapAllocator& heap) noexcept
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

Span<const Byte> AsBytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text), std::strlen(text));
}

ShaderDesc MakeShader(ShaderStage stage) noexcept
{
    ShaderDesc desc{};
    desc.stage = stage;
    desc.format = ShaderBytecodeFormat::HlslSource;
    desc.code = AsBytes("void main() {}");
    desc.entryPoint = "main";
    return desc;
}

struct Fixture
{
    Device* device = nullptr;
    ShaderHandle vertexShader;
    ShaderHandle pixelShader;
    ShaderHandle computeShader;
    BindGroupLayoutHandle groupLayout;
    PipelineLayoutHandle pipelineLayout;
    PipelineCacheHandle cache;
};

bool BuildFixture(Device& device, Fixture& out) noexcept
{
    out.device = &device;

    auto vertex = device.CreateShader(MakeShader(ShaderStage::Vertex));
    auto pixel = device.CreateShader(MakeShader(ShaderStage::Pixel));
    auto compute = device.CreateShader(MakeShader(ShaderStage::Compute));
    auto cache = device.CreatePipelineCache(PipelineCacheDesc{});
    if (!vertex || !pixel || !compute || !cache)
    {
        return false;
    }
    out.vertexShader = vertex.Value();
    out.pixelShader = pixel.Value();
    out.computeShader = compute.Value();
    out.cache = cache.Value();

    const BindGroupLayoutEntry entry{0, BindingType::UniformBuffer,
                                     ShaderStageBit(ShaderStage::Vertex), 1};
    BindGroupLayoutDesc groupDesc{};
    groupDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto groupLayout = device.CreateBindGroupLayout(groupDesc);
    if (!groupLayout)
    {
        return false;
    }
    out.groupLayout = groupLayout.Value();

    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerMaterial)] = out.groupLayout;
    auto layout = device.CreatePipelineLayout(layoutDesc);
    if (!layout)
    {
        return false;
    }
    out.pipelineLayout = layout.Value();
    return true;
}

// A graphics description that is valid until a test changes one field.
struct GraphicsFixture
{
    VertexBinding binding{};
    VertexAttribute attribute{};
    ColorAttachmentState attachment{};
    VertexLayout vertexLayout{};
    GraphicsPipelineDesc desc{};

    explicit GraphicsFixture(const Fixture& fixture) noexcept
    {
        binding.strideBytes = 12;
        attribute.location = 0;
        attribute.binding = 0;
        attribute.format = Format::R32G32B32Float;
        attachment.format = Format::RGBA8Unorm;

        vertexLayout.bindings = Span<const VertexBinding>(&binding, 1);
        vertexLayout.attributes = Span<const VertexAttribute>(&attribute, 1);

        desc.layout = fixture.pipelineLayout;
        desc.vertexShader = fixture.vertexShader;
        desc.pixelShader = fixture.pixelShader;
        desc.vertexLayout = vertexLayout;
        desc.depthStencilFormat = Format::D32Float;
        desc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
        desc.label = "test pipeline";
    }

    void RefreshAttachments() noexcept
    {
        desc.colorAttachments = Span<const ColorAttachmentState>(&attachment, 1);
    }
};

}  // namespace

CG_TEST(rhi_pipeline_graphics_create)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));
        Fixture fixture{};
        CG_REQUIRE(BuildFixture(device, fixture));
        GraphicsFixture graphics(fixture);

        auto created = device.CreateGraphicsPipeline(graphics.desc);
        CG_REQUIRE(static_cast<bool>(created));
        const PipelineHandle pipeline = created.Value();

        const PipelineInfo info = device.PipelineInfoOf(pipeline);
        CG_EXPECT_TRUE(info.valid);
        CG_EXPECT_EQ(info.kind, PipelineKind::Graphics);
        CG_EXPECT_EQ(info.hash, HashGraphicsPipelineDesc(graphics.desc));
        CG_EXPECT_NE(info.hash, u64{0});

        CG_EXPECT_TRUE(device.DestroyGraphicsPipeline(pipeline));
        CG_EXPECT_FALSE(device.PipelineInfoOf(pipeline).valid);
        CG_EXPECT_FALSE(device.DestroyGraphicsPipeline(pipeline));

        // Destroying a 4x multisampled pipeline's dependencies afterwards is
        // legal: a pipeline copies what it needs.
        CG_EXPECT_TRUE(device.DestroyShader(fixture.vertexShader));
        CG_EXPECT_TRUE(device.DestroyShader(fixture.pixelShader));
        CG_EXPECT_TRUE(device.DestroyShader(fixture.computeShader));
        CG_EXPECT_TRUE(device.DestroyPipelineLayout(fixture.pipelineLayout));
        CG_EXPECT_TRUE(device.DestroyBindGroupLayout(fixture.groupLayout));
        CG_EXPECT_TRUE(device.DestroyPipelineCache(fixture.cache));
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_pipeline_shader_stages)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    Fixture fixture{};
    CG_REQUIRE(BuildFixture(device, fixture));
    GraphicsFixture graphics(fixture);

    // A pixel shader cannot stand in for a vertex shader, and a compute shader
    // cannot either.
    graphics.desc.vertexShader = fixture.pixelShader;
    auto created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    graphics.desc.vertexShader = fixture.vertexShader;
    graphics.desc.pixelShader = fixture.computeShader;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.pixelShader = fixture.pixelShader;

    // Stale handles are rejected for the shaders, the layout, and the cache.
    graphics.desc.layout = PipelineLayoutHandle{};
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.layout = fixture.pipelineLayout;

    graphics.desc.vertexShader = ShaderHandle{};
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.vertexShader = fixture.vertexShader;

    graphics.desc.cache = PipelineCacheHandle{1, 99};
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.cache = PipelineCacheHandle{};

    // A compute pipeline with a pixel shader is a stage mismatch.
    ComputePipelineDesc compute{};
    compute.layout = fixture.pipelineLayout;
    compute.computeShader = fixture.pixelShader;
    created = device.CreateComputePipeline(compute);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_pipeline_vertex_layout)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    Fixture fixture{};
    CG_REQUIRE(BuildFixture(device, fixture));
    GraphicsFixture graphics(fixture);

    graphics.desc.vertexLayout.bindings = Span<const VertexBinding>();
    auto created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    graphics.desc.vertexLayout.bindings = Span<const VertexBinding>(&graphics.binding, 1);
    graphics.desc.vertexLayout.attributes = Span<const VertexAttribute>();
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.vertexLayout.attributes = Span<const VertexAttribute>(&graphics.attribute, 1);

    // A stride of zero or above 2048 cannot describe an element.
    graphics.binding.strideBytes = 0;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.binding.strideBytes = kMaxVertexStrideBytes + 1;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.binding.strideBytes = 12;

    // An attribute that points at no binding, or runs past the stride, is
    // rejected with the rule that failed.
    graphics.attribute.binding = 1;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attribute.binding = 0;

    graphics.attribute.offsetBytes = 8;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attribute.offsetBytes = 0;

    // Compressed and depth formats are not vertex input.
    graphics.attribute.format = Format::BC1Unorm;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attribute.format = Format::D32Float;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attribute.format = Format::R32G32B32Float;

    // Two attributes at one location would feed one shader input twice.
    VertexAttribute second{};
    second.location = 0;
    second.binding = 0;
    second.format = Format::RG32Float;
    VertexAttribute attributes[2] = {graphics.attribute, second};
    graphics.desc.vertexLayout.attributes = Span<const VertexAttribute>(attributes, 2);
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.vertexLayout.attributes = Span<const VertexAttribute>(&graphics.attribute, 1);

    CG_EXPECT_TRUE(static_cast<bool>(device.CreateGraphicsPipeline(graphics.desc)));
}

CG_TEST(rhi_pipeline_attachment_and_depth_rules)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    Fixture fixture{};
    CG_REQUIRE(BuildFixture(device, fixture));
    GraphicsFixture graphics(fixture);

    // No color attachment means nothing to write.
    graphics.desc.colorAttachments = Span<const ColorAttachmentState>();
    auto created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // A compressed or depth format cannot be a color target.
    graphics.attachment.format = Format::BC1Unorm;
    graphics.RefreshAttachments();
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attachment.format = Format::D32Float;
    graphics.RefreshAttachments();
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // Blending an integer target, or setting a write mask bit that does not
    // exist, is rejected.
    graphics.attachment.format = Format::R32Uint;
    graphics.attachment.blend.enabled = true;
    graphics.RefreshAttachments();
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attachment.blend.enabled = false;
    graphics.attachment.blend.writeMask = 0x10;
    graphics.RefreshAttachments();
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    graphics.attachment.format = Format::RGBA8Unorm;
    graphics.attachment.blend.writeMask = kColorWriteAll;
    graphics.RefreshAttachments();

    // Nine attachments exceed the fixed maximum.
    ColorAttachmentState nine[9] = {};
    for (u32 index = 0; index < 9; ++index)
    {
        nine[index].format = Format::RGBA8Unorm;
    }
    graphics.desc.colorAttachments = Span<const ColorAttachmentState>(nine, 9);
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);
    graphics.desc.colorAttachments = Span<const ColorAttachmentState>(&graphics.attachment, 1);

    // Depth state without a depth attachment, a color depth format, and stencil
    // on a format without stencil are all contradictions.
    graphics.desc.depthStencilFormat = Format::Count;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    graphics.desc.depthStencilFormat = Format::RGBA8Unorm;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    graphics.desc.depthStencilFormat = Format::D32Float;
    graphics.desc.depthStencil.stencilEnabled = true;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.depthStencil.stencilEnabled = false;

    // A pipeline needs no depth attachment only when it also has no depth state.
    graphics.desc.depthStencil.depthTestEnabled = false;
    graphics.desc.depthStencil.depthWriteEnabled = false;
    graphics.desc.depthStencilFormat = Format::Count;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateGraphicsPipeline(graphics.desc)));

    graphics.desc.depthStencil.depthTestEnabled = true;
    graphics.desc.depthStencil.depthWriteEnabled = true;
    graphics.desc.depthStencilFormat = Format::D32Float;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateGraphicsPipeline(graphics.desc)));
}

CG_TEST(rhi_pipeline_multisample_rules)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    Fixture fixture{};
    CG_REQUIRE(BuildFixture(device, fixture));
    GraphicsFixture graphics(fixture);

    graphics.desc.multisample.sampleCount = 3;
    auto created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    graphics.desc.multisample.sampleCount = 4;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateGraphicsPipeline(graphics.desc)));

    // Alpha to coverage needs samples to cover.
    graphics.desc.multisample.sampleCount = 1;
    graphics.desc.multisample.alphaToCoverage = true;
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.desc.multisample.alphaToCoverage = false;

    // A multisampled pipeline cannot use a format that has no samples.
    graphics.desc.multisample.sampleCount = 4;
    graphics.attachment.format = Format::BC1Unorm;
    graphics.RefreshAttachments();
    created = device.CreateGraphicsPipeline(graphics.desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    graphics.attachment.format = Format::RGBA8Unorm;
    graphics.RefreshAttachments();
}

CG_TEST(rhi_pipeline_compute_and_cache)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    Fixture fixture{};
    CG_REQUIRE(BuildFixture(device, fixture));

    ComputePipelineDesc compute{};
    compute.layout = fixture.pipelineLayout;
    compute.computeShader = fixture.computeShader;

    auto created = device.CreateComputePipeline(compute);
    CG_REQUIRE(static_cast<bool>(created));
    const PipelineHandle computePipeline = created.Value();
    CG_EXPECT_EQ(device.PipelineInfoOf(computePipeline).kind, PipelineKind::Compute);
    CG_EXPECT_EQ(device.PipelineInfoOf(computePipeline).hash, HashComputePipelineDesc(compute));

    // A cache does not change the pipeline's identity.
    compute.cache = fixture.cache;
    auto cached = device.CreateComputePipeline(compute);
    CG_REQUIRE(static_cast<bool>(cached));
    CG_EXPECT_EQ(device.PipelineInfoOf(cached.Value()).hash,
                 device.PipelineInfoOf(computePipeline).hash);
    CG_EXPECT_NE(cached.Value(), computePipeline);

    // The null backend compiles nothing, so a cache saves an empty blob, which
    // is a successful result a real backend's data would replace.
    auto bytes = device.PipelineCacheBytes(fixture.cache, heap);
    CG_EXPECT_TRUE(static_cast<bool>(bytes));
    if (bytes)
    {
        CG_EXPECT_EQ(bytes.Value().Size(), cyber::core::usize{0});
    }

    // Seeding a cache with bytes it does not understand is accepted; both APIs
    // require a backend to ignore foreign data.
    const Byte seed[4] = {static_cast<Byte>(1), static_cast<Byte>(2), static_cast<Byte>(3),
                          static_cast<Byte>(4)};
    PipelineCacheDesc seedDesc{};
    seedDesc.initialData = Span<const Byte>(seed, 4);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreatePipelineCache(seedDesc)));

    // A stale cache handle cannot be passed to a pipeline.
    CG_EXPECT_TRUE(device.DestroyPipelineCache(fixture.cache));
    compute.cache = fixture.cache;
    const auto stale = device.CreateComputePipeline(compute);
    CG_EXPECT_FALSE(stale);
    CG_EXPECT_EQ(stale.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_FALSE(device.DestroyPipelineCache(fixture.cache));

    // The pipeline created with the cache before it died still resolves.
    CG_EXPECT_TRUE(device.PipelineInfoOf(computePipeline).valid);
}
