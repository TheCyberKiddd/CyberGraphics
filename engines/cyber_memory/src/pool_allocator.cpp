#include <cyber/memory/alignment.h>
#include <cyber/memory/pool_allocator.h>

namespace cyber::memory
{

namespace
{

[[nodiscard]] bool NormalizeAlignment(usize alignment, usize& normalized) noexcept
{
    if (!IsPowerOfTwo(alignment))
    {
        return false;
    }
    normalized = alignment < alignof(void*) ? alignof(void*) : alignment;
    return true;
}

[[nodiscard]] bool DoubleFree(void* freeList, const void* block) noexcept
{
    for (void* current = freeList; current != nullptr; current = *static_cast<void**>(current))
    {
        if (current == block)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

PoolAllocator::PoolAllocator(void* buffer, usize capacity, usize blockSize,
                             usize blockAlignment) noexcept
{
    usize normalized = 0;
    if (!NormalizeAlignment(blockAlignment, normalized))
    {
        return;
    }
    m_blockAlignment = normalized;

    if (buffer == nullptr || capacity == 0)
    {
        return;
    }

    Byte* raw = static_cast<Byte*>(buffer);
    Byte* aligned = static_cast<Byte*>(AlignPointer(raw, m_blockAlignment));
    const usize lost = static_cast<usize>(aligned - raw);
    if (lost >= capacity)
    {
        return;
    }

    m_buffer = aligned;
    m_capacity = capacity - lost;

    usize actualBlock = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;
    if (actualBlock > kInvalidIndex - (m_blockAlignment - 1))
    {
        m_capacity = 0;
        return;
    }
    m_blockSize = AlignUp(actualBlock, m_blockAlignment);
    if (m_blockSize == 0 || m_blockSize > m_capacity)
    {
        m_capacity = 0;
        return;
    }
    m_blockCount = m_capacity / m_blockSize;
    if (m_blockCount == 0)
    {
        m_capacity = 0;
        return;
    }
    RebuildFreeList();
}

PoolAllocator::PoolAllocator(Allocator& backing, usize capacity, usize blockSize,
                             usize blockAlignment) noexcept
{
    usize normalized = 0;
    if (!NormalizeAlignment(blockAlignment, normalized))
    {
        return;
    }
    m_blockAlignment = normalized;

    if (capacity == 0 || capacity > kInvalidIndex - m_blockAlignment)
    {
        return;
    }

    void* raw = backing.Allocate(capacity + m_blockAlignment, m_blockAlignment);
    if (raw == nullptr)
    {
        return;
    }

    m_backing = &backing;
    m_raw = raw;
    m_owns = true;
    m_buffer = static_cast<Byte*>(AlignPointer(raw, m_blockAlignment));
    m_capacity = capacity;

    usize actualBlock = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;
    if (actualBlock > kInvalidIndex - (m_blockAlignment - 1))
    {
        m_capacity = 0;
        return;
    }
    m_blockSize = AlignUp(actualBlock, m_blockAlignment);
    if (m_blockSize == 0 || m_blockSize > m_capacity)
    {
        m_capacity = 0;
        return;
    }
    m_blockCount = m_capacity / m_blockSize;
    if (m_blockCount == 0)
    {
        m_capacity = 0;
        return;
    }
    RebuildFreeList();
}

PoolAllocator::~PoolAllocator()
{
    if (m_owns && m_backing != nullptr && m_raw != nullptr)
    {
        m_backing->Deallocate(m_raw);
    }
}

void* PoolAllocator::Allocate(usize size, usize alignment) noexcept
{
    if (size == 0 || size > m_blockSize || !IsPowerOfTwo(alignment) ||
        alignment > m_blockAlignment || m_freeList == nullptr)
    {
        return nullptr;
    }

    void* result = m_freeList;
    m_freeList = *static_cast<void**>(result);
    --m_freeCount;
    ++m_allocations;

    const usize used = (m_blockCount - m_freeCount) * m_blockSize;
    if (used > m_peak)
    {
        m_peak = used;
    }
    return result;
}

void PoolAllocator::Deallocate(void* pointer) noexcept
{
    if (pointer == nullptr || m_buffer == nullptr)
    {
        return;
    }

    Byte* block = static_cast<Byte*>(pointer);
    if (block < m_buffer || block >= m_buffer + m_blockCount * m_blockSize)
    {
        return;
    }
    const usize offset = static_cast<usize>(block - m_buffer);
    if (!IsAligned(offset, m_blockSize))
    {
        return;
    }
    if (DoubleFree(m_freeList, block))
    {
        return;
    }

    *reinterpret_cast<void**>(block) = m_freeList;
    m_freeList = block;
    ++m_freeCount;
}

void PoolAllocator::Reset() noexcept
{
    m_peak = 0;
    m_allocations = 0;
    RebuildFreeList();
}

bool PoolAllocator::Owns(const void* pointer) const noexcept
{
    if (pointer == nullptr || m_buffer == nullptr)
    {
        return false;
    }
    const Byte* address = static_cast<const Byte*>(pointer);
    return address >= m_buffer && address < m_buffer + m_blockCount * m_blockSize;
}

AllocatorStats PoolAllocator::Stats() const noexcept
{
    const usize capacity = m_blockCount * m_blockSize;
    const usize used = (m_blockCount - m_freeCount) * m_blockSize;
    return AllocatorStats{capacity, used, m_peak, m_allocations};
}

bool PoolAllocator::IsValid() const noexcept
{
    return m_buffer != nullptr && m_blockSize > 0 && m_blockCount > 0;
}

usize PoolAllocator::BlockSize() const noexcept
{
    return m_blockSize;
}

usize PoolAllocator::BlockCount() const noexcept
{
    return m_blockCount;
}

usize PoolAllocator::FreeCount() const noexcept
{
    return m_freeCount;
}

void PoolAllocator::RebuildFreeList() noexcept
{
    m_freeList = nullptr;
    m_freeCount = 0;
    for (usize index = 0; index < m_blockCount; ++index)
    {
        Byte* block = m_buffer + index * m_blockSize;
        *reinterpret_cast<void**>(block) = m_freeList;
        m_freeList = block;
        ++m_freeCount;
    }
}

}  // namespace cyber::memory
