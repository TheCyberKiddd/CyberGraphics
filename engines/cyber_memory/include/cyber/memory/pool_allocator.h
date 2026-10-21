#pragma once

#include <cyber/memory/allocator.h>

namespace cyber::memory
{

// Fixed-size block allocator. All blocks are aligned to blockAlignment and the
// allocator stores its free list inside the free blocks, so there is no
// per-allocation header. Allocate rejects requests larger than the block size or
// with stricter alignment than the pool guarantees.
class PoolAllocator final : public Allocator
{
public:
    // Uses buffer[0, capacity). The buffer must stay alive for the lifetime of
    // the allocator.
    PoolAllocator(void* buffer, usize capacity, usize blockSize,
                  usize blockAlignment = kDefaultAlignment) noexcept;

    // Allocates the pool backing store from backing.
    PoolAllocator(Allocator& backing, usize capacity, usize blockSize,
                  usize blockAlignment = kDefaultAlignment) noexcept;

    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) noexcept override;
    void Deallocate(void* pointer) noexcept override;
    void Reset() noexcept override;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept override;
    [[nodiscard]] AllocatorStats Stats() const noexcept override;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] usize BlockSize() const noexcept;
    [[nodiscard]] usize BlockCount() const noexcept;
    [[nodiscard]] usize FreeCount() const noexcept;

private:
    void RebuildFreeList() noexcept;

    Byte* m_buffer = nullptr;
    usize m_capacity = 0;
    usize m_blockSize = 0;
    usize m_blockAlignment = kDefaultAlignment;
    usize m_blockCount = 0;
    usize m_freeCount = 0;
    usize m_peak = 0;
    usize m_allocations = 0;
    void* m_freeList = nullptr;
    Allocator* m_backing = nullptr;
    void* m_raw = nullptr;
    bool m_owns = false;
};

}  // namespace cyber::memory
