#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;

namespace
{

DeviceDesc NullDeviceDesc(u32 adapterIndex = 0) noexcept
{
    DeviceDesc desc{};
    desc.backend = NullBackend();
    desc.adapterIndex = adapterIndex;
    desc.label = "test device";
    return desc;
}

}  // namespace

CG_TEST(rhi_device_create_and_destroy)
{
    HeapAllocator heap;
    {
        auto created = Device::Create(NullDeviceDesc(), heap);
        CG_REQUIRE(static_cast<bool>(created));

        Device device = std::move(created.Value());
        CG_EXPECT_TRUE(device.IsValid());
        CG_EXPECT_STREQ(device.BackendName(), "null");
        CG_EXPECT_TRUE(device.WaitIdle());
    }
    // Creating and destroying a device must not leak the allocator it used.
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_device_queues)
{
    HeapAllocator heap;
    auto created = Device::Create(NullDeviceDesc(), heap);
    CG_REQUIRE(static_cast<bool>(created));
    Device device = std::move(created.Value());

    const QueueHandle graphics = device.Queue(QueueKind::Graphics);
    const QueueHandle compute = device.Queue(QueueKind::Compute);
    const QueueHandle copy = device.Queue(QueueKind::Copy);
    CG_EXPECT_TRUE(graphics.IsValid());
    CG_EXPECT_TRUE(compute.IsValid());
    CG_EXPECT_TRUE(copy.IsValid());

    // Three classes, three distinct queues, and asking twice returns the same
    // handle: a queue is not created per call.
    CG_EXPECT_NE(graphics, compute);
    CG_EXPECT_NE(compute, copy);
    CG_EXPECT_NE(graphics, copy);
    CG_EXPECT_EQ(device.Queue(QueueKind::Graphics), graphics);

    CG_EXPECT_FALSE(device.Queue(QueueKind::Count).IsValid());
    CG_EXPECT_FALSE(device.Queue(static_cast<QueueKind>(kQueueKindCount + 2)).IsValid());
}

CG_TEST(rhi_device_capabilities_reported)
{
    HeapAllocator heap;
    auto created = Device::Create(NullDeviceDesc(), heap);
    CG_REQUIRE(static_cast<bool>(created));
    Device device = std::move(created.Value());

    const Capabilities capabilities = device.Capabilities();
    CG_EXPECT_TRUE(capabilities.limits.maxTexture2D > 0);
    CG_EXPECT_TRUE(capabilities.features.Has(Feature::Compute));
    CG_EXPECT_EQ(capabilities.adapter.backend, BackendKind::Null);

    // Capabilities are a value: reading them twice yields the same thing and
    // neither read is a reference into the device.
    const Capabilities again = device.Capabilities();
    CG_EXPECT_EQ(capabilities.limits.maxTexture2D, again.limits.maxTexture2D);
    CG_EXPECT_EQ(capabilities.features, again.features);
}

CG_TEST(rhi_device_invalid_arguments)
{
    HeapAllocator heap;

    // A null backend is a caller bug, not a crash.
    DeviceDesc noBackend{};
    auto created = Device::Create(noBackend, heap);
    CG_EXPECT_FALSE(static_cast<bool>(created));
    if (!created)
    {
        CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::InvalidArgument);
        CG_EXPECT_TRUE(created.Error().message != nullptr);
    }

    // The null backend has exactly one adapter.
    auto outOfRange = Device::Create(NullDeviceDesc(1), heap);
    CG_EXPECT_FALSE(static_cast<bool>(outOfRange));
    if (!outOfRange)
    {
        CG_EXPECT_EQ(outOfRange.Error().code, cyber::core::ErrorCode::OutOfRange);
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});

    // An incomplete function table is rejected before anything is allocated.
    Backend broken = *NullBackend();
    broken.destroyDevice = nullptr;
    DeviceDesc brokenDesc{};
    brokenDesc.backend = &broken;
    auto brokenResult = Device::Create(brokenDesc, heap);
    CG_EXPECT_FALSE(static_cast<bool>(brokenResult));
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_device_moved_from_is_empty)
{
    HeapAllocator heap;
    auto created = Device::Create(NullDeviceDesc(), heap);
    CG_REQUIRE(static_cast<bool>(created));

    Device source = std::move(created.Value());
    Device moved = std::move(source);
    CG_EXPECT_TRUE(moved.IsValid());
    CG_EXPECT_FALSE(source.IsValid());
    CG_EXPECT_STREQ(source.BackendName(), "none");

    // A moved-from device rejects every call instead of touching freed state.
    CG_EXPECT_EQ(source.Capabilities().limits.maxTexture2D, u32{0});
    CG_EXPECT_FALSE(source.Queue(QueueKind::Graphics).IsValid());
    CG_EXPECT_FALSE(source.WaitIdle());
    CG_EXPECT_FALSE(source.CreateFence(FenceDesc{}));
    CG_EXPECT_FALSE(source.SignalFence(FenceHandle{}, 1));
    CG_EXPECT_FALSE(source.WaitFence(FenceHandle{}, 1, 0));
    CG_EXPECT_FALSE(source.DestroyFence(FenceHandle{}));
    CG_EXPECT_EQ(source.FenceValue(FenceHandle{}), u64{0});

    // Move assignment releases what the target owned, then takes the source.
    HeapAllocator secondHeap;
    auto secondCreated = Device::Create(NullDeviceDesc(), secondHeap);
    CG_REQUIRE(static_cast<bool>(secondCreated));
    Device target = std::move(secondCreated.Value());
    target = std::move(moved);
    CG_EXPECT_TRUE(target.IsValid());
    CG_EXPECT_FALSE(moved.IsValid());
    CG_EXPECT_EQ(secondHeap.Stats().used, cyber::core::usize{0});
}

CG_TEST(rhi_device_fences_are_destroyed_with_the_device)
{
    HeapAllocator heap;
    {
        auto created = Device::Create(NullDeviceDesc(), heap);
        CG_REQUIRE(static_cast<bool>(created));

        Device device = std::move(created.Value());
        FenceDesc desc{};
        desc.label = "frame";
        for (u32 index = 0; index < 64; ++index)
        {
            auto fence = device.CreateFence(desc);
            CG_REQUIRE(static_cast<bool>(fence));
            CG_EXPECT_TRUE(device.SignalFence(fence.Value(), index + 1).IsOk());
        }
        // The fence table grew past its first allocation; destroying the device
        // must release it too.
    }
    CG_EXPECT_EQ(heap.Stats().used, cyber::core::usize{0});
}
