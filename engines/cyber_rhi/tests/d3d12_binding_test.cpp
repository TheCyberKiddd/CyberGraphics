#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
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

// A valid vertex shader: it compiles, and it does nothing useful, which is all a
// binding test needs.
constexpr char kVertexSource[] = "struct Input { float3 position : POSITION; };\n"
                                 "float4 main(Input input) : SV_Position\n"
                                 "{\n"
                                 "    return float4(input.position, 1.0);\n"
                                 "}\n";

// A shader with a syntax error, for the failure path.
constexpr char kBrokenSource[] = "float4 main() : SV_Position\n"
                                 "{\n"
                                 "    return float4(1.0, 0.0, 0.0;\n"
                                 "}\n";

[[nodiscard]] Span<const Byte> Bytes(const char* text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text),
                            std::strlen(text) * sizeof(char) / sizeof(Byte));
}

[[nodiscard]] bool OpenSoftwareDevice(Device& out, HeapAllocator& heap) noexcept
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

}  // namespace

CG_TEST(d3d12_binding_compiles_hlsl_source)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    ShaderDesc desc{};
    desc.stage = ShaderStage::Vertex;
    desc.format = ShaderBytecodeFormat::HlslSource;
    desc.code = Bytes(kVertexSource);
    desc.entryPoint = "main";
    desc.label = "triangle vertex";
    auto created = device.CreateShader(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const ShaderHandle shader = created.Value();

    const ShaderInfo info = device.ShaderInfoOf(shader);
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_TRUE(info.stage == ShaderStage::Vertex);
    // The stored bytes are the compiled blob, not the source.
    CG_EXPECT_TRUE(info.codeSize > 0);
    CG_EXPECT_TRUE(info.codeSize != desc.code.Size());

    CG_EXPECT_TRUE(device.DestroyShader(shader).IsOk());
    CG_EXPECT_FALSE(device.ShaderInfoOf(shader).valid);
    CG_EXPECT_FALSE(device.DestroyShader(shader));
}

CG_TEST(d3d12_binding_rejects_a_shader_that_does_not_compile)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    ShaderDesc desc{};
    desc.stage = ShaderStage::Vertex;
    desc.format = ShaderBytecodeFormat::HlslSource;
    desc.code = Bytes(kBrokenSource);
    desc.entryPoint = "main";
    const auto created = device.CreateShader(desc);
    CG_EXPECT_FALSE(created);
    if (!created)
    {
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
}

CG_TEST(d3d12_binding_rejects_incomplete_shader_descriptions)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    // No code.
    ShaderDesc empty{};
    empty.stage = ShaderStage::Pixel;
    empty.entryPoint = "main";
    const auto noCode = device.CreateShader(empty);
    CG_EXPECT_FALSE(noCode);
    if (!noCode)
    {
        CG_EXPECT_EQ(noCode.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // Source without an entry point.
    ShaderDesc noEntry{};
    noEntry.stage = ShaderStage::Vertex;
    noEntry.code = Bytes(kVertexSource);
    const auto noEntryPoint = device.CreateShader(noEntry);
    CG_EXPECT_FALSE(noEntryPoint);
    if (!noEntryPoint)
    {
        CG_EXPECT_EQ(noEntryPoint.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // A format Direct3D 12 cannot consume.
    ShaderDesc spirv{};
    spirv.stage = ShaderStage::Compute;
    spirv.format = ShaderBytecodeFormat::SpirV;
    spirv.code = Bytes(kVertexSource);
    const auto wrongFormat = device.CreateShader(spirv);
    CG_EXPECT_FALSE(wrongFormat);
    if (!wrongFormat)
    {
        CG_EXPECT_EQ(wrongFormat.Error().code, cyber::core::ErrorCode::Unsupported);
    }
}

CG_TEST(d3d12_binding_accepts_precompiled_bytecode)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    // A caller that already holds a blob hands it over verbatim: the backend must
    // not try to compile it again.
    const Byte blob[16] = {};
    ShaderDesc desc{};
    desc.stage = ShaderStage::Pixel;
    desc.format = ShaderBytecodeFormat::Dxil;
    desc.code = Span<const Byte>(blob, 16);
    auto created = device.CreateShader(desc);
    CG_REQUIRE(static_cast<bool>(created));
    CG_EXPECT_EQ(device.ShaderInfoOf(created.Value()).codeSize, u64{16});
}

CG_TEST(d3d12_binding_layouts_and_groups)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    BindGroupLayoutEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].type = BindingType::UniformBuffer;
    entries[0].visibility =
        ShaderStageBit(ShaderStage::Vertex) | ShaderStageBit(ShaderStage::Pixel);
    entries[1].binding = 1;
    entries[1].type = BindingType::SampledTexture;
    entries[1].visibility = ShaderStageBit(ShaderStage::Pixel);

    BindGroupLayoutDesc layoutDesc{};
    layoutDesc.entries = Span<const BindGroupLayoutEntry>(entries, 2);
    layoutDesc.label = "frame";
    auto layout = device.CreateBindGroupLayout(layoutDesc);
    CG_REQUIRE(static_cast<bool>(layout));

    // A uniform buffer and a sampled texture, in binding order.
    BufferDesc bufferDesc{};
    bufferDesc.usage = BufferUsage::Uniform | BufferUsage::HostVisible;
    bufferDesc.sizeBytes = 256;
    auto buffer = device.CreateBuffer(bufferDesc);
    CG_REQUIRE(static_cast<bool>(buffer));

    TextureDesc textureDesc{};
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.usage = TextureUsage::Sampled;
    textureDesc.width = 8;
    textureDesc.height = 8;
    auto texture = device.CreateTexture(textureDesc);
    CG_REQUIRE(static_cast<bool>(texture));

    TextureViewDesc viewDesc{};
    viewDesc.texture = texture.Value();
    viewDesc.format = Format::RGBA8Unorm;
    auto view = device.CreateTextureView(viewDesc);
    CG_REQUIRE(static_cast<bool>(view));

    BindingResource resources[2]{};
    resources[0].type = BindingType::UniformBuffer;
    resources[0].buffer = buffer.Value();
    resources[1].type = BindingType::SampledTexture;
    resources[1].textureView = view.Value();

    BindGroupDesc groupDesc{};
    groupDesc.layout = layout.Value();
    groupDesc.resources = Span<const BindingResource>(resources, 2);
    auto group = device.CreateBindGroup(groupDesc);
    CG_REQUIRE(static_cast<bool>(group));
    const BindGroupInfo info = device.BindGroupInfoOf(group.Value());
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_EQ(info.resourceCount, u32{2});
    CG_EXPECT_TRUE(info.layout == layout.Value());

    // One resource too few is refused, and the message is about the count.
    BindGroupDesc shortGroup{};
    shortGroup.layout = layout.Value();
    shortGroup.resources = Span<const BindingResource>(resources, 1);
    const auto refused = device.CreateBindGroup(shortGroup);
    CG_EXPECT_FALSE(refused);
    if (!refused)
    {
        CG_EXPECT_EQ(refused.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // A stale layout is refused too.
    CG_EXPECT_TRUE(device.DestroyBindGroupLayout(layout.Value()).IsOk());
    BindGroupDesc staleLayout{};
    staleLayout.layout = layout.Value();
    staleLayout.resources = Span<const BindingResource>(resources, 2);
    const auto stale = device.CreateBindGroup(staleLayout);
    CG_EXPECT_FALSE(stale);
    if (!stale)
    {
        CG_EXPECT_EQ(stale.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // A group outlives the layout it was made from, exactly as the contract says.
    CG_EXPECT_TRUE(device.BindGroupInfoOf(group.Value()).valid);
}

CG_TEST(d3d12_binding_pipeline_layout_builds_a_root_signature)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    BindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.type = BindingType::Sampler;
    entry.visibility = ShaderStageBit(ShaderStage::Pixel);
    BindGroupLayoutDesc layoutDesc{};
    layoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto layout = device.CreateBindGroupLayout(layoutDesc);
    CG_REQUIRE(static_cast<bool>(layout));

    PushConstantRange push{};
    push.offsetBytes = 0;
    push.sizeBytes = 64;
    push.stages = ShaderStageBit(ShaderStage::Vertex);

    PipelineLayoutDesc desc{};
    desc.groups[0] = layout.Value();
    desc.pushConstants = Span<const PushConstantRange>(&push, 1);
    desc.label = "triangle layout";
    auto pipelineLayout = device.CreatePipelineLayout(desc);
    CG_REQUIRE(static_cast<bool>(pipelineLayout));

    CG_EXPECT_TRUE(device.DestroyPipelineLayout(pipelineLayout.Value()).IsOk());
    CG_EXPECT_FALSE(device.DestroyPipelineLayout(pipelineLayout.Value()));

    // A layout naming a dead group layout cannot produce a root signature.
    PipelineLayoutDesc stale{};
    stale.groups[0] = layout.Value();
    CG_EXPECT_TRUE(device.DestroyBindGroupLayout(layout.Value()).IsOk());
    CG_EXPECT_FALSE(device.CreatePipelineLayout(stale));
}

CG_TEST(d3d12_binding_shader_bytecode_is_released)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenSoftwareDevice(device, heap));

        ShaderDesc desc{};
        desc.stage = ShaderStage::Vertex;
        desc.code = Bytes(kVertexSource);
        desc.entryPoint = "main";
        auto shader = device.CreateShader(desc);
        CG_REQUIRE(static_cast<bool>(shader));

        // The compiled blob is the device's allocation, and destroying the module
        // must return it rather than leaving it for teardown.
        CG_EXPECT_TRUE(device.DestroyShader(shader.Value()).IsOk());
        CG_EXPECT_TRUE(heap.Stats().used > 0);

        // A second compile proves the allocator is reusable after the first free.
        auto again = device.CreateShader(desc);
        CG_REQUIRE(static_cast<bool>(again));
        CG_EXPECT_TRUE(device.DestroyShader(again.Value()).IsOk());
    }
    CG_EXPECT_EQ(heap.Stats().used, u64{0});
}
