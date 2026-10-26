#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;

using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

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
    // The software adapter: every supported Windows version has one, so this is
    // the path CI runs.
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

CG_TEST(d3d12_resources_host_visible_buffer_round_trips)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    BufferDesc desc{};
    desc.usage = BufferUsage::Uniform | BufferUsage::HostVisible | BufferUsage::CopySource;
    desc.sizeBytes = 256;
    desc.label = "constants";
    auto created = device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const BufferHandle buffer = created.Value();

    const BufferInfo info = device.BufferInfoOf(buffer);
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_EQ(info.sizeBytes, u64{256});
    CG_EXPECT_TRUE(HasUsage(info.usage, BufferUsage::HostVisible));

    Byte bytes[16]{};
    for (u32 index = 0; index < 16; ++index)
    {
        bytes[index] = static_cast<Byte>(static_cast<unsigned char>(index * 7));
    }
    CG_EXPECT_TRUE(device.WriteBuffer(buffer, 64, Span<const Byte>(bytes, 16)).IsOk());
    // A write past the end is a range error, not a crash.
    const auto overrun = device.WriteBuffer(buffer, 250, Span<const Byte>(bytes, 16));
    CG_EXPECT_FALSE(overrun);
    if (!overrun)
    {
        CG_EXPECT_EQ(overrun.Error().code, cyber::core::ErrorCode::OutOfRange);
    }
    // A readback of a write-through buffer is refused: read it back through a
    // Readback buffer instead, which is what the API requires.
    CG_EXPECT_FALSE(device.ReadBuffer(buffer, 0, Span<Byte>(bytes, 4)));

    CG_EXPECT_TRUE(device.DestroyBuffer(buffer).IsOk());
    CG_EXPECT_FALSE(device.BufferInfoOf(buffer).valid);
    CG_EXPECT_FALSE(device.DestroyBuffer(buffer));
}

CG_TEST(d3d12_resources_readback_buffer_is_cpu_readable)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    BufferDesc desc{};
    desc.usage = BufferUsage::Readback | BufferUsage::CopyDestination;
    desc.sizeBytes = 128;
    auto created = device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const BufferHandle buffer = created.Value();

    // Writing a readback buffer through the host is refused, because the API
    // fixes its usage to the GPU side.
    Byte bytes[4]{};
    const auto written = device.WriteBuffer(buffer, 0, Span<const Byte>(bytes, 4));
    CG_EXPECT_FALSE(written);
    if (!written)
    {
        CG_EXPECT_EQ(written.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // Reading it returns zeroed bytes, which is what a fresh readback heap holds.
    Byte read[8]{};
    read[0] = static_cast<Byte>(0x7F);
    CG_EXPECT_TRUE(device.ReadBuffer(buffer, 0, Span<Byte>(read, 8)).IsOk());
    CG_EXPECT_TRUE(std::to_integer<unsigned>(read[0]) == 0U);
}

CG_TEST(d3d12_resources_texture_and_view_lifetime)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    TextureDesc desc{};
    desc.dimension = TextureDimension::Dim2D;
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
    desc.width = 64;
    desc.height = 32;
    desc.label = "albedo";
    auto created = device.CreateTexture(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const TextureHandle texture = created.Value();

    const TextureInfo info = device.TextureInfoOf(texture);
    CG_EXPECT_TRUE(info.valid);
    CG_EXPECT_EQ(info.width, u32{64});
    CG_EXPECT_EQ(info.height, u32{32});
    CG_EXPECT_EQ(info.mipLevels, u32{1});
    CG_EXPECT_TRUE(info.sizeBytes > 0);

    TextureViewDesc viewDesc{};
    viewDesc.texture = texture;
    viewDesc.format = Format::RGBA8Unorm;
    viewDesc.dimension = TextureDimension::Dim2D;
    viewDesc.label = "albedo view";
    auto viewCreated = device.CreateTextureView(viewDesc);
    CG_REQUIRE(static_cast<bool>(viewCreated));
    const TextureViewHandle view = viewCreated.Value();
    CG_EXPECT_TRUE(device.TextureViewInfoOf(view).valid);
    CG_EXPECT_EQ(device.TextureViewInfoOf(view).format, Format::RGBA8Unorm);

    // Destroying the texture retires its views, so a view handle never describes
    // released storage.
    CG_EXPECT_TRUE(device.DestroyTexture(texture).IsOk());
    CG_EXPECT_FALSE(device.TextureViewInfoOf(view).valid);
    CG_EXPECT_FALSE(device.DestroyTextureView(view));
    CG_EXPECT_FALSE(device.TextureInfoOf(texture).valid);
}

CG_TEST(d3d12_resources_rejects_a_format_without_a_native_equivalent)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    // A three-component float is a vertex attribute shorthand, not a texel
    // format, so a texture cannot be created with it.
    TextureDesc desc{};
    desc.format = Format::R32G32B32Float;
    desc.usage = TextureUsage::Sampled;
    desc.width = 8;
    desc.height = 8;
    const auto created = device.CreateTexture(desc);
    CG_EXPECT_FALSE(created);
    if (!created)
    {
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::Unsupported);
    }
}

CG_TEST(d3d12_resources_sampler_creation_and_release)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    SamplerDesc desc{};
    desc.minFilter = FilterMode::Linear;
    desc.magFilter = FilterMode::Linear;
    desc.mipmapFilter = FilterMode::Linear;
    desc.maxAnisotropy = 4;
    desc.addressU = AddressMode::ClampToEdge;
    desc.label = "trilinear";
    auto created = device.CreateSampler(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const SamplerHandle sampler = created.Value();
    CG_EXPECT_TRUE(device.DestroySampler(sampler).IsOk());
    CG_EXPECT_FALSE(device.DestroySampler(sampler));
}

CG_TEST(d3d12_resources_tracks_buffer_state_across_transitions)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    BufferDesc desc{};
    desc.usage = BufferUsage::Vertex | BufferUsage::CopyDestination;
    desc.sizeBytes = 1024;
    auto created = device.CreateBuffer(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const BufferHandle buffer = created.Value();
    CG_EXPECT_TRUE(device.BufferStateOf(buffer) == ResourceState::Undefined);

    BufferBarrier barrier{};
    barrier.buffer = buffer;
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::VertexBuffer;
    CG_EXPECT_TRUE(
        device.Transition(Span<const BufferBarrier>(&barrier, 1), Span<const TextureBarrier>())
            .IsOk());
    CG_EXPECT_TRUE(device.BufferStateOf(buffer) == ResourceState::VertexBuffer);

    // A barrier whose `from` disagrees with the tracked state is refused, and the
    // state it would have written is unchanged.
    barrier.from = ResourceState::CopyDestination;
    barrier.to = ResourceState::CopySource;
    const auto mismatched =
        device.Transition(Span<const BufferBarrier>(&barrier, 1), Span<const TextureBarrier>());
    CG_EXPECT_FALSE(mismatched);
    if (!mismatched)
    {
        CG_EXPECT_EQ(mismatched.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
    CG_EXPECT_TRUE(device.BufferStateOf(buffer) == ResourceState::VertexBuffer);

    // A barrier naming a dead handle fails and names it as stale.
    CG_EXPECT_TRUE(device.DestroyBuffer(buffer).IsOk());
    barrier.from = ResourceState::Undefined;
    const auto stale =
        device.Transition(Span<const BufferBarrier>(&barrier, 1), Span<const TextureBarrier>());
    CG_EXPECT_FALSE(stale);
    if (!stale)
    {
        CG_EXPECT_EQ(stale.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
}

CG_TEST(d3d12_resources_tracks_texture_subresource_state)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    TextureDesc desc{};
    desc.format = Format::RGBA8Unorm;
    desc.usage = TextureUsage::Sampled | TextureUsage::RenderTarget;
    desc.width = 32;
    desc.height = 32;
    desc.mipLevels = 2;
    auto created = device.CreateTexture(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const TextureHandle texture = created.Value();

    TextureBarrier barrier{};
    barrier.texture = texture;
    barrier.from = ResourceState::Undefined;
    barrier.to = ResourceState::ShaderRead;
    CG_EXPECT_TRUE(
        device.Transition(Span<const BufferBarrier>(), Span<const TextureBarrier>(&barrier, 1))
            .IsOk());
    CG_EXPECT_TRUE(device.TextureStateOf(texture, 0, 0) == ResourceState::ShaderRead);
    // A mip the batch covered is in the same state; the state is per subresource,
    // not per texture.
    CG_EXPECT_TRUE(device.TextureStateOf(texture, 1, 0) == ResourceState::ShaderRead);

    // One mip only.
    barrier.range.baseMipLevel = 1;
    barrier.range.mipLevelCount = 1;
    barrier.from = ResourceState::ShaderRead;
    barrier.to = ResourceState::RenderTarget;
    CG_EXPECT_TRUE(
        device.Transition(Span<const BufferBarrier>(), Span<const TextureBarrier>(&barrier, 1))
            .IsOk());
    CG_EXPECT_TRUE(device.TextureStateOf(texture, 0, 0) == ResourceState::ShaderRead);
    CG_EXPECT_TRUE(device.TextureStateOf(texture, 1, 0) == ResourceState::RenderTarget);
}

CG_TEST(d3d12_resources_release_everything_on_teardown)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenSoftwareDevice(device, heap));

        BufferDesc bufferDesc{};
        bufferDesc.usage = BufferUsage::HostVisible | BufferUsage::Vertex;
        bufferDesc.sizeBytes = 512;
        auto buffer = device.CreateBuffer(bufferDesc);
        CG_REQUIRE(static_cast<bool>(buffer));

        TextureDesc textureDesc{};
        textureDesc.format = Format::RGBA8Unorm;
        textureDesc.usage = TextureUsage::Sampled;
        textureDesc.width = 16;
        textureDesc.height = 16;
        auto texture = device.CreateTexture(textureDesc);
        CG_REQUIRE(static_cast<bool>(texture));

        TextureViewDesc viewDesc{};
        viewDesc.texture = texture.Value();
        viewDesc.format = Format::RGBA8Unorm;
        auto view = device.CreateTextureView(viewDesc);
        CG_REQUIRE(static_cast<bool>(view));

        auto sampler = device.CreateSampler(SamplerDesc{});
        CG_REQUIRE(static_cast<bool>(sampler));

        // Nothing is destroyed by hand: the device must release all four, plus its
        // slot tables, without a leak the allocator can see.
        CG_EXPECT_TRUE(heap.Stats().used > 0);
    }
    CG_EXPECT_EQ(heap.Stats().used, u64{0});
}
