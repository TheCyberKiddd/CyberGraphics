#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/device.h>
#include <cyber/test/test.h>

#include <chrono>
#include <thread>
#include <utility>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;

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

}  // namespace

CG_TEST(rhi_fence_create_signal_and_wait)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    FenceDesc desc{};
    desc.initialValue = 5;
    desc.label = "frame fence";
    auto created = device.CreateFence(desc);
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();
    CG_EXPECT_TRUE(fence.IsValid());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{5});

    // A wait for a value that is already reached returns immediately.
    CG_EXPECT_TRUE(device.WaitFence(fence, 5, 0).IsOk());
    CG_EXPECT_TRUE(device.WaitFence(fence, 1, 0).IsOk());

    CG_EXPECT_TRUE(device.SignalFence(fence, 9).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{9});
    CG_EXPECT_TRUE(device.WaitFence(fence, 9, 0).IsOk());

    // The default timeout is "block until reached", and it must not block when
    // the value is already there.
    CG_EXPECT_TRUE(device.WaitFence(fence, 9).IsOk());

    // Destroying invalidates the handle and frees the slot.
    CG_EXPECT_TRUE(device.DestroyFence(fence).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{0});
}

CG_TEST(rhi_fence_wait_times_out)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();

    // A poll on an unreached value is NotFound, not InvalidArgument: the caller
    // must be able to tell "not yet" from "wrong handle".
    const auto polled = device.WaitFence(fence, 1, 0);
    CG_EXPECT_FALSE(polled);
    if (!polled)
    {
        CG_EXPECT_EQ(polled.Error().code, cyber::core::ErrorCode::NotFound);
    }

    // A bounded wait really waits and then reports the same code.
    const auto start = std::chrono::steady_clock::now();
    const auto timedOut = device.WaitFence(fence, 1, 5'000'000ULL);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CG_EXPECT_FALSE(timedOut);
    if (!timedOut)
    {
        CG_EXPECT_EQ(timedOut.Error().code, cyber::core::ErrorCode::NotFound);
    }
    CG_EXPECT_TRUE(elapsed >= std::chrono::milliseconds(4));
}

CG_TEST(rhi_fence_blocks_until_signalled)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();

    std::thread signaller(
        [&device, fence]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            (void)device.SignalFence(fence, 42);
        });

    const auto start = std::chrono::steady_clock::now();
    const auto waited = device.WaitFence(fence, 42);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    signaller.join();

    CG_EXPECT_TRUE(waited.IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{42});
    // The wait must have actually blocked rather than returned because the value
    // happened to be reached already.
    CG_EXPECT_TRUE(elapsed >= std::chrono::milliseconds(10));
}

CG_TEST(rhi_fence_timeline_never_goes_backwards)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();

    CG_EXPECT_TRUE(device.SignalFence(fence, 10).IsOk());
    // A lower signal is accepted and ignored, because a copy queue that finishes
    // out of order must not rewind a timeline another queue already waited on.
    CG_EXPECT_TRUE(device.SignalFence(fence, 3).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{10});
    CG_EXPECT_TRUE(device.SignalFence(fence, 10).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{10});
    CG_EXPECT_TRUE(device.SignalFence(fence, 11).IsOk());
    CG_EXPECT_EQ(device.FenceValue(fence), u64{11});
}

CG_TEST(rhi_fence_stale_handles_are_rejected)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle first = created.Value();
    CG_EXPECT_TRUE(device.DestroyFence(first).IsOk());

    // Every operation on a destroyed fence reports the handle, not a crash.
    const auto signalled = device.SignalFence(first, 1);
    CG_EXPECT_FALSE(signalled);
    if (!signalled)
    {
        CG_EXPECT_EQ(signalled.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
    CG_EXPECT_FALSE(device.WaitFence(first, 1, 0));
    CG_EXPECT_FALSE(device.DestroyFence(first));
    CG_EXPECT_EQ(device.FenceValue(first), u64{0});

    // A default handle is equally invalid.
    CG_EXPECT_FALSE(device.SignalFence(FenceHandle{}, 1));
    CG_EXPECT_FALSE(device.DestroyFence(FenceHandle{}));

    // The freed slot is reused, and the new handle must not resolve as the old
    // one. This is the property a generation counter exists for.
    auto recreated = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(recreated));
    const FenceHandle second = recreated.Value();
    CG_EXPECT_EQ(second.index, first.index);
    CG_EXPECT_NE(second.generation, first.generation);
    CG_EXPECT_NE(second, first);

    CG_EXPECT_TRUE(device.SignalFence(second, 2).IsOk());
    CG_EXPECT_EQ(device.FenceValue(second), u64{2});
    // The old handle still refers to nothing even though its slot is live again.
    CG_EXPECT_EQ(device.FenceValue(first), u64{0});
    CG_EXPECT_FALSE(device.SignalFence(first, 3));
    CG_EXPECT_EQ(device.FenceValue(second), u64{2});
}

CG_TEST(rhi_fence_handle_wait_while_destroyed)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto created = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(created));
    const FenceHandle fence = created.Value();

    // A waiter must wake and report a stale handle when the fence it waits on is
    // destroyed, rather than blocking forever or reading freed memory.
    std::thread destroyer(
        [&device, fence]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            (void)device.DestroyFence(fence);
        });

    const auto waited = device.WaitFence(fence, 1);
    destroyer.join();
    CG_EXPECT_FALSE(waited);
    if (!waited)
    {
        CG_EXPECT_EQ(waited.Error().code, cyber::core::ErrorCode::InvalidArgument);
    }
}
