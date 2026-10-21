#pragma once

#include <cyber/memory/allocator.h>

namespace cyber::memory
{

// Root allocator backed by std::malloc. It stores a small header before the
// aligned payload so Deallocate does not need the original size. This allocator
// is intended for tools, diagnostics, and backing stores, not for hot paths.
//
// Reset() is a no-op: the heap allocator cannot discover blocks it handed out.
class HeapAllocator final : public Allocator
{
public:
    HeapAllocator() noexcept = default;
    ~HeapAllocator() override = default;

    [[nodiscard]] void* Allocate(usize size, usize alignment = kDefaultAlignment) noexcept override;
    void Deallocate(void* pointer) noexcept override;
    void Reset() noexcept override;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept override;
    [[nodiscard]] AllocatorStats Stats() const noexcept override;

private:
    AllocatorStats m_stats{};
};

}  // namespace cyber::memory
