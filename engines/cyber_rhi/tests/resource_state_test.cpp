#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
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

BufferHandle MakeBuffer(Device& device) noexcept
{
    BufferDesc desc{};
    desc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    desc.sizeBytes = 1024;
    const auto created = device.CreateBuffer(desc);
    return created ? created.Value() : BufferHandle{};
}

}  // namespace

CG_TEST(rhi_resource_state_classification)
{
    CG_EXPECT_FALSE(IsValidStateSet(ResourceState::Undefined));
    CG_EXPECT_FALSE(IsReadState(ResourceState::Undefined));
    CG_EXPECT_FALSE(IsWriteState(ResourceState::Undefined));

    CG_EXPECT_TRUE(IsReadState(ResourceState::ShaderRead));
    CG_EXPECT_TRUE(IsReadState(ResourceState::CopySource));
    CG_EXPECT_FALSE(IsWriteState(ResourceState::ShaderRead));
    CG_EXPECT_TRUE(IsWriteState(ResourceState::RenderTarget));
    CG_EXPECT_TRUE(IsWriteState(ResourceState::ShaderWrite));
    CG_EXPECT_TRUE(IsWriteState(ResourceState::Present));

    // Reads combine; a write is alone.
    CG_EXPECT_TRUE(IsReadState(ResourceState::ShaderRead | ResourceState::CopySource));
    CG_EXPECT_TRUE(IsValidStateSet(ResourceState::ShaderRead | ResourceState::CopySource));
    CG_EXPECT_TRUE(IsValidStateSet(ResourceState::ShaderWrite));
    CG_EXPECT_FALSE(IsValidStateSet(ResourceState::ShaderWrite | ResourceState::ShaderRead));
    CG_EXPECT_FALSE(IsValidStateSet(ResourceState::ShaderRead | ResourceState::RenderTarget));
    CG_EXPECT_FALSE(IsValidStateSet(ResourceState::Present | ResourceState::ShaderRead));
    CG_EXPECT_FALSE(IsValidStateSet(ResourceState::CopyDestination | ResourceState::CopySource));
    CG_EXPECT_FALSE(IsValidStateSet(ResourceState::DepthRead | ResourceState::DepthWrite));

    CG_EXPECT_TRUE(
        HasState(ResourceState::ShaderRead | ResourceState::CopySource, ResourceState::CopySource));
    CG_EXPECT_FALSE(HasState(ResourceState::ShaderRead, ResourceState::CopySource));
    CG_EXPECT_FALSE(HasState(ResourceState::ShaderRead, ResourceState::Undefined));
    CG_EXPECT_EQ(ResourceStateBits(ResourceState::ShaderRead | ResourceState::CopySource),
                 ResourceStateBits(ResourceState::ShaderRead) |
                     ResourceStateBits(ResourceState::CopySource));

    CG_EXPECT_STREQ(ResourceStateName(ResourceState::Undefined), "undefined");
    CG_EXPECT_STREQ(ResourceStateName(ResourceState::CopyDestination), "copy_destination");
    CG_EXPECT_STREQ(ResourceStateName(ResourceState::IndirectArgument), "indirect_argument");
    CG_EXPECT_STREQ(ResourceStateName(ResourceState::ShaderRead | ResourceState::CopySource),
                    "multiple");
}

CG_TEST(rhi_resource_state_kind_membership)
{
    CG_EXPECT_TRUE(IsBufferResourceState(ResourceState::VertexBuffer));
    CG_EXPECT_TRUE(IsBufferResourceState(ResourceState::ShaderWrite));
    CG_EXPECT_TRUE(IsBufferResourceState(ResourceState::ShaderRead | ResourceState::CopySource));
    CG_EXPECT_FALSE(IsBufferResourceState(ResourceState::RenderTarget));
    CG_EXPECT_FALSE(IsBufferResourceState(ResourceState::DepthRead));
    CG_EXPECT_FALSE(IsBufferResourceState(ResourceState::Present));
    CG_EXPECT_FALSE(IsBufferResourceState(ResourceState::Undefined));

    CG_EXPECT_TRUE(IsTextureResourceState(ResourceState::RenderTarget));
    CG_EXPECT_TRUE(IsTextureResourceState(ResourceState::DepthWrite));
    CG_EXPECT_TRUE(IsTextureResourceState(ResourceState::Present));
    CG_EXPECT_FALSE(IsTextureResourceState(ResourceState::VertexBuffer));
    CG_EXPECT_FALSE(IsTextureResourceState(ResourceState::IndirectArgument));
    CG_EXPECT_FALSE(IsTextureResourceState(ResourceState::Undefined));
}

CG_TEST(rhi_resource_state_buffer_barriers)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));
    const BufferHandle buffer = MakeBuffer(device);
    CG_REQUIRE(buffer.IsValid());
    CG_EXPECT_EQ(device.BufferStateOf(buffer), ResourceState::Undefined);

    BufferBarrier barrier{};
    barrier.buffer = buffer;
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::VertexBuffer;
    CG_EXPECT_TRUE(device.Transition(Span<const BufferBarrier>(&barrier, 1), {}));
    CG_EXPECT_EQ(device.BufferStateOf(buffer), ResourceState::VertexBuffer);

    // A combined read state is one barrier.
    barrier.from = ResourceState::VertexBuffer;
    barrier.to = ResourceState::ShaderRead | ResourceState::CopySource;
    CG_EXPECT_TRUE(device.Transition(Span<const BufferBarrier>(&barrier, 1), {}));
    CG_EXPECT_EQ(device.BufferStateOf(buffer),
                 ResourceState::ShaderRead | ResourceState::CopySource);

    // A mismatched source is the missing-barrier bug: rejected, state unchanged.
    barrier.from = ResourceState::VertexBuffer;
    barrier.to = ResourceState::IndexBuffer;
    const auto mismatch = device.Transition(Span<const BufferBarrier>(&barrier, 1), {});
    CG_EXPECT_FALSE(mismatch);
    CG_EXPECT_EQ(mismatch.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_EQ(device.BufferStateOf(buffer),
                 ResourceState::ShaderRead | ResourceState::CopySource);

    // Undefined as a target is never legal; as a source it discards contents.
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::Undefined;
    CG_EXPECT_FALSE(device.Transition(Span<const BufferBarrier>(&barrier, 1), {}));
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::IndexBuffer;
    CG_EXPECT_TRUE(device.Transition(Span<const BufferBarrier>(&barrier, 1), {}));
    CG_EXPECT_EQ(device.BufferStateOf(buffer), ResourceState::IndexBuffer);

    // A state a buffer cannot be in is rejected on either side.
    barrier.from = ResourceState::IndexBuffer;
    barrier.to = ResourceState::RenderTarget;
    const auto wrongTarget = device.Transition(Span<const BufferBarrier>(&barrier, 1), {});
    CG_EXPECT_FALSE(wrongTarget);
    CG_EXPECT_EQ(wrongTarget.Error().code, ErrorCode::InvalidArgument);
    barrier.from = ResourceState::RenderTarget;
    barrier.to = ResourceState::IndexBuffer;
    CG_EXPECT_FALSE(device.Transition(Span<const BufferBarrier>(&barrier, 1), {}));

    // A stale handle is rejected and nothing changes.
    CG_EXPECT_TRUE(device.DestroyBuffer(buffer));
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::ShaderRead;
    const auto stale = device.Transition(Span<const BufferBarrier>(&barrier, 1), {});
    CG_EXPECT_FALSE(stale);
    CG_EXPECT_EQ(stale.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(rhi_resource_state_texture_barriers)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    TextureDesc desc{};
    desc.dimension = TextureDimension::Dim2D;
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::Sampled | TextureUsage::RenderTarget;
    desc.width = 8;
    desc.height = 8;
    desc.depth = 1;
    desc.arrayLayers = 2;
    desc.mipLevels = 3;
    desc.sampleCount = 1;
    auto created = device.CreateTexture(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const TextureHandle texture = created.Value();

    // A whole-texture transition reaches every subresource.
    TextureBarrier barrier{};
    barrier.texture = texture;
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::ShaderRead;
    CG_REQUIRE(device.Transition({}, Span<const TextureBarrier>(&barrier, 1)));
    for (u32 mip = 0; mip < 3; ++mip)
    {
        for (u32 layer = 0; layer < 2; ++layer)
        {
            CG_EXPECT_EQ(device.TextureStateOf(texture, mip, layer), ResourceState::ShaderRead);
        }
    }

    // A range touches only the subresources it names. A zero count means "the
    // rest of the chain".
    barrier.range.baseMipLevel = 1;
    barrier.range.mipLevelCount = 0;
    barrier.range.baseArrayLayer = 0;
    barrier.range.layerCount = 2;
    barrier.from = ResourceState::ShaderRead;
    barrier.to = ResourceState::RenderTarget;
    CG_REQUIRE(device.Transition({}, Span<const TextureBarrier>(&barrier, 1)));
    CG_EXPECT_EQ(device.TextureStateOf(texture, 0, 0), ResourceState::ShaderRead);
    CG_EXPECT_EQ(device.TextureStateOf(texture, 0, 1), ResourceState::ShaderRead);
    CG_EXPECT_EQ(device.TextureStateOf(texture, 1, 0), ResourceState::RenderTarget);
    CG_EXPECT_EQ(device.TextureStateOf(texture, 2, 1), ResourceState::RenderTarget);

    // A barrier whose source does not match every subresource in its range is
    // rejected, and no subresource in that range changes: the batch is atomic.
    barrier.range = SubresourceRange{};
    barrier.from = ResourceState::ShaderRead;
    barrier.to = ResourceState::DepthRead;
    const auto mismatch = device.Transition({}, Span<const TextureBarrier>(&barrier, 1));
    CG_EXPECT_FALSE(mismatch);
    CG_EXPECT_EQ(mismatch.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_EQ(device.TextureStateOf(texture, 0, 0), ResourceState::ShaderRead);
    CG_EXPECT_EQ(device.TextureStateOf(texture, 1, 0), ResourceState::RenderTarget);

    // Undefined discards, so it is accepted even over mixed tracked state.
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::ShaderWrite;
    CG_EXPECT_TRUE(device.Transition({}, Span<const TextureBarrier>(&barrier, 1)));
    CG_EXPECT_EQ(device.TextureStateOf(texture, 2, 0), ResourceState::ShaderWrite);

    // A state a texture cannot be in, an aspect the format lacks, and a range
    // that leaves the texture are all rejected.
    barrier.from = ResourceState::ShaderWrite;
    barrier.to = ResourceState::VertexBuffer;
    const auto wrongTarget = device.Transition({}, Span<const TextureBarrier>(&barrier, 1));
    CG_EXPECT_FALSE(wrongTarget);
    CG_EXPECT_EQ(wrongTarget.Error().code, ErrorCode::InvalidArgument);

    barrier.from = ResourceState::ShaderWrite;
    barrier.to = ResourceState::ShaderRead;
    barrier.range.aspect = TextureAspect::Depth;
    CG_EXPECT_FALSE(device.Transition({}, Span<const TextureBarrier>(&barrier, 1)));

    barrier.range = SubresourceRange{};
    barrier.range.baseMipLevel = 3;
    CG_EXPECT_FALSE(device.Transition({}, Span<const TextureBarrier>(&barrier, 1)));
    barrier.range.baseArrayLayer = 2;
    CG_EXPECT_FALSE(device.Transition({}, Span<const TextureBarrier>(&barrier, 1)));

    // An empty batch is legal and does nothing.
    CG_EXPECT_TRUE(device.Transition({}, {}));
}

CG_TEST(rhi_resource_state_batch_is_atomic)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    const BufferHandle first = MakeBuffer(device);
    const BufferHandle second = MakeBuffer(device);
    CG_REQUIRE(first.IsValid());
    CG_REQUIRE(second.IsValid());

    BufferBarrier barriers[2] = {};
    barriers[0].buffer = first;
    barriers[0].from = ResourceState::Undefined;
    barriers[0].to = ResourceState::VertexBuffer;
    barriers[1].buffer = second;
    barriers[1].from = ResourceState::ShaderRead;
    barriers[1].to = ResourceState::IndexBuffer;

    // The second barrier is wrong; the first must not apply.
    const auto rejected = device.Transition(Span<const BufferBarrier>(barriers, 2), {});
    CG_EXPECT_FALSE(rejected);
    CG_EXPECT_EQ(rejected.Error().code, ErrorCode::InvalidArgument);
    CG_EXPECT_EQ(device.BufferStateOf(first), ResourceState::Undefined);
    CG_EXPECT_EQ(device.BufferStateOf(second), ResourceState::Undefined);

    // With the second barrier corrected, both apply.
    barriers[1].from = ResourceState::Undefined;
    CG_EXPECT_TRUE(device.Transition(Span<const BufferBarrier>(barriers, 2), {}));
    CG_EXPECT_EQ(device.BufferStateOf(first), ResourceState::VertexBuffer);
    CG_EXPECT_EQ(device.BufferStateOf(second), ResourceState::IndexBuffer);
}

CG_TEST(rhi_resource_state_range_resolution)
{
    TextureInfo info;
    info.valid = true;
    info.dimension = TextureDimension::Dim2D;
    info.format = Format::RGBA8Unorm;
    info.width = 16;
    info.height = 16;
    info.depth = 1;
    info.arrayLayers = 3;
    info.mipLevels = 5;
    info.sampleCount = 1;

    // Zero counts mean "everything remaining".
    ResolvedRange full = ResolveSubresourceRange(info, SubresourceRange{});
    CG_EXPECT_TRUE(full.valid);
    CG_EXPECT_EQ(full.baseMipLevel, u32{0});
    CG_EXPECT_EQ(full.mipLevelCount, u32{5});
    CG_EXPECT_EQ(full.baseArrayLayer, u32{0});
    CG_EXPECT_EQ(full.layerCount, u32{3});

    SubresourceRange partial{};
    partial.baseMipLevel = 2;
    partial.mipLevelCount = 3;
    partial.baseArrayLayer = 1;
    partial.layerCount = 2;
    const ResolvedRange resolved = ResolveSubresourceRange(info, partial);
    CG_EXPECT_TRUE(resolved.valid);
    CG_EXPECT_EQ(resolved.mipLevelCount, u32{3});
    CG_EXPECT_EQ(resolved.layerCount, u32{2});

    // More than remains, a base at or past the end, and an aspect the format
    // does not have are all invalid.
    partial.mipLevelCount = 4;
    CG_EXPECT_FALSE(ResolveSubresourceRange(info, partial).valid);
    partial = SubresourceRange{};
    partial.baseMipLevel = 5;
    CG_EXPECT_FALSE(ResolveSubresourceRange(info, partial).valid);
    partial = SubresourceRange{};
    partial.baseArrayLayer = 3;
    CG_EXPECT_FALSE(ResolveSubresourceRange(info, partial).valid);
    partial = SubresourceRange{};
    partial.aspect = TextureAspect::Depth;
    CG_EXPECT_FALSE(ResolveSubresourceRange(info, partial).valid);

    // An invalid texture info resolves to an invalid range instead of reading
    // through a zeroed shape.
    info.valid = false;
    CG_EXPECT_FALSE(ResolveSubresourceRange(info, SubresourceRange{}).valid);
}
