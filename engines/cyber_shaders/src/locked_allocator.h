#pragma once

#include <cyber/memory/allocator.h>

#include <mutex>

namespace cyber::shaders::detail
{

// Serializes every call into an upstream allocator so job workers and the
// owning thread can share it. The upstream allocator must outlive the adapter.
class LockedAllocator final : public memory::Allocator
{
public:
    explicit LockedAllocator(memory::Allocator& upstream) noexcept : m_upstream(upstream) {}

    [[nodiscard]] void* Allocate(core::usize size, core::usize alignment) noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_upstream.Allocate(size, alignment);
    }

    void Deallocate(void* pointer) noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_upstream.Deallocate(pointer);
    }

    // Resetting a shared upstream would invalidate memory other owners hold.
    void Reset() noexcept override {}

    [[nodiscard]] bool Owns(const void* pointer) const noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_upstream.Owns(pointer);
    }

    [[nodiscard]] memory::AllocatorStats Stats() const noexcept override
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_upstream.Stats();
    }

private:
    memory::Allocator& m_upstream;
    mutable std::mutex m_mutex;
};

}  // namespace cyber::shaders::detail
