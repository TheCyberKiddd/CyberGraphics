#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
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

BindGroupLayoutEntry Entry(u32 binding, BindingType type, ShaderStageMask visibility,
                           u32 arrayCount = 1) noexcept
{
    BindGroupLayoutEntry entry{};
    entry.binding = binding;
    entry.type = type;
    entry.visibility = visibility;
    entry.arrayCount = arrayCount;
    return entry;
}

const BindGroupLayoutEntry kStandardEntries[3] = {
    Entry(0, BindingType::UniformBuffer, ShaderStageBit(ShaderStage::Vertex)),
    Entry(1, BindingType::SampledTexture, ShaderStageBit(ShaderStage::Pixel)),
    Entry(2, BindingType::Sampler, ShaderStageBit(ShaderStage::Pixel)),
};

struct StandardResources
{
    BindGroupLayoutHandle layout;
    BufferHandle uniforms;
    TextureHandle texture;
    TextureViewHandle view;
    SamplerHandle sampler;
};

bool BuildStandard(Device& device, StandardResources& out) noexcept
{
    BufferDesc bufferDesc{};
    bufferDesc.usage = BufferUsage::Uniform;
    bufferDesc.sizeBytes = 512;
    auto buffer = device.CreateBuffer(bufferDesc);
    if (!buffer)
    {
        return false;
    }
    out.uniforms = buffer.Value();

    TextureDesc textureDesc{};
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.usage = TextureUsage::Sampled;
    textureDesc.width = 4;
    textureDesc.height = 4;
    auto texture = device.CreateTexture(textureDesc);
    if (!texture)
    {
        return false;
    }
    out.texture = texture.Value();

    TextureViewDesc viewDesc{};
    viewDesc.texture = out.texture;
    auto view = device.CreateTextureView(viewDesc);
    if (!view)
    {
        return false;
    }
    out.view = view.Value();

    auto sampler = device.CreateSampler(SamplerDesc{});
    if (!sampler)
    {
        return false;
    }
    out.sampler = sampler.Value();

    BindGroupLayoutDesc layoutDesc{};
    layoutDesc.entries = Span<const BindGroupLayoutEntry>(kStandardEntries, 3);
    auto layout = device.CreateBindGroupLayout(layoutDesc);
    if (!layout)
    {
        return false;
    }
    out.layout = layout.Value();
    return true;
}

}  // namespace

CG_TEST(rhi_binding_layout_lifetime)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenDevice(device, heap));
        StandardResources resources{};
        CG_REQUIRE(BuildStandard(device, resources));

        CG_EXPECT_TRUE(resources.layout.IsValid());
        CG_EXPECT_TRUE(device.DestroyBindGroupLayout(resources.layout));
        CG_EXPECT_FALSE(device.DestroyBindGroupLayout(resources.layout));

        CG_EXPECT_TRUE(device.DestroySampler(resources.sampler));
        CG_EXPECT_TRUE(device.DestroyTextureView(resources.view));
        CG_EXPECT_TRUE(device.DestroyTexture(resources.texture));
        CG_EXPECT_TRUE(device.DestroyBuffer(resources.uniforms));
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_binding_layout_invalid_descriptors)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    const Limits limits = device.Capabilities().limits;

    BindGroupLayoutEntry entries[20] = {};
    for (u32 index = 0; index < 20; ++index)
    {
        entries[index] = Entry(index, BindingType::Sampler, ShaderStageBit(ShaderStage::Pixel));
    }

    BindGroupLayoutDesc desc{};
    desc.entries = Span<const BindGroupLayoutEntry>(entries, 0);
    auto created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    desc.entries = Span<const BindGroupLayoutEntry>(entries, limits.maxBindingsPerGroup + 1);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    BindGroupLayoutEntry entry = Entry(0, BindingType::Count, ShaderStageBit(ShaderStage::Pixel));
    desc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    entry = Entry(0, BindingType::Sampler, 0);
    desc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // A bit above the real stages is not a visibility mask.
    entry = Entry(0, BindingType::Sampler, 1U << 7);
    desc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    entry = Entry(0, BindingType::Sampler, ShaderStageBit(ShaderStage::Pixel), 0);
    desc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // Binding numbers start at zero and stop below the device limit.
    entry =
        Entry(limits.maxBindingsPerGroup, BindingType::Sampler, ShaderStageBit(ShaderStage::Pixel));
    desc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);

    // Two entries claiming one number is a descriptor collision.
    const BindGroupLayoutEntry duplicate[2] = {
        Entry(0, BindingType::Sampler, ShaderStageBit(ShaderStage::Pixel)),
        Entry(0, BindingType::SampledTexture, ShaderStageBit(ShaderStage::Pixel)),
    };
    desc.entries = Span<const BindGroupLayoutEntry>(duplicate, 2);
    created = device.CreateBindGroupLayout(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // The largest legal layout is accepted.
    desc.entries = Span<const BindGroupLayoutEntry>(entries, limits.maxBindingsPerGroup);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateBindGroupLayout(desc)));
}

CG_TEST(rhi_binding_group_create_and_info)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    StandardResources resources{};
    CG_REQUIRE(BuildStandard(device, resources));

    BindingResource bindings[3] = {};
    bindings[0].type = BindingType::UniformBuffer;
    bindings[0].buffer = resources.uniforms;
    bindings[0].offset = 0;
    bindings[0].size = 256;
    bindings[1].type = BindingType::SampledTexture;
    bindings[1].textureView = resources.view;
    bindings[2].type = BindingType::Sampler;
    bindings[2].sampler = resources.sampler;

    BindGroupDesc desc{};
    desc.layout = resources.layout;
    desc.resources = Span<const BindingResource>(bindings, 3);
    auto created = device.CreateBindGroup(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const BindGroupHandle group = created.Value();

    const BindGroupInfo info = device.BindGroupInfoOf(group);
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_EQ(info.layout, resources.layout);
    CG_EXPECT_EQ(info.resourceCount, u32{3});

    // A zero size means "the rest of the buffer", and is legal.
    bindings[0].size = 0;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateBindGroup(desc)));

    // Destroying the layout does not break a group: the group copied what it
    // needed at creation.
    CG_EXPECT_TRUE(device.DestroyBindGroupLayout(resources.layout));
    CG_EXPECT_TRUE(device.BindGroupInfoOf(group).valid);
    CG_EXPECT_TRUE(device.DestroyBindGroup(group));
    CG_EXPECT_FALSE(device.BindGroupInfoOf(group).valid);
    CG_EXPECT_FALSE(device.DestroyBindGroup(group));
}

CG_TEST(rhi_binding_group_invalid_resources)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    StandardResources resources{};
    CG_REQUIRE(BuildStandard(device, resources));

    BindingResource bindings[3] = {};
    bindings[0].type = BindingType::UniformBuffer;
    bindings[0].buffer = resources.uniforms;
    bindings[1].type = BindingType::SampledTexture;
    bindings[1].textureView = resources.view;
    bindings[2].type = BindingType::Sampler;
    bindings[2].sampler = resources.sampler;

    BindGroupDesc desc{};
    desc.layout = resources.layout;
    desc.resources = Span<const BindingResource>(bindings, 3);

    // Too many and too few resources.
    desc.resources = Span<const BindingResource>(bindings, 2);
    auto created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    BindingResource four[4] = {};
    four[0] = bindings[0];
    four[1] = bindings[1];
    four[2] = bindings[2];
    four[3] = bindings[2];
    desc.resources = Span<const BindingResource>(four, 4);
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);

    // A resource whose type does not match its slot.
    desc.resources = Span<const BindingResource>(bindings, 3);
    bindings[1].type = BindingType::StorageTexture;
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    bindings[1].type = BindingType::SampledTexture;

    // A uniform binding needs uniform memory, not a vertex buffer.
    BufferDesc vertexDesc{};
    vertexDesc.usage = BufferUsage::Vertex;
    vertexDesc.sizeBytes = 512;
    auto vertex = device.CreateBuffer(vertexDesc);
    CG_REQUIRE(static_cast<bool>(vertex));
    bindings[0].buffer = vertex.Value();
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    bindings[0].buffer = resources.uniforms;

    // Offsets must respect the device's uniform alignment.
    bindings[0].offset = 1;
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    bindings[0].offset = 0;

    // A range past the end, and a zero-size range at the very end, are both
    // outside the buffer.
    bindings[0].offset = 256;
    bindings[0].size = 512;
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);
    bindings[0].offset = 512;
    bindings[0].size = 0;
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::OutOfRange);
    bindings[0].offset = 0;

    // Stale resources are caught before any range arithmetic.
    BufferDesc tempDesc{};
    tempDesc.usage = BufferUsage::Uniform;
    tempDesc.sizeBytes = 256;
    auto temp = device.CreateBuffer(tempDesc);
    CG_REQUIRE(static_cast<bool>(temp));
    const BufferHandle staleBuffer = temp.Value();
    CG_EXPECT_TRUE(device.DestroyBuffer(staleBuffer));
    bindings[0].buffer = staleBuffer;
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    bindings[0].buffer = resources.uniforms;

    // A sampled binding cannot read a storage-only texture.
    TextureDesc storageDesc{};
    storageDesc.format = Format::RGBA8Unorm;
    storageDesc.usage = TextureUsage::Storage;
    storageDesc.width = 4;
    storageDesc.height = 4;
    auto storageTexture = device.CreateTexture(storageDesc);
    CG_REQUIRE(static_cast<bool>(storageTexture));
    TextureViewDesc storageViewDesc{};
    storageViewDesc.texture = storageTexture.Value();
    auto storageView = device.CreateTextureView(storageViewDesc);
    CG_REQUIRE(static_cast<bool>(storageView));
    bindings[0].buffer = resources.uniforms;
    bindings[1].textureView = storageView.Value();
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    bindings[1].textureView = resources.view;

    // And a stale sampler is rejected.
    bindings[2].sampler = SamplerHandle{};
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
    bindings[2].sampler = resources.sampler;

    // The same descriptor that just failed everywhere now succeeds.
    CG_EXPECT_TRUE(static_cast<bool>(device.CreateBindGroup(desc)));

    // A stale layout is rejected.
    CG_EXPECT_TRUE(device.DestroyBindGroupLayout(resources.layout));
    created = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_binding_group_array_count)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    const BindGroupLayoutEntry entry =
        Entry(0, BindingType::Sampler, ShaderStageBit(ShaderStage::Pixel), 3);
    BindGroupLayoutDesc layoutDesc{};
    layoutDesc.entries = Span<const BindGroupLayoutEntry>(&entry, 1);
    auto layout = device.CreateBindGroupLayout(layoutDesc);
    CG_REQUIRE(static_cast<bool>(layout));

    auto sampler = device.CreateSampler(SamplerDesc{});
    CG_REQUIRE(static_cast<bool>(sampler));

    BindingResource resources[3] = {};
    for (u32 index = 0; index < 3; ++index)
    {
        resources[index].type = BindingType::Sampler;
        resources[index].sampler = sampler.Value();
    }

    BindGroupDesc desc{};
    desc.layout = layout.Value();
    desc.resources = Span<const BindingResource>(resources, 3);
    auto group = device.CreateBindGroup(desc);
    CG_REQUIRE(static_cast<bool>(group));
    CG_EXPECT_EQ(device.BindGroupInfoOf(group.Value()).resourceCount, u32{3});

    // An array slot needs one resource per element, not one for the binding.
    desc.resources = Span<const BindingResource>(resources, 1);
    const auto short1 = device.CreateBindGroup(desc);
    CG_EXPECT_FALSE(short1);
    CG_EXPECT_EQ(short1.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_binding_pipeline_layout)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    StandardResources resources{};
    CG_REQUIRE(BuildStandard(device, resources));
    const Limits limits = device.Capabilities().limits;

    // One used slot; the rest stay invalid.
    PipelineLayoutDesc desc{};
    desc.groups[static_cast<u32>(BindGroupSlot::PerMaterial)] = resources.layout;

    PushConstantRange range{};
    range.offsetBytes = 0;
    range.sizeBytes = 16;
    range.stages = ShaderStageBit(ShaderStage::Vertex);
    desc.pushConstants = Span<const PushConstantRange>(&range, 1);

    auto created = device.CreatePipelineLayout(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const PipelineLayoutHandle layout = created.Value();
    CG_EXPECT_TRUE(device.DestroyPipelineLayout(layout));
    CG_EXPECT_FALSE(device.DestroyPipelineLayout(layout));

    // No used slot is a pipeline that binds nothing.
    PipelineLayoutDesc empty{};
    auto failed = device.CreatePipelineLayout(empty);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);

    // A duplicate group layout in two slots is ambiguous.
    PipelineLayoutDesc duplicate{};
    duplicate.groups[0] = resources.layout;
    duplicate.groups[1] = resources.layout;
    failed = device.CreatePipelineLayout(duplicate);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);

    // Push-constant rules.
    PushConstantRange badRange = range;
    badRange.sizeBytes = 2;
    PipelineLayoutDesc unaligned{};
    unaligned.groups[0] = resources.layout;
    unaligned.pushConstants = Span<const PushConstantRange>(&badRange, 1);
    failed = device.CreatePipelineLayout(unaligned);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);

    badRange = range;
    badRange.sizeBytes = 0;
    failed = device.CreatePipelineLayout(unaligned);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);

    badRange = range;
    badRange.stages = 0;
    failed = device.CreatePipelineLayout(unaligned);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);

    // A range that leaves the push-constant block is out of range, not
    // malformed.
    PushConstantRange tooLarge = range;
    tooLarge.offsetBytes = limits.maxPushConstantBytes - 8;
    tooLarge.sizeBytes = 16;
    PipelineLayoutDesc oversized{};
    oversized.groups[0] = resources.layout;
    oversized.pushConstants = Span<const PushConstantRange>(&tooLarge, 1);
    failed = device.CreatePipelineLayout(oversized);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::OutOfRange);

    // Two ranges for the same stage may not overlap.
    PushConstantRange overlapping[2] = {};
    overlapping[0] = range;
    overlapping[1] = range;
    overlapping[1].offsetBytes = 8;
    PipelineLayoutDesc overlapDesc{};
    overlapDesc.groups[0] = resources.layout;
    overlapDesc.pushConstants = Span<const PushConstantRange>(overlapping, 2);
    failed = device.CreatePipelineLayout(overlapDesc);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);

    // Disjoint ranges, or the same bytes for different stages, are legal.
    overlapping[1].offsetBytes = 16;
    CG_EXPECT_TRUE(static_cast<bool>(device.CreatePipelineLayout(overlapDesc)));
    overlapping[1].offsetBytes = 8;
    overlapping[1].stages = ShaderStageBit(ShaderStage::Pixel);
    CG_EXPECT_TRUE(static_cast<bool>(device.CreatePipelineLayout(overlapDesc)));

    // More than the fixed number of ranges is rejected.
    PushConstantRange many[5] = {};
    for (u32 index = 0; index < 5; ++index)
    {
        many[index].offsetBytes = index * 4;
        many[index].sizeBytes = 4;
        many[index].stages = ShaderStageBit(ShaderStage::Pixel);
    }
    PipelineLayoutDesc manyDesc{};
    manyDesc.groups[0] = resources.layout;
    manyDesc.pushConstants = Span<const PushConstantRange>(many, 5);
    failed = device.CreatePipelineLayout(manyDesc);
    CG_EXPECT_FALSE(failed);
    CG_EXPECT_EQ(failed.Error().code, ErrorCode::InvalidArgument);
}
