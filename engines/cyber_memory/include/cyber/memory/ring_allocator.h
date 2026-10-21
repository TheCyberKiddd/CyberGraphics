#pragma once

#include <cyber/memory/allocator.h>

namespace cyber::memory
{

// Bump allocator with wraparound. When a request does not fit at the current
// head, the head wraps to zero and the oldest bytes may be reused. This is a
// transient allocator: pointers from before a wrap are invalid. Reset starts a
// new pass. Allocations larger than Capacity fail.
class RingAllocator final : public Allocator
{
public:
    // Uses buffer[0, capacity). The buffer must stay alive for the lifetime of
    // the allocator.
    explicit RingAllocator(void* buffer, usize capacity) noexcept;

    // Allocates the ring storage from backing.
    explicit RingAllocator(Allocator& backing, usize capacity) noexcept;

    ~RingAllocator() override;

    RingAllocator(const RingAllocator&) = delete;
    RingAllocator& operator=(const RingAllocator&) = delete;

    [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) noexcept override;
    void Deallocate(void* pointer) noexcept override;
    void Reset() noexcept override;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept override;
    [[nodiscard]] AllocatorStats Stats() const noexcept override;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] usize Capacity() const noexcept;
    [[nodiscard]] usize Used() const noexcept;
    [[nodiscard]] bool HasWrapped() const noexcept;

private:
    Byte* m_buffer = nullptr;
    usize m_capacity = 0;
    usize m_head = 0;
    usize m_used = 0;
    usize m_peak = 0;
    usize m_allocations = 0;
    bool m_wrapped = false;
    Allocator* m_backing = nullptr;
    void* m_raw = nullptr;
    bool m_owns = false;
};

}  // namespace cyber::memory
