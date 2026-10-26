#include <cyber/rhi/pipeline.h>
#include <cyber/test/test.h>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;

namespace
{

struct Fixture
{
    VertexBinding binding{};
    VertexAttribute attribute{};
    ColorAttachmentState attachment{};
    VertexLayout vertexLayout{};
    GraphicsPipelineDesc desc{};

    Fixture() noexcept
    {
        binding.strideBytes = 12;
        binding.rate = VertexInputRate::PerVertex;
        attribute.location = 0;
        attribute.binding = 0;
        attribute.format = Format::R32G32B32Float;
        attribute.offsetBytes = 0;
        attachment.format = Format::RGBA8Unorm;

        vertexLayout.bindings = cyber::memory::Span<const VertexBinding>(&binding, 1);
        vertexLayout.attributes = cyber::memory::Span<const VertexAttribute>(&attribute, 1);

        desc.layout = PipelineLayoutHandle{1, 1};
        desc.vertexShader = ShaderHandle{2, 1};
        desc.pixelShader = ShaderHandle{3, 1};
        desc.vertexLayout = vertexLayout;
        desc.depthStencilFormat = Format::D32Float;
        desc.colorAttachments = cyber::memory::Span<const ColorAttachmentState>(&attachment, 1);
        desc.label = "first";
    }
};

}  // namespace

CG_TEST(rhi_pipeline_hash_is_stable)
{
    Fixture fixture;
    const u64 first = HashGraphicsPipelineDesc(fixture.desc);
    const u64 second = HashGraphicsPipelineDesc(fixture.desc);
    CG_EXPECT_EQ(first, second);
    CG_EXPECT_NE(first, u64{0});

    // A label is not part of the compiled object and must not change identity.
    fixture.desc.label = "a completely different name";
    CG_EXPECT_EQ(HashGraphicsPipelineDesc(fixture.desc), first);

    const ComputePipelineDesc compute{};
    const u64 computeFirst = HashComputePipelineDesc(compute);
    CG_EXPECT_EQ(computeFirst, HashComputePipelineDesc(compute));
    CG_EXPECT_NE(computeFirst, first);
}

CG_TEST(rhi_pipeline_hash_covers_state)
{
    Fixture fixture;
    const u64 baseline = HashGraphicsPipelineDesc(fixture.desc);

    // Every field that changes the compiled object must change the hash.
    fixture.desc.topology = PrimitiveTopology::TriangleStrip;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.topology = PrimitiveTopology::TriangleList;

    fixture.desc.rasterizer.cullMode = CullMode::None;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.rasterizer.cullMode = CullMode::Back;

    fixture.desc.rasterizer.depthBiasConstant = 1.0f;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.rasterizer.depthBiasConstant = 0.0f;

    fixture.desc.multisample.sampleCount = 4;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.multisample.sampleCount = 1;

    fixture.desc.depthStencil.depthCompare = CompareFunction::Greater;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.depthStencil.depthCompare = CompareFunction::Less;

    fixture.desc.depthStencil.front.passOp = StencilOperation::IncrementWrap;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.depthStencil.front.passOp = StencilOperation::Keep;

    fixture.desc.depthStencilFormat = Format::D24UnormS8Uint;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.depthStencilFormat = Format::D32Float;

    fixture.attachment.blend.enabled = true;
    fixture.attachment.blend.color.source = BlendFactor::SourceAlpha;
    fixture.desc.colorAttachments =
        cyber::memory::Span<const ColorAttachmentState>(&fixture.attachment, 1);
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.attachment.blend.enabled = false;
    fixture.attachment.blend.color.source = BlendFactor::One;

    fixture.attachment.format = Format::RGBA16Float;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.attachment.format = Format::RGBA8Unorm;

    fixture.attribute.format = Format::RG32Float;
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.attribute.format = Format::R32G32B32Float;

    // A reloaded shader gets a new generation and therefore a new pipeline.
    fixture.desc.pixelShader = ShaderHandle{3, 2};
    CG_EXPECT_NE(HashGraphicsPipelineDesc(fixture.desc), baseline);
    fixture.desc.pixelShader = ShaderHandle{3, 1};

    // Back to the baseline: the hash is a pure function of the description.
    CG_EXPECT_EQ(HashGraphicsPipelineDesc(fixture.desc), baseline);
}
