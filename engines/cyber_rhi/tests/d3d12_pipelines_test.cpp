#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/shader.h>
#include <cyber/test/test.h>

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

// A vertex shader whose inputs are addressed by location: the backend maps
// location N to the `TEXCOORD` semantic with index N, so a vertex layout and a
// shader agree without either naming a semantic.
constexpr char kVertexSource[] = "struct Input\n"
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

constexpr char kPixelSource[] = "float4 main(float4 position : SV_Position,\n"
                                "             float2 uv : TEXCOORD0) : SV_Target\n"
                                "{\n"
                                "    return float4(uv, 0.25, 1.0);\n"
                                "}\n";

constexpr char kComputeSource[] = "[numthreads(8, 8, 1)]\n"
                                  "void main(uint3 id : SV_DispatchThreadID)\n"
                                  "{\n"
                                  "}\n";

// One used slot is the binding contract's minimum layout; the pipeline never
// records a draw, so the group is declared but not bound here, and the group
// layout releases with the device.
[[nodiscard]] bool MakeLayout(Device& device, PipelineLayoutHandle& layout) noexcept
{
    const BindGroupLayoutEntry entry{0, BindingType::UniformBuffer,
                                     ShaderStageBit(ShaderStage::Pixel), 1};
    BindGroupLayoutDesc groupDesc{};
    groupDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto groupLayout = device.CreateBindGroupLayout(groupDesc);
    if (!groupLayout)
    {
        return false;
    }
    PipelineLayoutDesc layoutDesc{};
    layoutDesc.groups[static_cast<u32>(BindGroupSlot::PerMaterial)] = groupLayout.Value();
    auto created = device.CreatePipelineLayout(layoutDesc);
    if (!created)
    {
        return false;
    }
    layout = created.Value();
    return true;
}

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
    // The last adapter is WARP when a machine has no GPU, so this opens whatever
    // the API found rather than assuming hardware.
    desc.adapterIndex = count - 1;
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

// Everything a graphics pipeline needs, so each test changes only the field it is
// about. `layout`, the shaders, and the attachment are filled by the caller.
struct GraphicsFixture
{
    PipelineLayoutHandle layout{};
    ShaderHandle vertex{};
    ShaderHandle pixel{};
    VertexBinding binding{32, VertexInputRate::PerVertex};
    VertexAttribute attributes[2]{};
    ColorAttachmentState attachment{};
};

[[nodiscard]] GraphicsPipelineDesc MakeGraphicsDesc(GraphicsFixture& fixture) noexcept
{
    fixture.attributes[0].location = 0;
    fixture.attributes[0].binding = 0;
    fixture.attributes[0].format = Format::R32G32B32Float;
    fixture.attributes[0].offsetBytes = 0;

    fixture.attributes[1].location = 1;
    fixture.attributes[1].binding = 0;
    fixture.attributes[1].format = Format::RG32Float;
    fixture.attributes[1].offsetBytes = 12;

    fixture.attachment.format = Format::RGBA8Unorm;

    GraphicsPipelineDesc desc{};
    desc.layout = fixture.layout;
    desc.vertexShader = fixture.vertex;
    desc.pixelShader = fixture.pixel;
    desc.vertexLayout.bindings = Span<const VertexBinding>(&fixture.binding, 1);
    desc.vertexLayout.attributes = Span<const VertexAttribute>(fixture.attributes, 2);
    desc.depthStencilFormat = Format::D32Float;
    desc.colorAttachments = Span<const ColorAttachmentState>(&fixture.attachment, 1);
    desc.label = "triangle";
    return desc;
}

}  // namespace

CG_TEST(d3d12_pipelines_create_a_graphics_pipeline)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    PipelineLayoutHandle layout{};
    CG_REQUIRE(MakeLayout(device, layout));

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.code = Bytes(kVertexSource);
    vertexDesc.entryPoint = "main";
    auto vertex = device.CreateShader(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertex));

    ShaderDesc pixelDesc{};
    pixelDesc.stage = ShaderStage::Pixel;
    pixelDesc.code = Bytes(kPixelSource);
    pixelDesc.entryPoint = "main";
    auto pixel = device.CreateShader(pixelDesc);
    CG_REQUIRE(static_cast<bool>(pixel));

    GraphicsFixture fixture{};
    fixture.layout = layout;
    ;
    fixture.vertex = vertex.Value();
    fixture.pixel = pixel.Value();
    const GraphicsPipelineDesc desc = MakeGraphicsDesc(fixture);

    auto created = device.CreateGraphicsPipeline(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const PipelineHandle pipeline = created.Value();

    const PipelineInfo info = device.PipelineInfoOf(pipeline);
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_TRUE(info.kind == PipelineKind::Graphics);
    // The hash is the one the description computes, so a cache key derived from
    // the description addresses this object.
    CG_EXPECT_EQ(info.hash, HashGraphicsPipelineDesc(desc));

    // A second pipeline with the same description is a distinct handle with the
    // same hash: descriptions are identity, handles are lifetime.
    auto again = device.CreateGraphicsPipeline(desc);
    CG_REQUIRE(static_cast<bool>(again));
    CG_EXPECT_FALSE(again.Value() == pipeline);
    CG_EXPECT_EQ(device.PipelineInfoOf(again.Value()).hash, info.hash);

    CG_EXPECT_TRUE(device.DestroyGraphicsPipeline(pipeline).IsOk());
    CG_EXPECT_FALSE(device.PipelineInfoOf(pipeline).valid);
    CG_EXPECT_FALSE(device.DestroyGraphicsPipeline(pipeline));
    CG_EXPECT_TRUE(device.DestroyGraphicsPipeline(again.Value()).IsOk());
}

CG_TEST(d3d12_pipelines_reject_incomplete_descriptions)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    PipelineLayoutHandle layout{};
    CG_REQUIRE(MakeLayout(device, layout));

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.code = Bytes(kVertexSource);
    vertexDesc.entryPoint = "main";
    auto vertex = device.CreateShader(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertex));

    ShaderDesc pixelDesc{};
    pixelDesc.stage = ShaderStage::Pixel;
    pixelDesc.code = Bytes(kPixelSource);
    pixelDesc.entryPoint = "main";
    auto pixel = device.CreateShader(pixelDesc);
    CG_REQUIRE(static_cast<bool>(pixel));

    GraphicsFixture fixture{};
    fixture.layout = layout;
    ;
    fixture.vertex = vertex.Value();
    fixture.pixel = pixel.Value();

    // No color attachment: a pipeline has to write somewhere.
    GraphicsPipelineDesc noAttachment = MakeGraphicsDesc(fixture);
    noAttachment.colorAttachments = Span<const ColorAttachmentState>{};
    const auto noColor = device.CreateGraphicsPipeline(noAttachment);
    CG_EXPECT_FALSE(noColor);
    if (!noColor)
    {
        CG_EXPECT_EQ(noColor.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // Depth state without a depth format: the two disagree.
    GraphicsPipelineDesc noDepthFormat = MakeGraphicsDesc(fixture);
    noDepthFormat.depthStencilFormat = Format::Count;
    const auto noDepth = device.CreateGraphicsPipeline(noDepthFormat);
    CG_EXPECT_FALSE(noDepth);
    if (!noDepth)
    {
        CG_EXPECT_EQ(noDepth.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // An attribute that runs past its binding's stride would read another
    // vertex's bytes.
    GraphicsFixture overrun = fixture;
    GraphicsPipelineDesc pastStride = MakeGraphicsDesc(overrun);
    overrun.attributes[1].offsetBytes = 28;
    pastStride.vertexLayout.attributes = Span<const VertexAttribute>(overrun.attributes, 2);
    const auto overrunResult = device.CreateGraphicsPipeline(pastStride);
    CG_EXPECT_FALSE(overrunResult);
    if (!overrunResult)
    {
        CG_EXPECT_EQ(overrunResult.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // A vertex shader in the pixel slot is refused before the API sees it.
    GraphicsFixture swapped = fixture;
    swapped.pixel = vertex.Value();
    const GraphicsPipelineDesc wrongStage = MakeGraphicsDesc(swapped);
    const auto stageResult = device.CreateGraphicsPipeline(wrongStage);
    CG_EXPECT_FALSE(stageResult);
    if (!stageResult)
    {
        CG_EXPECT_EQ(stageResult.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // A pipeline layout that has been destroyed cannot produce a pipeline.
    CG_EXPECT_TRUE(device.DestroyPipelineLayout(layout).IsOk());
    GraphicsFixture stale = fixture;
    const GraphicsPipelineDesc staleDesc = MakeGraphicsDesc(stale);
    const auto staleResult = device.CreateGraphicsPipeline(staleDesc);
    CG_EXPECT_FALSE(staleResult);
    if (!staleResult)
    {
        CG_EXPECT_EQ(staleResult.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
}

CG_TEST(d3d12_pipelines_refuse_shaders_that_do_not_fit_the_state)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    PipelineLayoutHandle layout{};
    CG_REQUIRE(MakeLayout(device, layout));

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.code = Bytes(kVertexSource);
    vertexDesc.entryPoint = "main";
    auto vertex = device.CreateShader(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertex));

    ShaderDesc pixelDesc{};
    pixelDesc.stage = ShaderStage::Pixel;
    pixelDesc.code = Bytes(kPixelSource);
    pixelDesc.entryPoint = "main";
    auto pixel = device.CreateShader(pixelDesc);
    CG_REQUIRE(static_cast<bool>(pixel));

    // The layout declares two inputs, but the pixel shader's second input is not
    // a vertex input; the vertex stage here is fed by a layout whose attribute
    // formats cannot produce the shader's float2 at location 0. Direct3D 12
    // refuses it, and the backend turns that into an argument error rather than an
    // internal failure.
    GraphicsFixture fixture{};
    fixture.layout = layout;
    ;
    fixture.vertex = vertex.Value();
    fixture.pixel = pixel.Value();
    GraphicsPipelineDesc desc = MakeGraphicsDesc(fixture);
    GraphicsFixture bad = fixture;

    // A three-component float attribute where the shader reads two components of
    // a two-component input: the format's size (8) still fits the stride, so this
    // is the case the API decides, not this backend.
    bad.attributes[1].format = Format::R32G32B32Float;
    desc.vertexLayout.attributes = Span<const VertexAttribute>(bad.attributes, 2);
    const auto created = device.CreateGraphicsPipeline(desc);
    // A matching pair succeeds; a mismatched one is an argument error. Either
    // answer is correct, but a success must produce a usable pipeline and a
    // failure must not leave anything behind.
    if (created)
    {
        CG_EXPECT_TRUE(device.PipelineInfoOf(created.Value()).valid);
        CG_EXPECT_TRUE(device.DestroyGraphicsPipeline(created.Value()).IsOk());
    }
    else
    {
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
}

CG_TEST(d3d12_pipelines_create_a_compute_pipeline)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    // Direct3D 12 has a compute queue on every device, so the capability must be
    // there; a missing one would mean the backend and the API disagree.
    const Capabilities capabilities = device.Capabilities();
    CG_REQUIRE(capabilities.features.Has(Feature::Compute));

    PipelineLayoutHandle layout{};
    CG_REQUIRE(MakeLayout(device, layout));

    ShaderDesc shaderDesc{};
    shaderDesc.stage = ShaderStage::Compute;
    shaderDesc.code = Bytes(kComputeSource);
    shaderDesc.entryPoint = "main";
    auto shader = device.CreateShader(shaderDesc);
    CG_REQUIRE(static_cast<bool>(shader));

    ComputePipelineDesc desc{};
    desc.layout = layout;
    ;
    desc.computeShader = shader.Value();
    desc.label = "clear";
    auto created = device.CreateComputePipeline(desc);
    CG_REQUIRE(static_cast<bool>(created));

    const PipelineInfo info = device.PipelineInfoOf(created.Value());
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_TRUE(info.kind == PipelineKind::Compute);
    CG_EXPECT_EQ(info.hash, HashComputePipelineDesc(desc));

    // The two destroy entries are not interchangeable: asking the graphics entry
    // to release a compute pipeline is a caller mistake, not a no-op.
    CG_EXPECT_FALSE(device.DestroyGraphicsPipeline(created.Value()).IsOk());
    CG_EXPECT_TRUE(device.PipelineInfoOf(created.Value()).valid);
    CG_EXPECT_TRUE(device.DestroyComputePipeline(created.Value()).IsOk());
    CG_EXPECT_FALSE(device.PipelineInfoOf(created.Value()).valid);
    CG_EXPECT_FALSE(device.DestroyComputePipeline(created.Value()));

    // A graphics shader in the compute slot is refused.
    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.code = Bytes(kVertexSource);
    vertexDesc.entryPoint = "main";
    auto vertex = device.CreateShader(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertex));
    ComputePipelineDesc wrong{};
    wrong.layout = layout;
    ;
    wrong.computeShader = vertex.Value();
    CG_EXPECT_FALSE(device.CreateComputePipeline(wrong));
}

CG_TEST(d3d12_pipelines_cache_round_trip)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenD3D12Device(device, heap));

    PipelineLayoutHandle layout{};
    CG_REQUIRE(MakeLayout(device, layout));

    ShaderDesc vertexDesc{};
    vertexDesc.stage = ShaderStage::Vertex;
    vertexDesc.code = Bytes(kVertexSource);
    vertexDesc.entryPoint = "main";
    auto vertex = device.CreateShader(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertex));

    ShaderDesc pixelDesc{};
    pixelDesc.stage = ShaderStage::Pixel;
    pixelDesc.code = Bytes(kPixelSource);
    pixelDesc.entryPoint = "main";
    auto pixel = device.CreateShader(pixelDesc);
    CG_REQUIRE(static_cast<bool>(pixel));

    PipelineCacheDesc cacheDesc{};
    cacheDesc.label = "triangle cache";
    auto cache = device.CreatePipelineCache(cacheDesc);
    CG_REQUIRE(static_cast<bool>(cache));

    GraphicsFixture fixture{};
    fixture.layout = layout;
    ;
    fixture.vertex = vertex.Value();
    fixture.pixel = pixel.Value();
    GraphicsPipelineDesc desc = MakeGraphicsDesc(fixture);
    desc.cache = cache.Value();

    auto first = device.CreateGraphicsPipeline(desc);
    CG_REQUIRE(static_cast<bool>(first));

    // The same description through the same cache must produce a working
    // pipeline, whether the cache answers from its library or the device
    // compiles it again.
    auto second = device.CreateGraphicsPipeline(desc);
    CG_REQUIRE(static_cast<bool>(second));
    CG_EXPECT_EQ(device.PipelineInfoOf(second.Value()).hash,
                 device.PipelineInfoOf(first.Value()).hash);

    auto bytes = device.PipelineCacheBytes(cache.Value(), heap);
    CG_REQUIRE(static_cast<bool>(bytes));
    // Direct3D 12 provides a pipeline library on every adapter that supports
    // `ID3D12Device1`, so a cache that has stored a state has bytes to save. If a
    // device ever refuses the library the answer is empty and the seed test below
    // is skipped, which is the contract's allowance for a backend without one.
    CG_EXPECT_FALSE(bytes.Value().Empty());

    if (!bytes.Value().Empty())
    {
        // Bytes from a previous run must be accepted, either as a usable seed or
        // by being ignored: the contract forbids rejecting them.
        PipelineCacheDesc seeded{};
        seeded.initialData = Span<const Byte>(bytes.Value().Data(), bytes.Value().Size());
        auto restored = device.CreatePipelineCache(seeded);
        CG_EXPECT_TRUE(static_cast<bool>(restored));
        if (restored)
        {
            GraphicsPipelineDesc reused = MakeGraphicsDesc(fixture);
            reused.cache = restored.Value();
            auto fromCache = device.CreateGraphicsPipeline(reused);
            CG_EXPECT_TRUE(static_cast<bool>(fromCache));
            if (fromCache)
            {
                CG_EXPECT_TRUE(device.DestroyGraphicsPipeline(fromCache.Value()).IsOk());
            }
            CG_EXPECT_TRUE(device.DestroyPipelineCache(restored.Value()).IsOk());
        }
    }

    // Bytes that mean nothing to this device are ignored, not an error.
    const Byte garbage[9] = {Byte{1}, Byte{2}, Byte{3}, Byte{4}, Byte{5},
                             Byte{6}, Byte{7}, Byte{8}, Byte{9}};
    PipelineCacheDesc junk{};
    junk.initialData = Span<const Byte>(garbage, 9);
    auto tolerant = device.CreatePipelineCache(junk);
    CG_EXPECT_TRUE(static_cast<bool>(tolerant));
    if (tolerant)
    {
        CG_EXPECT_TRUE(device.DestroyPipelineCache(tolerant.Value()).IsOk());
    }

    // A pipeline outlives the cache it was created through.
    CG_EXPECT_TRUE(device.DestroyPipelineCache(cache.Value()).IsOk());
    CG_EXPECT_TRUE(device.PipelineInfoOf(first.Value()).valid);
    CG_EXPECT_FALSE(device.DestroyPipelineCache(cache.Value()));
}

CG_TEST(d3d12_pipelines_release_their_state)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenD3D12Device(device, heap));

        PipelineLayoutHandle layout{};
        CG_REQUIRE(MakeLayout(device, layout));

        ShaderDesc shaderDesc{};
        shaderDesc.stage = ShaderStage::Compute;
        shaderDesc.code = Bytes(kComputeSource);
        shaderDesc.entryPoint = "main";
        auto shader = device.CreateShader(shaderDesc);
        CG_REQUIRE(static_cast<bool>(shader));

        ComputePipelineDesc desc{};
        desc.layout = layout;
        ;
        desc.computeShader = shader.Value();
        auto pipeline = device.CreateComputePipeline(desc);
        if (pipeline)
        {
            // Destroying the state returns it rather than leaving it to teardown.
            CG_EXPECT_TRUE(device.DestroyComputePipeline(pipeline.Value()).IsOk());
        }
    }
    // The device's own allocations are all that is left, and they are gone too.
    CG_EXPECT_EQ(heap.Stats().used, u64{0});
}
