#include <cyber/core/log.h>
#include <cyber/core/types.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/submission.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::rhi;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;

namespace
{

[[nodiscard]] bool OpenD3D12Device(Device& out, HeapAllocator& heap) noexcept
{
    const Backend* backend = D3D12Backend();
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
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

}  // namespace

CG_TEST(rhi_timestamps_read_back_after_the_completion_fence)
{
    HeapAllocator heap;
    Device device;
    if (!OpenD3D12Device(device, heap))
    {
        CG_SKIP("this build has no Direct3D 12 adapter for timestamp queries");
    }
    const u64 frequency = device.TimestampFrequency(QueueKind::Graphics);
    if (frequency == 0)
    {
        CG_SKIP("this Direct3D 12 adapter does not report a timestamp frequency");
    }

    TimestampQueryHeapDesc queryDesc{};
    queryDesc.queryCount = 2;
    queryDesc.label = "phase 15 pass timings";
    auto queryHeap = device.CreateTimestampQueryHeap(queryDesc);
    CG_REQUIRE(static_cast<bool>(queryHeap));

    auto fenceResult = device.CreateFence(FenceDesc{});
    CG_REQUIRE(static_cast<bool>(fenceResult));
    const FenceHandle fence = fenceResult.Value();

    CommandListDesc listDesc{};
    listDesc.queue = QueueKind::Graphics;
    listDesc.label = "timestamp test";
    auto listResult = device.CreateCommandList(listDesc);
    CG_REQUIRE(static_cast<bool>(listResult));
    CommandList list = std::move(listResult.Value());
    CG_REQUIRE(list.Begin().IsOk());
    CG_REQUIRE(list.WriteTimestamp(queryHeap.Value(), 0).IsOk());
    CG_REQUIRE(list.WriteTimestamp(queryHeap.Value(), 1).IsOk());
    CG_REQUIRE(list.ResolveTimestamps(queryHeap.Value(), 0, 2).IsOk());
    CG_REQUIRE(list.End().IsOk());

    u64 timestamps[2]{};
    const Span<u64> output(timestamps, 2);
    const auto notReady = device.ReadTimestamps(queryHeap.Value(), 0, 2, fence, 1, output);
    CG_EXPECT_FALSE(notReady);
    if (!notReady)
    {
        CG_EXPECT_EQ(notReady.Error().code, cyber::core::ErrorCode::NotFound);
    }

    const CommandListHandle lists[] = {list.Handle()};
    FenceSignal signal{};
    signal.fence = fence;
    signal.value = 1;
    SubmitDesc submit{};
    submit.queue = QueueKind::Graphics;
    submit.commandLists = Span<const CommandListHandle>(lists, 1);
    submit.signals = Span<const FenceSignal>(&signal, 1);
    CG_REQUIRE(device.Submit(submit).IsOk());
    CG_REQUIRE(device.WaitFence(fence, 1).IsOk());
    CG_REQUIRE(device.ReadTimestamps(queryHeap.Value(), 0, 2, fence, 1, output).IsOk());
    CG_EXPECT_TRUE(timestamps[0] != 0);
    CG_EXPECT_TRUE(timestamps[1] >= timestamps[0]);
    CG_LOG(Info, Rhi, "Phase 15 timestamp sample: %.3f ms",
           static_cast<double>(timestamps[1] - timestamps[0]) * 1000.0 /
               static_cast<double>(frequency));

    list = CommandList{};
    CG_EXPECT_TRUE(device.DestroyTimestampQueryHeap(queryHeap.Value()).IsOk());
    CG_EXPECT_TRUE(device.DestroyFence(fence).IsOk());
}
