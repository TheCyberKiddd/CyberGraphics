#pragma once

#include <cyber/memory/allocator.h>

namespace cyber::memory
{

// Variable-size allocator with boundary tags, a first-fit free list, and
// address-order coalescing. Every block starts with a 64-byte header and the
// payload is 64-byte aligned. Overhead is therefore 64 bytes per allocation and
// the minimum block is 64 bytes.
//
// Requested alignment must be a power of two no greater than 64. Deallocate
// validates the block tag before touching the free list.
class FreeListAllocator final : public Allocator
{
public:
    static constexpr usize kHeaderSize = 64;
    static constexpr usize kMaxAlignment = 64;

    // Uses buffer[0, capacity). The buffer must stay alive for the lifetime of
    // the allocator.
    explicit FreeListAllocator(void* buffer, usize capacity) noexcept;

    // Allocates the heap storage from backing.
    explicit FreeListAllocator(Allocator& backing, usize capacity) noexcept;

    ~FreeListAllocator() override;

    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;

    [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) noexcept override;
    void Deallocate(void* pointer) noexcept override;
    void Reset() noexcept override;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept override;
    [[nodiscard]] AllocatorStats Stats() const noexcept override;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] usize Capacity() const noexcept;
    [[nodiscard]] usize Used() const noexcept;
    [[nodiscard]] usize FreeBlockCount() const noexcept;
    [[nodiscard]] usize LargestFreeBlock() const noexcept;

private:
    struct BlockHeader
    {
        usize size = 0;
        usize prevSize = 0;
        BlockHeader* nextFree = nullptr;
        BlockHeader* prevFree = nullptr;
        usize requested = 0;
        u32 magic = 0;
        u32 flags = 0;
        u64 reserved = 0;
    };

    static_assert(sizeof(BlockHeader) <= kHeaderSize);

    void Initialize() noexcept;
    void Unlink(BlockHeader* block) noexcept;
    void PushFront(BlockHeader* block) noexcept;
    void CoalesceWithNext(BlockHeader* block) noexcept;
    [[nodiscard]] bool IsFree(const BlockHeader* block) const noexcept;

    Byte* m_buffer = nullptr;
    usize m_capacity = 0;
    usize m_used = 0;
    usize m_peak = 0;
    usize m_allocations = 0;
    BlockHeader* m_freeHead = nullptr;
    Allocator* m_backing = nullptr;
    void* m_raw = nullptr;
    bool m_owns = false;
};

}  // namespace cyber::memory
