#pragma once

#include <cyber/memory/allocator.h>

namespace cyber::memory
{

// Bump allocator over a caller-provided or backing-allocated buffer.
// Deallocate is a no-op; Reset reclaims everything. Alignment padding is
// included in Used. There is no per-allocation metadata.
class LinearAllocator : public Allocator
{
public:
    // Uses buffer[0, capacity). The buffer must stay alive for the lifetime of
    // the allocator.
    explicit LinearAllocator(void* buffer, usize capacity) noexcept;

    // Allocates capacity bytes from backing and owns that allocation.
    explicit LinearAllocator(Allocator& backing, usize capacity) noexcept;

    ~LinearAllocator() override;

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) noexcept override;
    void Deallocate(void* pointer) noexcept override;
    void Reset() noexcept override;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept override;
    [[nodiscard]] AllocatorStats Stats() const noexcept override;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] usize Capacity() const noexcept;
    [[nodiscard]] usize Used() const noexcept;
    [[nodiscard]] usize Remaining() const noexcept;

    // Rewinds to a previous offset. Returns false if offset is beyond the
    // current allocation point.
    [[nodiscard]] bool TryResetTo(usize offset) noexcept;

protected:
    Byte* m_buffer = nullptr;
    usize m_capacity = 0;
    usize m_offset = 0;
    usize m_peak = 0;
    usize m_allocations = 0;
    Allocator* m_backing = nullptr;
    bool m_owns = false;
};

}  // namespace cyber::memory
