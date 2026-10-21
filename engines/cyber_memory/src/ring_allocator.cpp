#include <cyber/memory/alignment.h>
#include <cyber/memory/ring_allocator.h>

namespace cyber::memory
{

RingAllocator::RingAllocator(void* buffer, usize capacity) noexcept
    : m_buffer(static_cast<Byte*>(buffer)), m_capacity(capacity)
{
}

RingAllocator::RingAllocator(Allocator& backing, usize capacity) noexcept : m_backing(&backing)
{
    if (capacity == 0)
    {
        return;
    }

    void* raw = backing.Allocate(capacity, kDefaultAlignment);
    if (raw == nullptr)
    {
        return;
    }

    m_raw = raw;
    m_buffer = static_cast<Byte*>(raw);
    m_capacity = capacity;
    m_owns = true;
}

RingAllocator::~RingAllocator()
{
    if (m_owns && m_backing != nullptr && m_raw != nullptr)
    {
        m_backing->Deallocate(m_raw);
    }
}

void* RingAllocator::Allocate(usize size, usize alignment) noexcept
{
    if (size == 0 || size > m_capacity || !IsPowerOfTwo(alignment) || m_buffer == nullptr)
    {
        return nullptr;
    }
    if (m_head > kInvalidIndex - (alignment - 1))
    {
        return nullptr;
    }

    usize aligned = AlignUp(m_head, alignment);
    if (aligned > m_capacity || size > m_capacity - aligned)
    {
        aligned = 0;
        m_wrapped = true;
    }
    if (size > m_capacity - aligned)
    {
        return nullptr;
    }

    void* result = m_buffer + aligned;
    m_head = aligned + size;
    m_used += size;
    if (m_used > m_capacity)
    {
        m_used = m_capacity;
    }
    if (m_used > m_peak)
    {
        m_peak = m_used;
    }
    ++m_allocations;
    return result;
}

void RingAllocator::Deallocate(void* pointer) noexcept
{
    (void)pointer;
}

void RingAllocator::Reset() noexcept
{
    m_head = 0;
    m_used = 0;
    m_peak = 0;
    m_allocations = 0;
    m_wrapped = false;
}

bool RingAllocator::Owns(const void* pointer) const noexcept
{
    if (pointer == nullptr || m_buffer == nullptr)
    {
        return false;
    }
    const Byte* address = static_cast<const Byte*>(pointer);
    return address >= m_buffer && address < m_buffer + m_capacity;
}

AllocatorStats RingAllocator::Stats() const noexcept
{
    return AllocatorStats{m_capacity, m_used, m_peak, m_allocations};
}

bool RingAllocator::IsValid() const noexcept
{
    return m_buffer != nullptr && m_capacity > 0;
}

usize RingAllocator::Capacity() const noexcept
{
    return m_capacity;
}

usize RingAllocator::Used() const noexcept
{
    return m_used;
}

bool RingAllocator::HasWrapped() const noexcept
{
    return m_wrapped;
}

}  // namespace cyber::memory
