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
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

inline constexpr u32 kMaxAdapters = 32;

// The adapter list, so a test can pick the software one without assuming it is
// the only entry.
struct AdapterList
{
    AdapterInfo entries[kMaxAdapters]{};
    u32 count = 0;
};

[[nodiscard]] bool ListAdapters(AdapterList& out) noexcept
{
    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        return false;
    }
    out.count = EnumerateAdapters(*backend, Span<AdapterInfo>(out.entries, kMaxAdapters));
    return out.count > 0 && out.count <= kMaxAdapters;
}

// A device on the software adapter, which every supported Windows version has.
// That is what CI runs, and it is the path with the fewest driver variables.
[[nodiscard]] bool OpenSoftwareDevice(Device& out, HeapAllocator& heap,
                                      u32* usedIndex = nullptr) noexcept
{
    AdapterList adapters;
    if (!ListAdapters(adapters))
    {
        return false;
    }
    DeviceDesc desc{};
    desc.backend = D3D12Backend();
    desc.adapterIndex = adapters.count - 1;
    if (usedIndex != nullptr)
    {
        *usedIndex = desc.adapterIndex;
    }
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

}  // namespace

CG_TEST(d3d12_device_reports_the_adapter_it_was_created_on)
{
    HeapAllocator heap;
    AdapterList adapters;
    CG_REQUIRE(ListAdapters(adapters));

    Device device;
    u32 index = 0;
    CG_REQUIRE(OpenSoftwareDevice(device, heap, &index));

    const Capabilities capabilities = device.Capabilities();
    // The device must describe the same adapter enumeration reported for that
    // index; a divergence would mean the two walks disagree about order.
    CG_EXPECT_STREQ(capabilities.adapter.name, adapters.entries[index].name);
    CG_EXPECT_EQ(capabilities.adapter.vendorId, adapters.entries[index].vendorId);
    CG_EXPECT_TRUE(capabilities.adapter.backend == BackendKind::D3D12);
    CG_EXPECT_EQ(capabilities.adapter.apiVersion, u32{1200});
}

CG_TEST(d3d12_device_has_three_queues_and_timeline_fences)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    CG_EXPECT_TRUE(device.Queue(QueueKind::Graphics).IsValid());
    CG_EXPECT_TRUE(device.Queue(QueueKind::Compute).IsValid());
    CG_EXPECT_TRUE(device.Queue(QueueKind::Copy).IsValid());

    // The three handles identify three different queues.
    CG_EXPECT_NE(device.Queue(QueueKind::Graphics), device.Queue(QueueKind::Compute));
    CG_EXPECT_NE(device.Queue(QueueKind::Compute), device.Queue(QueueKind::Copy));

    const Capabilities capabilities = device.Capabilities();
    CG_EXPECT_TRUE(capabilities.features.Has(Feature::TimelineFences));
    CG_EXPECT_TRUE(capabilities.limits.maxBufferBytes > 0);
    CG_EXPECT_EQ(capabilities.limits.maxBindGroups, u32{4});
    CG_EXPECT_TRUE(capabilities.limits.maxPushConstantBytes >= 128);
    CG_EXPECT_TRUE(capabilities.limits.minUniformBufferOffsetAlignment >= 64);
    CG_EXPECT_TRUE(capabilities.limits.maxTexture2D >= 8192);
}

CG_TEST(d3d12_device_rejects_an_adapter_index_past_the_list)
{
    HeapAllocator heap;
    AdapterList adapters;
    CG_REQUIRE(ListAdapters(adapters));

    DeviceDesc desc{};
    desc.backend = D3D12Backend();
    desc.adapterIndex = adapters.count;
    const auto created = Device::Create(desc, heap);
    CG_EXPECT_FALSE(created);
    if (!created)
    {
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::OutOfRange);
    }
    CG_EXPECT_EQ(heap.Stats().used, u64{0});
}

CG_TEST(d3d12_device_fence_is_a_real_timeline)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    FenceDesc desc{};
    desc.initialValue = 3;
    desc.label = "frame fence";
    auto created = device.CreateFence(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();
    CG_EXPECT_EQ(device.FenceValue(fence), u64{3});

    // A value the timeline already reached is satisfied without waiting.
    CG_EXPECT_TRUE(device.WaitFence(fence, 3, 0).IsOk());
    CG_EXPECT_TRUE(device.WaitFence(fence, 1, 0).IsOk());

    // A value it has not reached is a poll miss, not an argument error.
    const auto polled = device.WaitFence(fence, 4, 0);
    CG_EXPECT_FALSE(polled);
    if (!polled)
    {
        CG_EXPECT_EQ(polled.Error().code, cyber::core::ErrorCode::NotFound);
    }

    // Signalling through this backend goes to the graphics queue, so the wait
    // proves the GPU reached the value rather than the CPU bookkeeping alone.
    CG_EXPECT_TRUE(device.SignalFence(fence, 7).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{7});
    CG_EXPECT_TRUE(device.WaitFence(fence, 7).IsOk());

    // The timeline never moves backwards.
    CG_EXPECT_TRUE(device.SignalFence(fence, 2).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{7});
}

CG_TEST(d3d12_device_fence_rejects_stale_handles)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle first = created.Value();
    CG_EXPECT_TRUE(device.DestroyFence(first).IsOk());
    CG_EXPECT_EQ(device.FenceValue(first), u64{0});

    const auto signalled = device.SignalFence(first, 1);
    CG_EXPECT_FALSE(signalled);
    if (!signalled)
    {
        CG_EXPECT_EQ(signalled.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
    CG_EXPECT_FALSE(device.WaitFence(first, 1, 0));
    CG_EXPECT_FALSE(device.DestroyFence(first));
    CG_EXPECT_FALSE(device.DestroyFence(FenceHandle{}));

    // The slot is reused and the old handle keeps referring to nothing.
    auto recreated = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(recreated));
    const FenceHandle second = recreated.Value();
    CG_EXPECT_EQ(second.index, first.index);
    CG_EXPECT_NE(second.generation, first.generation);
    CG_EXPECT_TRUE(device.SignalFence(second, 5).IsOk());
    CG_EXPECT_EQ(device.FenceValue(second), u64{5});
    CG_EXPECT_EQ(device.FenceValue(first), u64{0});
}

CG_TEST(d3d12_device_fence_wait_observes_a_gpu_signal)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();

    // An infinite wait blocks until the queue reaches the value. The signal is
    // issued first, from the same thread, because the queue signal itself is what
    // makes the value reachable.
    CG_EXPECT_TRUE(device.SignalFence(fence, 11).IsOk());
    CG_EXPECT_TRUE(device.WaitFence(fence, 11).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{11});
}

CG_TEST(d3d12_device_unimplemented_calls_report_unsupported)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenSoftwareDevice(device, heap));

    // A call this increment has not reached answers `Unsupported` with a message
    // naming it, rather than pretending to succeed or crashing. Resources and
    // shaders landed, pipelines have not, so a pipeline is the current boundary.
    const auto created = device.CreateGraphicsPipeline(GraphicsPipelineDesc{});
    CG_EXPECT_FALSE(created);
    if (!created)
    {
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::Unsupported);
        CG_EXPECT_TRUE(created.Error().message != nullptr);
    }

    // A destroy of something that was never created is a stale handle.
    const auto destroyed = device.DestroyBuffer(BufferHandle{});
    CG_EXPECT_FALSE(destroyed);
    if (!destroyed)
    {
        CG_EXPECT_EQ(destroyed.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }

    // Idle is meaningful even with nothing recorded.
    CG_EXPECT_TRUE(device.WaitIdle().IsOk());
}

CG_TEST(d3d12_device_releases_everything_it_allocated)
{
    HeapAllocator heap;
    {
        Device device;
        CG_REQUIRE(OpenSoftwareDevice(device, heap));
        CG_EXPECT_TRUE(heap.Stats().used > 0);

        auto first = device.CreateFence(FenceDesc{});
        auto second = device.CreateFence(FenceDesc{});
        CG_REQUIRE(static_cast<bool>(first));
        CG_REQUIRE(static_cast<bool>(second));
        CG_EXPECT_TRUE(device.SignalFence(first.Value(), 1).IsOk());

        // Two devices on one backend share nothing, so a second creation must
        // still work while the first is alive. Handles are per-device and two
        // fresh devices legitimately report the same queue handle, so the proof
        // is that each device tracks its own fences independently.
        HeapAllocator other;
        Device secondDevice;
        CG_REQUIRE(OpenSoftwareDevice(secondDevice, other));
        auto onFirst = device.CreateFence(FenceDesc{});
        auto onSecond = secondDevice.CreateFence(FenceDesc{});
        CG_REQUIRE(static_cast<bool>(onFirst));
        CG_REQUIRE(static_cast<bool>(onSecond));
        CG_EXPECT_TRUE(device.SignalFence(onFirst.Value(), 21).IsOk());
        CG_EXPECT_TRUE(secondDevice.SignalFence(onSecond.Value(), 34).IsOk());
        CG_EXPECT_EQ(device.FenceValue(onFirst.Value()), u64{21});
        CG_EXPECT_EQ(secondDevice.FenceValue(onSecond.Value()), u64{34});
        CG_EXPECT_EQ(other.Stats().used > 0, true);
    }
    // Every allocation is returned, including the fence table and the free list.
    CG_EXPECT_EQ(heap.Stats().used, u64{0});
}
