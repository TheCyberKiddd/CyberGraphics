#include <cyber/memory/linear_allocator.h>

namespace cyber::memory
{

LinearAllocator::LinearAllocator(void* buffer, usize capacity) noexcept
    : m_buffer(static_cast<Byte*>(buffer)), m_capacity(capacity)
{
}

LinearAllocator::LinearAllocator(Allocator& backing, usize capacity) noexcept : m_backing(&backing)
{
    if (capacity == 0)
    {
        return;
    }

    void* memory = backing.Allocate(capacity, kDefaultAlignment);
    if (memory == nullptr)
    {
        return;
    }

    m_buffer = static_cast<Byte*>(memory);
    m_capacity = capacity;
    m_owns = true;
}

LinearAllocator::~LinearAllocator()
{
    if (m_owns && m_backing != nullptr && m_buffer != nullptr)
    {
        m_backing->Deallocate(m_buffer);
    }
}

void* LinearAllocator::Allocate(usize size, usize alignment) noexcept
{
    if (size == 0 || !IsPowerOfTwo(alignment) || m_buffer == nullptr)
    {
        return nullptr;
    }
    if (m_offset > kInvalidIndex - (alignment - 1))
    {
        return nullptr;
    }

    const usize aligned = AlignUp(m_offset, alignment);
    if (aligned > m_capacity || size > m_capacity - aligned)
    {
        return nullptr;
    }

    void* result = m_buffer + aligned;
    m_offset = aligned + size;
    if (m_offset > m_peak)
    {
        m_peak = m_offset;
    }
    ++m_allocations;
    return result;
}

void LinearAllocator::Deallocate(void* pointer) noexcept
{
    (void)pointer;
}

void LinearAllocator::Reset() noexcept
{
    m_offset = 0;
    m_peak = 0;
    m_allocations = 0;
}

bool LinearAllocator::Owns(const void* pointer) const noexcept
{
    if (pointer == nullptr || m_buffer == nullptr)
    {
        return false;
    }
    const Byte* address = static_cast<const Byte*>(pointer);
    return address >= m_buffer && address < m_buffer + m_capacity;
}

AllocatorStats LinearAllocator::Stats() const noexcept
{
    return AllocatorStats{m_capacity, m_offset, m_peak, m_allocations};
}

bool LinearAllocator::IsValid() const noexcept
{
    return m_buffer != nullptr && m_capacity > 0;
}

usize LinearAllocator::Capacity() const noexcept
{
    return m_capacity;
}

usize LinearAllocator::Used() const noexcept
{
    return m_offset;
}

usize LinearAllocator::Remaining() const noexcept
{
    return m_capacity - m_offset;
}

bool LinearAllocator::TryResetTo(usize offset) noexcept
{
    if (offset > m_offset)
    {
        return false;
    }
    m_offset = offset;
    return true;
}

}  // namespace cyber::memory
