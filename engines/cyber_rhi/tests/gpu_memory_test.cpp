// The Phase 15 GPU memory test (ADR 0018, increment 1).
//
// Everything here is observable through `Device::MemoryStats()`: blocks, bytes
// reserved, bytes used, and the largest free stride. The allocator's own free
// list is not reachable from a test, which is deliberate — the contract the
// callers hold is the report, not the bookkeeping.

#include <cyber/memory/heap_allocator.h>
#include <cyber/rhi/d3d12.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/texture.h>
#include <cyber/test/test.h>

namespace cyber::rhi::test
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::HeapAllocator;

namespace
{

constexpr u64 kMiB = 1024U * 1024U;
constexpr u64 kSegment = 64U * 1024U;  // D3D12's default placement alignment.
constexpr u64 kFirstHeap = 8U * kMiB;  // ADR 0018: growth starts at 8 MiB.

// A device on the last adapter: WARP when the machine has no GPU, which is
// where CI runs and where the allocator has the fewest shared drivers in the
// picture.
[[nodiscard]] bool OpenDevice(Device& out, HeapAllocator& heap) noexcept
{
    const Backend* backend = D3D12Backend();
    if (backend == nullptr)
    {
        return false;
    }
    const u32 adapters = backend->enumerateAdapters(nullptr, 0);
    if (adapters == 0)
    {
        return false;
    }
    DeviceDesc desc{};
    desc.backend = backend;
    desc.adapterIndex = adapters - 1;
    auto created = Device::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

[[nodiscard]] bool IsEmpty(const MemoryStats& stats) noexcept
{
    return stats.deviceLocal.blockCount == 0 && stats.deviceLocal.reservedBytes == 0 &&
           stats.deviceLocal.usedBytes == 0 && stats.upload.blockCount == 0 &&
           stats.upload.reservedBytes == 0 && stats.upload.usedBytes == 0 &&
           stats.readback.blockCount == 0 && stats.readback.reservedBytes == 0 &&
           stats.readback.usedBytes == 0;
}

[[nodiscard]] BufferDesc BufferOfSize(u64 sizeBytes, BufferUsage usage, const char* label) noexcept
{
    BufferDesc desc{};
    desc.sizeBytes = sizeBytes;
    desc.usage = usage;
    desc.label = label;
    return desc;
}

}  // namespace

CG_TEST(gpu_memory_empty_device_reports_nothing)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_TRUE(IsEmpty(stats));
    CG_EXPECT_EQ(stats.ReservedBytes(), u64{0});
    CG_EXPECT_EQ(stats.UsedBytes(), u64{0});
}

CG_TEST(gpu_memory_first_allocation_opens_one_block_per_class)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    // One buffer of each kind. Each class opens its first 8 MiB block, and the
    // rounded reservation of each buffer is one 64 KiB segment.
    auto vertex = device.CreateBuffer(BufferOfSize(kSegment, BufferUsage::Vertex, "vertex"));
    CG_REQUIRE(static_cast<bool>(vertex));
    auto upload = device.CreateBuffer(BufferOfSize(kSegment, BufferUsage::HostVisible, "upload"));
    CG_REQUIRE(static_cast<bool>(upload));
    auto readback = device.CreateBuffer(BufferOfSize(kSegment, BufferUsage::Readback, "readback"));
    CG_REQUIRE(static_cast<bool>(readback));

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.deviceLocal.blockCount, u32{1});
    CG_EXPECT_EQ(stats.upload.blockCount, u32{1});
    CG_EXPECT_EQ(stats.readback.blockCount, u32{1});
    CG_EXPECT_EQ(stats.deviceLocal.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(stats.upload.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(stats.readback.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(stats.deviceLocal.usedBytes, kSegment);
    CG_EXPECT_EQ(stats.upload.usedBytes, kSegment);
    CG_EXPECT_EQ(stats.readback.usedBytes, kSegment);
}

CG_TEST(gpu_memory_neighbouring_allocations_share_a_block)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    // Four buffers whose combined rounded reservation is far below the first
    // block: all of them must land inside it, so the block count and the
    // reserved bytes stay put while the used bytes climb by a segment each.
    BufferHandle buffers[4];
    for (u32 index = 0; index < 4; ++index)
    {
        auto created =
            device.CreateBuffer(BufferOfSize(kSegment, BufferUsage::CopyDestination, "sub"));
        CG_REQUIRE(static_cast<bool>(created));
        buffers[index] = created.Value();
    }

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.deviceLocal.blockCount, u32{1});
    CG_EXPECT_EQ(stats.deviceLocal.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(stats.deviceLocal.usedBytes, 4 * kSegment);
    CG_EXPECT_EQ(stats.deviceLocal.wasteBytes, u64{0});

    for (const BufferHandle buffer : buffers)
    {
        CG_EXPECT_TRUE(device.DestroyBuffer(buffer));
    }

    // Freed space returns to the block's report, not to the OS.
    const MemoryStats released = device.MemoryStats();
    CG_EXPECT_EQ(released.deviceLocal.blockCount, u32{1});
    CG_EXPECT_EQ(released.deviceLocal.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(released.deviceLocal.usedBytes, u64{0});
    CG_EXPECT_EQ(released.deviceLocal.largestFreeBytes, kFirstHeap);
}

CG_TEST(gpu_memory_classes_do_not_mix)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto upload = device.CreateBuffer(BufferOfSize(kSegment, BufferUsage::HostVisible, "a"));
    CG_REQUIRE(static_cast<bool>(upload));
    auto readback = device.CreateBuffer(BufferOfSize(kSegment, BufferUsage::Readback, "b"));
    CG_REQUIRE(static_cast<bool>(readback));

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.deviceLocal.blockCount, u32{0});
    CG_EXPECT_EQ(stats.upload.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(stats.readback.reservedBytes, kFirstHeap);
    // Destroying the upload buffer leaves the readback class untouched.
    CG_EXPECT_TRUE(device.DestroyBuffer(upload.Value()));
    const MemoryStats after = device.MemoryStats();
    CG_EXPECT_EQ(after.upload.usedBytes, u64{0});
    CG_EXPECT_EQ(after.readback.usedBytes, kSegment);
}

CG_TEST(gpu_memory_freed_reservation_is_reused)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    auto first = device.CreateBuffer(BufferOfSize(4 * kSegment, BufferUsage::Vertex, "first"));
    CG_REQUIRE(static_cast<bool>(first));
    CG_EXPECT_TRUE(device.DestroyBuffer(first.Value()));

    // A same-sized request must land in the freed range, not open a new heap.
    auto second = device.CreateBuffer(BufferOfSize(4 * kSegment, BufferUsage::Vertex, "second"));
    CG_REQUIRE(static_cast<bool>(second));
    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.deviceLocal.blockCount, u32{1});
    CG_EXPECT_EQ(stats.deviceLocal.reservedBytes, kFirstHeap);
    CG_EXPECT_EQ(stats.deviceLocal.usedBytes, 4 * kSegment);
    CG_EXPECT_EQ(stats.deviceLocal.wasteBytes, u64{0});
}

CG_TEST(gpu_memory_growing_a_class_doubles_the_block_size)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    // 1 MiB reservations: eight fill the first 8 MiB block exactly. The ninth
    // opens the next block, whose size is the doubled policy (16 MiB), and
    // takes the request. Reserved bytes then report 8 + 16 MiB.
    BufferHandle buffers[9];
    for (u32 index = 0; index < 9; ++index)
    {
        auto created =
            device.CreateBuffer(BufferOfSize(1 * kMiB, BufferUsage::CopyDestination, "grow"));
        CG_REQUIRE(static_cast<bool>(created));
        buffers[index] = created.Value();
    }

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.deviceLocal.blockCount, u32{2});
    CG_EXPECT_EQ(stats.deviceLocal.reservedBytes, kFirstHeap + 2 * kFirstHeap);
    CG_EXPECT_EQ(stats.deviceLocal.usedBytes, 9 * kMiB);

    for (const BufferHandle buffer : buffers)
    {
        CG_EXPECT_TRUE(device.DestroyBuffer(buffer));
    }
    const MemoryStats released = device.MemoryStats();
    CG_EXPECT_EQ(released.deviceLocal.blockCount, u32{2});
    CG_EXPECT_EQ(released.deviceLocal.reservedBytes, kFirstHeap + 2 * kFirstHeap);
    CG_EXPECT_EQ(released.deviceLocal.usedBytes, u64{0});
    // The two free ranges: 8 MiB in the first block, 16 MiB in the second.
    CG_EXPECT_EQ(released.deviceLocal.largestFreeBytes, 16 * kMiB);
}

CG_TEST(gpu_memory_oversized_request_gets_a_dedicated_block)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    // Above the largest block size the policy serves, the request is its own
    // heap: reserved bytes report exactly its rounded size, and a same-size
    // neighbour opens another dedicated block rather than sharing.
    const u64 big = 256 * kMiB + kSegment;
    auto first = device.CreateBuffer(BufferOfSize(big, BufferUsage::CopyDestination, "big"));
    CG_REQUIRE(static_cast<bool>(first));
    auto second = device.CreateBuffer(BufferOfSize(big, BufferUsage::CopyDestination, "big2"));
    CG_REQUIRE(static_cast<bool>(second));

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.deviceLocal.blockCount, u32{2});
    CG_EXPECT_EQ(stats.deviceLocal.reservedBytes, 2 * big);
    CG_EXPECT_EQ(stats.deviceLocal.usedBytes, 2 * big);

    CG_EXPECT_TRUE(device.DestroyBuffer(first.Value()));
    const MemoryStats released = device.MemoryStats();
    CG_EXPECT_EQ(released.deviceLocal.blockCount, u32{1});
    CG_EXPECT_EQ(released.deviceLocal.reservedBytes, big);
    CG_EXPECT_EQ(released.deviceLocal.usedBytes, big);
}

CG_TEST(gpu_memory_counts_alignment_padding_as_waste)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    // A 100 KiB reservation rounds up to two 64 KiB segments: 28 KiB of the
    // second segment is alignment padding, which the report shows as waste.
    auto buffer = device.CreateBuffer(BufferOfSize(100 * 1024, BufferUsage::HostVisible, "pad"));
    CG_REQUIRE(static_cast<bool>(buffer));

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_EQ(stats.upload.usedBytes, 2 * kSegment);
    CG_EXPECT_EQ(stats.upload.wasteBytes, 2 * kSegment - 100 * 1024);
}

CG_TEST(gpu_memory_zero_size_is_refused)
{
    HeapAllocator heap;
    Device device;
    CG_REQUIRE(OpenDevice(device, heap));

    // A zero-byte reservation cannot place a command list marker, so it is an
    // invalid argument, not an allocation of nothing.
    auto created = device.CreateBuffer(BufferOfSize(0, BufferUsage::Vertex, "zero"));
    CG_EXPECT_FALSE(created);
    CG_EXPECT_EQ(created.Error().code, cyber::core::ErrorCode::InvalidArgument);

    const MemoryStats stats = device.MemoryStats();
    CG_EXPECT_TRUE(IsEmpty(stats));
}

}  // namespace cyber::rhi::test
