#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/bindless_table.h>
#include <cyber/renderer/descriptor_allocator.h>
#include <cyber/renderer/frame_stream.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/frame_ring.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

#include <array>
#include <utility>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::renderer::BindlessResourceTable;
using cyber::renderer::BindlessResourceTableDesc;
using cyber::renderer::FrameBufferStream;
using cyber::renderer::FrameBufferStreamDesc;
using cyber::renderer::PersistentDescriptorAllocator;
using cyber::renderer::ResourceBindingPath;
using cyber::renderer::TransientDescriptorAllocator;
using cyber::rhi::CommandList;
using cyber::rhi::CommandListDesc;
using cyber::rhi::Device;
using cyber::rhi::DeviceDesc;
using cyber::rhi::FenceDesc;
using cyber::rhi::FrameRing;
using cyber::rhi::FrameRingDesc;
using cyber::rhi::QueueKind;
using cyber::rhi::ResourceState;

namespace
{

struct Fixture
{
    HeapAllocator heap;
    Device device;
    FrameRing ring;

    [[nodiscard]] bool Open(u32 framesInFlight = 2) noexcept
    {
        DeviceDesc deviceDesc{};
        deviceDesc.backend = cyber::rhi::NullBackend();
        auto createdDevice = Device::Create(deviceDesc, heap);
        if (!createdDevice)
        {
            return false;
        }
        device = std::move(createdDevice.Value());

        auto fence = device.CreateFence(FenceDesc{});
        if (!fence)
        {
            return false;
        }
        FrameRingDesc ringDesc{};
        ringDesc.fence = fence.Value();
        ringDesc.framesInFlight = framesInFlight;
        auto createdRing = FrameRing::Create(ringDesc);
        if (!createdRing)
        {
            return false;
        }
        ring = std::move(createdRing.Value());
        return true;
    }

    [[nodiscard]] bool Record(FrameBufferStream& stream) noexcept
    {
        CommandListDesc desc{};
        desc.queue = QueueKind::Graphics;
        auto created = device.CreateCommandList(desc);
        if (!created)
        {
            return false;
        }
        CommandList list = std::move(created.Value());
        return list.Begin().IsOk() && stream.RecordUploads(list).IsOk() && list.End().IsOk();
    }

    [[nodiscard]] bool OpenD3D12() noexcept
    {
        const cyber::rhi::Backend* backend = cyber::rhi::D3D12Backend();
        if (backend == nullptr)
        {
            return false;
        }
        const u32 adapterCount = backend->enumerateAdapters(nullptr, 0);
        if (adapterCount == 0)
        {
            return false;
        }
        DeviceDesc desc{};
        desc.backend = backend;
        desc.adapterIndex = adapterCount - 1;
        auto createdDevice = Device::Create(desc, heap);
        if (!createdDevice)
        {
            return false;
        }
        device = std::move(createdDevice.Value());
        return true;
    }
};

}  // namespace

CG_TEST(renderer_phase19_frame_stream_upload_and_reuse)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());

    FrameBufferStreamDesc desc{};
    desc.framesInFlight = 2;
    desc.capacityBytes = 256;
    desc.maxAllocationsPerFrame = 4;
    desc.bindingType = cyber::rhi::BindingType::ReadOnlyStorageBuffer;
    desc.elementStride = 16;
    auto created = FrameBufferStream::Create(fixture.device, fixture.heap, desc);
    CG_REQUIRE(static_cast<bool>(created));
    FrameBufferStream stream = std::move(created.Value());

    auto frame0 = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame0));
    CG_EXPECT_EQ(frame0.Value().frameIndex, u64{0});
    CG_REQUIRE(stream.BeginFrame(fixture.ring, frame0.Value()).IsOk());
    const std::array<Byte, 20> invalidData{};
    auto invalidAllocation =
        stream.Allocate(Span<const Byte>(invalidData.data(), invalidData.size()));
    CG_EXPECT_FALSE(invalidAllocation);
    if (!invalidAllocation)
    {
        CG_EXPECT_EQ(invalidAllocation.Error().code, ErrorCode::InvalidArgument);
    }

    const std::array<Byte, 32> data{};
    auto first = stream.Allocate(Span<const Byte>(data.data(), data.size()));
    CG_REQUIRE(static_cast<bool>(first));
    auto second = stream.Allocate(Span<const Byte>(data.data(), data.size()));
    CG_REQUIRE(static_cast<bool>(second));
    CG_EXPECT_EQ(first.Value().offsetBytes, u64{0});
    CG_EXPECT_EQ(second.Value().offsetBytes, u64{32});
    CG_EXPECT_EQ(first.Value().buffer, stream.DestinationBuffer(0));
    CG_EXPECT_EQ(first.Value().frameIndex, frame0.Value().frameIndex);

    const cyber::rhi::FrameSlot invalidFrame{};
    CG_EXPECT_FALSE(stream.BeginFrame(fixture.ring, invalidFrame));
    CG_REQUIRE(fixture.Record(stream));
    CG_EXPECT_EQ(fixture.device.BufferStateOf(stream.DestinationBuffer(0)),
                 ResourceState::ShaderRead);

    auto frame1 = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame1));
    CG_REQUIRE(stream.BeginFrame(fixture.ring, frame1.Value()).IsOk());
    std::array<Byte, 272> tooLarge{};
    auto overflow = stream.Allocate(Span<const Byte>(tooLarge.data(), tooLarge.size()));
    CG_EXPECT_FALSE(overflow);
    if (!overflow)
    {
        CG_EXPECT_EQ(overflow.Error().code, ErrorCode::OutOfRange);
    }
    CG_REQUIRE(fixture.Record(stream));

    CG_REQUIRE(fixture.device.SignalFence(fixture.ring.Fence(), frame0.Value().signalValue).IsOk());
    auto reusedFrame = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(reusedFrame));
    CG_REQUIRE(stream.BeginFrame(fixture.ring, reusedFrame.Value()).IsOk());
    auto reused = stream.Allocate(Span<const Byte>(data.data(), data.size()));
    CG_REQUIRE(static_cast<bool>(reused));
    CG_EXPECT_EQ(reused.Value().offsetBytes, u64{0});
    CG_REQUIRE(fixture.Record(stream));
    CG_EXPECT_TRUE(stream.Destroy().IsOk());
}

CG_TEST(renderer_phase19_descriptor_allocators)
{
    HeapAllocator heap;
    PersistentDescriptorAllocator persistent;
    CG_REQUIRE(persistent.Initialize(2, heap).IsOk());

    auto first = persistent.Allocate();
    auto second = persistent.Allocate();
    CG_REQUIRE(static_cast<bool>(first));
    CG_REQUIRE(static_cast<bool>(second));
    CG_EXPECT_TRUE(first.Value().valid);
    CG_EXPECT_TRUE(second.Value().valid);
    CG_EXPECT_FALSE(persistent.Allocate());
    CG_EXPECT_TRUE(persistent.Free(first.Value()).IsOk());
    CG_EXPECT_FALSE(persistent.IsLive(first.Value()));
    CG_EXPECT_FALSE(persistent.Free(first.Value()));
    auto reused = persistent.Allocate();
    CG_REQUIRE(static_cast<bool>(reused));
    CG_EXPECT_EQ(reused.Value().index, first.Value().index);
    CG_EXPECT_TRUE(reused.Value().generation != first.Value().generation);

    Fixture fixture;
    CG_REQUIRE(fixture.Open());
    TransientDescriptorAllocator transient;
    CG_REQUIRE(transient.Initialize(2, 4, heap).IsOk());
    CG_EXPECT_FALSE(transient.Allocate(1));
    auto frame0 = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame0));
    CG_REQUIRE(transient.BeginFrame(fixture.ring, frame0.Value()).IsOk());
    auto range = transient.Allocate(3);
    CG_REQUIRE(static_cast<bool>(range));
    CG_EXPECT_EQ(range.Value().first, u32{0});
    CG_EXPECT_EQ(range.Value().frameSlot, u32{0});
    CG_EXPECT_FALSE(transient.Allocate(2));

    auto frame1 = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame1));
    CG_REQUIRE(transient.BeginFrame(fixture.ring, frame1.Value()).IsOk());
    auto nextSlotRange = transient.Allocate(4);
    CG_REQUIRE(static_cast<bool>(nextSlotRange));
    CG_EXPECT_EQ(nextSlotRange.Value().frameSlot, u32{1});
    CG_EXPECT_EQ(nextSlotRange.Value().first, u32{0});

    transient.Destroy();
    persistent.Destroy();
}

CG_TEST(renderer_phase19_stream_combines_storage_alignments)
{
    Fixture fixture;
    CG_REQUIRE(fixture.Open());

    FrameBufferStreamDesc desc{};
    desc.framesInFlight = 2;
    desc.capacityBytes = 96;
    desc.maxAllocationsPerFrame = 2;
    desc.bindingType = cyber::rhi::BindingType::ReadOnlyStorageBuffer;
    desc.elementStride = 12;
    auto created = FrameBufferStream::Create(fixture.device, fixture.heap, desc);
    CG_REQUIRE(static_cast<bool>(created));
    FrameBufferStream stream = std::move(created.Value());

    auto frame = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame));
    CG_REQUIRE(stream.BeginFrame(fixture.ring, frame.Value()).IsOk());
    const std::array<Byte, 12> element{};
    auto first = stream.Allocate(Span<const Byte>(element.data(), element.size()));
    auto second = stream.Allocate(Span<const Byte>(element.data(), element.size()));
    CG_REQUIRE(static_cast<bool>(first));
    CG_REQUIRE(static_cast<bool>(second));
    CG_EXPECT_EQ(first.Value().offsetBytes, u64{0});
    CG_EXPECT_EQ(second.Value().offsetBytes, u64{48});
    CG_EXPECT_TRUE(stream.Destroy().IsOk());
}

CG_TEST(renderer_phase19_bindless_capability_fallback)
{
    HeapAllocator heap;
    DeviceDesc deviceDesc{};
    deviceDesc.backend = cyber::rhi::NullBackend();
    auto createdDevice = Device::Create(deviceDesc, heap);
    CG_REQUIRE(static_cast<bool>(createdDevice));
    Device device = std::move(createdDevice.Value());

    CG_EXPECT_EQ(cyber::renderer::SelectResourceBindingPath(device),
                 ResourceBindingPath::Conventional);
    BindlessResourceTableDesc desc{};
    desc.capacity = 4;
    auto table = BindlessResourceTable::Create(device, heap, desc);
    CG_EXPECT_FALSE(table);
    if (!table)
    {
        CG_EXPECT_EQ(table.Error().code, ErrorCode::Unsupported);
    }
}

CG_TEST(renderer_phase19_bindless_table_d3d12)
{
    Fixture fixture;
    if (!fixture.OpenD3D12())
    {
        CG_SKIP("no D3D12 software adapter is available");
    }
    if (!fixture.device.Capabilities().features.Has(cyber::rhi::Feature::BindlessResources))
    {
        CG_SKIP("D3D12 adapter does not support bindless resource indexing");
    }

    cyber::rhi::TextureDesc textureDesc{};
    textureDesc.format = cyber::rhi::Format::RGBA8Unorm;
    textureDesc.usage = cyber::rhi::TextureUsage::Sampled;
    textureDesc.width = 8;
    textureDesc.height = 8;
    auto texture = fixture.device.CreateTexture(textureDesc);
    CG_REQUIRE(static_cast<bool>(texture));

    cyber::rhi::TextureViewDesc viewDesc{};
    viewDesc.texture = texture.Value();
    viewDesc.format = cyber::rhi::Format::RGBA8Unorm;
    auto view = fixture.device.CreateTextureView(viewDesc);
    CG_REQUIRE(static_cast<bool>(view));

    cyber::rhi::BufferDesc bufferDesc{};
    bufferDesc.usage = cyber::rhi::BufferUsage::Storage;
    bufferDesc.sizeBytes = 256;
    bufferDesc.elementStride = 16;
    auto buffer = fixture.device.CreateBuffer(bufferDesc);
    CG_REQUIRE(static_cast<bool>(buffer));

    auto sampler = fixture.device.CreateSampler(cyber::rhi::SamplerDesc{});
    CG_REQUIRE(static_cast<bool>(sampler));

    BindlessResourceTableDesc tableDesc{};
    tableDesc.capacity = 2;
    tableDesc.framesInFlight = 2;
    tableDesc.storageElementStride = 16;
    tableDesc.fallbackTexture = view.Value();
    tableDesc.fallbackBuffer = buffer.Value();
    tableDesc.sampler = sampler.Value();
    auto createdTable = BindlessResourceTable::Create(fixture.device, fixture.heap, tableDesc);
    CG_REQUIRE(static_cast<bool>(createdTable));
    BindlessResourceTable table = std::move(createdTable.Value());

    auto textureIndex = table.AllocateTexture(view.Value());
    auto invalidBufferIndex = table.AllocateBuffer(buffer.Value(), 4, 16);
    auto bufferIndex = table.AllocateBuffer(buffer.Value(), 16, 32);
    CG_REQUIRE(static_cast<bool>(textureIndex));
    CG_EXPECT_FALSE(invalidBufferIndex);
    CG_REQUIRE(static_cast<bool>(bufferIndex));

    auto fence = fixture.device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(fence));
    FrameRingDesc ringDesc{};
    ringDesc.fence = fence.Value();
    ringDesc.framesInFlight = 2;
    auto createdRing = FrameRing::Create(ringDesc);
    CG_REQUIRE(static_cast<bool>(createdRing));
    fixture.ring = std::move(createdRing.Value());

    auto frame0 = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame0));
    CG_REQUIRE(table.SyncFrame(fixture.ring, frame0.Value()).IsOk());
    CG_EXPECT_TRUE(fixture.device.BindGroupInfoOf(table.Group(0)).valid);

    CG_REQUIRE(table.UpdateTexture(textureIndex.Value(), view.Value()).IsOk());
    CG_REQUIRE(table.UpdateBuffer(bufferIndex.Value(), buffer.Value(), 32, 32).IsOk());
    CG_REQUIRE(table.SyncFrame(fixture.ring, frame0.Value()).IsOk());
    CG_REQUIRE(table.FreeTexture(textureIndex.Value()).IsOk());
    CG_REQUIRE(table.FreeBuffer(bufferIndex.Value()).IsOk());
    auto frame1 = fixture.ring.BeginFrame(fixture.device);
    CG_REQUIRE(static_cast<bool>(frame1));
    CG_REQUIRE(table.SyncFrame(fixture.ring, frame1.Value()).IsOk());
    CG_EXPECT_TRUE(fixture.device.BindGroupInfoOf(table.Group(1)).valid);
    CG_EXPECT_TRUE(table.Destroy().IsOk());
}
