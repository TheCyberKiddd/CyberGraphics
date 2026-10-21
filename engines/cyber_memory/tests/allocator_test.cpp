#include <cyber/memory/memory.h>
#include <cyber/test/test.h>

#include <cstddef>

using namespace cyber::memory;

namespace
{

struct LeakCapture
{
    usize count = 0;
    usize size = 0;
    const char* tag = nullptr;
};

void CaptureLeak(void* context, const LeakInfo& leak) noexcept
{
    auto* capture = static_cast<LeakCapture*>(context);
    ++capture->count;
    capture->size = leak.size;
    capture->tag = leak.tag;
}

u32 NextRandom(u32& state) noexcept
{
    state = state * 1664525U + 1013904223U;
    return state;
}

}  // namespace

CG_TEST(memory_heap_allocator)
{
    HeapAllocator heap;

    void* block = heap.Allocate(100, 64);
    CG_REQUIRE(block != nullptr);
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(block), 64));
    CG_EXPECT_TRUE(heap.Owns(block));
    CG_EXPECT_TRUE(heap.Stats().used >= 100);
    CG_EXPECT_TRUE(heap.Stats().peak >= 100);

    heap.Deallocate(block);
    CG_EXPECT_EQ(heap.Stats().used, usize{0});
    CG_EXPECT_TRUE(heap.Allocate(0, 16) == nullptr);
    CG_EXPECT_TRUE(heap.Allocate(16, 3) == nullptr);
    heap.Deallocate(nullptr);
}

CG_TEST(memory_linear_allocator)
{
    alignas(64) Byte buffer[256];
    LinearAllocator allocator(buffer, sizeof(buffer));

    void* first = allocator.Allocate(16, 16);
    CG_REQUIRE(first != nullptr);
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(first), 16));

    void* second = allocator.Allocate(32, 64);
    CG_REQUIRE(second != nullptr);
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(second), 64));
    CG_EXPECT_TRUE(allocator.Used() > 48);
    CG_EXPECT_TRUE(allocator.Stats().peak >= allocator.Used());

    CG_EXPECT_TRUE(allocator.Owns(first));
    CG_EXPECT_FALSE(allocator.Owns(buffer + sizeof(buffer)));

    CG_EXPECT_TRUE(allocator.Allocate(4096, 16) == nullptr);
    CG_EXPECT_TRUE(allocator.TryResetTo(allocator.Used() + 1) == false);
    allocator.Reset();
    CG_EXPECT_EQ(allocator.Used(), usize{0});
    CG_EXPECT_EQ(allocator.Stats().allocations, usize{0});

    HeapAllocator heap;
    {
        LinearAllocator backed(heap, 128);
        CG_REQUIRE(backed.IsValid());
        CG_EXPECT_TRUE(backed.Allocate(64, 16) != nullptr);
        CG_EXPECT_TRUE(heap.Stats().used >= 128);
    }
    CG_EXPECT_EQ(heap.Stats().used, usize{0});
}

CG_TEST(memory_stack_allocator)
{
    alignas(64) Byte buffer[256];
    StackAllocator allocator(buffer, sizeof(buffer));

    const StackAllocator::Marker start = allocator.Mark();
    void* first = allocator.Allocate(32, 16);
    CG_REQUIRE(first != nullptr);

    const StackAllocator::Marker middle = allocator.Mark();
    void* second = allocator.Allocate(64, 16);
    CG_REQUIRE(second != nullptr);
    CG_EXPECT_TRUE(middle > start);

    CG_EXPECT_TRUE(allocator.FreeTo(middle));
    CG_EXPECT_EQ(allocator.Used(), middle);
    CG_EXPECT_TRUE(allocator.FreeTo(start));
    CG_EXPECT_EQ(allocator.Used(), usize{0});
    CG_EXPECT_FALSE(allocator.FreeTo(allocator.Capacity() + 1));
}

CG_TEST(memory_pool_allocator)
{
    alignas(64) Byte buffer[512];
    PoolAllocator pool(buffer, sizeof(buffer), 32, 16);

    CG_REQUIRE(pool.IsValid());
    CG_EXPECT_EQ(pool.BlockSize(), usize{32});
    CG_EXPECT_EQ(pool.BlockCount(), usize{16});

    void* first = pool.Allocate(16, 16);
    void* second = pool.Allocate(32, 16);
    CG_REQUIRE(first != nullptr);
    CG_REQUIRE(second != nullptr);
    CG_EXPECT_NE(first, second);
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(first), 16));
    CG_EXPECT_TRUE(pool.Allocate(33, 16) == nullptr);
    CG_EXPECT_TRUE(pool.Allocate(16, 32) == nullptr);
    CG_EXPECT_FALSE(pool.Owns(buffer + sizeof(buffer)));

    pool.Deallocate(first);
    pool.Deallocate(first);
    void* reused = pool.Allocate(16, 16);
    CG_REQUIRE(reused != nullptr);

    while (pool.Allocate(16, 16) != nullptr)
    {
    }
    CG_EXPECT_EQ(pool.FreeCount(), usize{0});

    pool.Reset();
    CG_EXPECT_EQ(pool.FreeCount(), pool.BlockCount());
    CG_EXPECT_EQ(pool.Stats().used, usize{0});
}

CG_TEST(memory_free_list_allocator)
{
    alignas(64) Byte buffer[2048];
    FreeListAllocator allocator(buffer, sizeof(buffer));

    CG_REQUIRE(allocator.IsValid());
    void* first = allocator.Allocate(100, 16);
    void* second = allocator.Allocate(200, 64);
    void* third = allocator.Allocate(50, 16);
    CG_REQUIRE(first != nullptr);
    CG_REQUIRE(second != nullptr);
    CG_REQUIRE(third != nullptr);
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(first), 16));
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(second), 64));
    CG_EXPECT_TRUE(allocator.IsValid());
    CG_EXPECT_TRUE(allocator.Used() >= 350);
    CG_EXPECT_TRUE(allocator.Allocate(4096, 16) == nullptr);
    CG_EXPECT_TRUE(allocator.Allocate(16, 128) == nullptr);

    allocator.Deallocate(second);
    allocator.Deallocate(first);
    allocator.Deallocate(third);
    CG_EXPECT_TRUE(allocator.IsValid());
    CG_EXPECT_EQ(allocator.FreeBlockCount(), usize{1});
    CG_EXPECT_EQ(allocator.LargestFreeBlock(), allocator.Capacity());
    CG_EXPECT_EQ(allocator.Stats().used, usize{0});

    void* again = allocator.Allocate(1024, 64);
    CG_REQUIRE(again != nullptr);
    allocator.Deallocate(again);
    allocator.Reset();
    CG_EXPECT_EQ(allocator.FreeBlockCount(), usize{1});
    CG_EXPECT_EQ(allocator.Stats().used, usize{0});
    allocator.Deallocate(buffer);
}

CG_TEST(memory_free_list_allocator_stress)
{
    alignas(64) Byte buffer[16384];
    FreeListAllocator allocator(buffer, sizeof(buffer));

    constexpr usize kCount = 128;
    void* blocks[kCount]{};
    u32 state = 0x12345678U;
    usize requestedTotal = 0;

    for (usize index = 0; index < kCount; ++index)
    {
        const usize size = 1 + (NextRandom(state) % 64);
        const usize alignment = usize{1} << (NextRandom(state) % 7);
        blocks[index] = allocator.Allocate(size, alignment);
        CG_REQUIRE(blocks[index] != nullptr);
        requestedTotal += size;
        CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(blocks[index]), alignment));
        CG_EXPECT_TRUE(allocator.IsValid());
    }

    CG_EXPECT_EQ(allocator.Stats().used, requestedTotal);
    CG_EXPECT_EQ(allocator.FreeBlockCount(), usize{0});

    for (usize index = kCount; index > 1; --index)
    {
        const usize swapIndex = NextRandom(state) % index;
        void* temporary = blocks[index - 1];
        blocks[index - 1] = blocks[swapIndex];
        blocks[swapIndex] = temporary;
    }

    for (usize index = 0; index < kCount; ++index)
    {
        allocator.Deallocate(blocks[index]);
        CG_EXPECT_TRUE(allocator.IsValid());
    }

    CG_EXPECT_EQ(allocator.Stats().used, usize{0});
    CG_EXPECT_EQ(allocator.FreeBlockCount(), usize{1});
    CG_EXPECT_EQ(allocator.LargestFreeBlock(), allocator.Capacity());
}

CG_TEST(memory_ring_allocator)
{
    alignas(64) Byte buffer[128];
    RingAllocator allocator(buffer, sizeof(buffer));

    CG_REQUIRE(allocator.IsValid());
    void* first = allocator.Allocate(32, 16);
    void* second = allocator.Allocate(32, 16);
    void* third = allocator.Allocate(32, 16);
    CG_REQUIRE(first != nullptr);
    CG_REQUIRE(second != nullptr);
    CG_REQUIRE(third != nullptr);
    CG_EXPECT_FALSE(allocator.HasWrapped());

    void* wrapped = allocator.Allocate(64, 16);
    CG_REQUIRE(wrapped != nullptr);
    CG_EXPECT_TRUE(allocator.HasWrapped());
    CG_EXPECT_TRUE(allocator.Allocate(256, 16) == nullptr);

    allocator.Reset();
    CG_EXPECT_FALSE(allocator.HasWrapped());
    CG_EXPECT_EQ(allocator.Used(), usize{0});
}

CG_TEST(memory_frame_allocator)
{
    alignas(64) Byte buffer[256];
    FrameAllocator allocator(buffer, sizeof(buffer));

    allocator.BeginFrame();
    CG_REQUIRE(allocator.Allocate(64, 16) != nullptr);
    allocator.EndFrame();
    CG_EXPECT_TRUE(allocator.Used() > 0);

    allocator.BeginFrame();
    CG_EXPECT_EQ(allocator.Used(), usize{0});
}

CG_TEST(memory_tracking_allocator)
{
    HeapAllocator heap;
    TrackingAllocator tracking(heap, "frame");

    void* first = tracking.Allocate(32, 16);
    void* tagged = tracking.AllocateTagged(64, 64, "texture");
    CG_REQUIRE(first != nullptr);
    CG_REQUIRE(tagged != nullptr);
    CG_EXPECT_TRUE(IsAligned(reinterpret_cast<usize>(tagged), 64));
    CG_EXPECT_EQ(tracking.LeakCount(), usize{2});
    CG_EXPECT_TRUE(tracking.Owns(first));

    LeakCapture capture;
    CG_EXPECT_EQ(tracking.ReportLeaks(CaptureLeak, &capture), usize{2});
    CG_EXPECT_EQ(capture.count, usize{2});
    CG_EXPECT_TRUE(capture.tag != nullptr);

    tracking.Deallocate(first);
    CG_EXPECT_EQ(tracking.LeakCount(), usize{1});
    tracking.Deallocate(tagged);
    CG_EXPECT_EQ(tracking.LeakCount(), usize{0});
    CG_EXPECT_EQ(tracking.Stats().used, usize{0});
    CG_EXPECT_EQ(heap.Stats().used, usize{0});

    tracking.SetDefaultTag("assets");
    CG_EXPECT_STREQ(tracking.DefaultTag(), "assets");
    void* leaked = tracking.Allocate(16, 16);
    CG_REQUIRE(leaked != nullptr);

    LeakCapture leak;
    CG_EXPECT_EQ(tracking.ReportLeaks(CaptureLeak, &leak), usize{1});
    CG_EXPECT_EQ(leak.size, usize{16});
    CG_EXPECT_STREQ(leak.tag, "assets");

    tracking.Deallocate(leaked);
    CG_EXPECT_EQ(tracking.ReportLeaks(nullptr, nullptr), usize{0});
}
