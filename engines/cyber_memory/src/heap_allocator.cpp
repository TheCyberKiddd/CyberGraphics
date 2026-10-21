#include <cyber/memory/heap_allocator.h>

#include <cstdlib>
#include <memory>
#include <new>

namespace cyber::memory
{

namespace
{

struct Header
{
    void* raw = nullptr;
    usize size = 0;
};

}  // namespace

void* HeapAllocator::Allocate(usize size, usize alignment) noexcept
{
    if (size == 0 || !IsPowerOfTwo(alignment))
    {
        return nullptr;
    }

    usize effective = alignment;
    if (effective < alignof(Header))
    {
        effective = alignof(Header);
    }
    if (size > kInvalidIndex - effective - sizeof(Header))
    {
        return nullptr;
    }

    void* raw = std::malloc(size + effective + sizeof(Header));
    if (raw == nullptr)
    {
        return nullptr;
    }

    Byte* base = static_cast<Byte*>(raw) + sizeof(Header);
    Byte* aligned = static_cast<Byte*>(AlignPointer(base, effective));
    Header* header = reinterpret_cast<Header*>(aligned - sizeof(Header));
    std::construct_at(header);
    header->raw = raw;
    header->size = size;

    m_stats.used += size;
    if (m_stats.used > m_stats.peak)
    {
        m_stats.peak = m_stats.used;
    }
    ++m_stats.allocations;
    return aligned;
}

void HeapAllocator::Deallocate(void* pointer) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }

    Header* header = reinterpret_cast<Header*>(static_cast<Byte*>(pointer) - sizeof(Header));
    void* raw = header->raw;
    const usize size = header->size;
    std::destroy_at(header);
    if (size <= m_stats.used)
    {
        m_stats.used -= size;
    }
    else
    {
        m_stats.used = 0;
    }
    std::free(raw);
}

void HeapAllocator::Reset() noexcept
{
    // The root allocator cannot discover outstanding blocks without a registry.
    // Callers must release them individually.
}

bool HeapAllocator::Owns(const void* pointer) const noexcept
{
    // A heap pointer may have come from any allocator in the process. The root
    // allocator cannot prove provenance, so it only rejects null.
    return pointer != nullptr;
}

AllocatorStats HeapAllocator::Stats() const noexcept
{
    return m_stats;
}

}  // namespace cyber::memory
