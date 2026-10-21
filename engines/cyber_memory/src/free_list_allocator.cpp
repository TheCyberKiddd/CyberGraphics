#include <cyber/memory/alignment.h>
#include <cyber/memory/free_list_allocator.h>

#include <memory>
#include <new>

namespace cyber::memory
{

namespace
{

constexpr u32 kFreeListMagic = 0x4347464CU;  // "CGFL"
constexpr u32 kFreeFlag = 1U;

}  // namespace

FreeListAllocator::FreeListAllocator(void* buffer, usize capacity) noexcept
{
    if (buffer == nullptr || capacity < kHeaderSize)
    {
        return;
    }

    Byte* raw = static_cast<Byte*>(buffer);
    Byte* aligned = static_cast<Byte*>(AlignPointer(raw, kHeaderSize));
    const usize lost = static_cast<usize>(aligned - raw);
    if (lost >= capacity)
    {
        return;
    }

    m_buffer = aligned;
    m_capacity = capacity - lost;
    Initialize();
}

FreeListAllocator::FreeListAllocator(Allocator& backing, usize capacity) noexcept
{
    if (capacity == 0 || capacity > kInvalidIndex - kHeaderSize)
    {
        return;
    }

    void* raw = backing.Allocate(capacity + kHeaderSize, kHeaderSize);
    if (raw == nullptr)
    {
        return;
    }

    m_raw = raw;
    m_backing = &backing;
    m_owns = true;
    m_buffer = static_cast<Byte*>(AlignPointer(raw, kHeaderSize));
    m_capacity = capacity;
    Initialize();
}

FreeListAllocator::~FreeListAllocator()
{
    if (m_owns && m_backing != nullptr && m_raw != nullptr)
    {
        m_backing->Deallocate(m_raw);
    }
}

void FreeListAllocator::Initialize() noexcept
{
    if (m_buffer == nullptr || m_capacity < kHeaderSize)
    {
        m_buffer = nullptr;
        m_capacity = 0;
        m_freeHead = nullptr;
        return;
    }

    BlockHeader* first = std::construct_at(reinterpret_cast<BlockHeader*>(m_buffer));
    first->size = m_capacity;
    first->prevSize = 0;
    first->nextFree = nullptr;
    first->prevFree = nullptr;
    first->requested = 0;
    first->magic = kFreeListMagic;
    first->flags = kFreeFlag;
    m_freeHead = first;
}

void FreeListAllocator::Unlink(BlockHeader* block) noexcept
{
    if (block->prevFree != nullptr)
    {
        block->prevFree->nextFree = block->nextFree;
    }
    else
    {
        m_freeHead = block->nextFree;
    }

    if (block->nextFree != nullptr)
    {
        block->nextFree->prevFree = block->prevFree;
    }
    block->nextFree = nullptr;
    block->prevFree = nullptr;
}

void FreeListAllocator::PushFront(BlockHeader* block) noexcept
{
    block->flags |= kFreeFlag;
    block->nextFree = m_freeHead;
    block->prevFree = nullptr;
    if (m_freeHead != nullptr)
    {
        m_freeHead->prevFree = block;
    }
    m_freeHead = block;
}

bool FreeListAllocator::IsFree(const BlockHeader* block) const noexcept
{
    return (block->flags & kFreeFlag) != 0;
}

void FreeListAllocator::CoalesceWithNext(BlockHeader* block) noexcept
{
    Byte* nextAddress = reinterpret_cast<Byte*>(block) + block->size;
    Byte* end = m_buffer + m_capacity;
    if (nextAddress >= end)
    {
        return;
    }

    BlockHeader* next = reinterpret_cast<BlockHeader*>(nextAddress);
    if (next->magic != kFreeListMagic || !IsFree(next))
    {
        return;
    }

    const usize nextSize = next->size;
    Unlink(next);
    block->size += nextSize;
    std::destroy_at(next);

    Byte* afterAddress = reinterpret_cast<Byte*>(block) + block->size;
    if (afterAddress < end)
    {
        reinterpret_cast<BlockHeader*>(afterAddress)->prevSize = block->size;
    }
}

void* FreeListAllocator::Allocate(usize size, usize alignment) noexcept
{
    if (size == 0 || !IsPowerOfTwo(alignment) || alignment > kMaxAlignment)
    {
        return nullptr;
    }
    if (size > kInvalidIndex - kHeaderSize)
    {
        return nullptr;
    }

    const usize rawNeeded = kHeaderSize + size;
    const usize needed = AlignUp(rawNeeded, kHeaderSize);
    if (needed < rawNeeded)
    {
        return nullptr;
    }

    for (BlockHeader* block = m_freeHead; block != nullptr; block = block->nextFree)
    {
        if (block->size < needed)
        {
            continue;
        }

        Unlink(block);
        const usize remaining = block->size - needed;
        if (remaining >= kHeaderSize)
        {
            Byte* splitAddress = reinterpret_cast<Byte*>(block) + needed;
            BlockHeader* split = std::construct_at(reinterpret_cast<BlockHeader*>(splitAddress));
            split->size = remaining;
            split->prevSize = needed;
            split->nextFree = nullptr;
            split->prevFree = nullptr;
            split->requested = 0;
            split->magic = kFreeListMagic;
            split->flags = kFreeFlag;

            Byte* afterAddress = splitAddress + split->size;
            if (afterAddress < m_buffer + m_capacity)
            {
                reinterpret_cast<BlockHeader*>(afterAddress)->prevSize = split->size;
            }

            block->size = needed;
            PushFront(split);
        }

        block->flags &= ~kFreeFlag;
        block->requested = size;
        m_used += size;
        if (m_used > m_peak)
        {
            m_peak = m_used;
        }
        ++m_allocations;
        return reinterpret_cast<Byte*>(block) + kHeaderSize;
    }

    return nullptr;
}

void FreeListAllocator::Deallocate(void* pointer) noexcept
{
    if (pointer == nullptr || m_buffer == nullptr)
    {
        return;
    }

    Byte* payload = static_cast<Byte*>(pointer);
    if (payload < m_buffer + kHeaderSize || payload >= m_buffer + m_capacity)
    {
        return;
    }

    BlockHeader* block = reinterpret_cast<BlockHeader*>(payload - kHeaderSize);
    if (block->magic != kFreeListMagic || IsFree(block))
    {
        return;
    }

    if (block->requested <= m_used)
    {
        m_used -= block->requested;
    }
    else
    {
        m_used = 0;
    }
    block->requested = 0;
    block->flags |= kFreeFlag;

    CoalesceWithNext(block);

    if (block->prevSize != 0)
    {
        Byte* previousAddress = reinterpret_cast<Byte*>(block) - block->prevSize;
        if (previousAddress >= m_buffer)
        {
            BlockHeader* previous = reinterpret_cast<BlockHeader*>(previousAddress);
            if (previous->magic == kFreeListMagic && IsFree(previous))
            {
                const usize blockSize = block->size;
                Unlink(previous);
                previous->size += blockSize;
                std::destroy_at(block);
                block = previous;

                Byte* afterAddress = reinterpret_cast<Byte*>(block) + block->size;
                if (afterAddress < m_buffer + m_capacity)
                {
                    reinterpret_cast<BlockHeader*>(afterAddress)->prevSize = block->size;
                }
            }
        }
    }

    PushFront(block);
}

void FreeListAllocator::Reset() noexcept
{
    m_used = 0;
    m_peak = 0;
    m_allocations = 0;
    Initialize();
}

bool FreeListAllocator::Owns(const void* pointer) const noexcept
{
    if (pointer == nullptr || m_buffer == nullptr)
    {
        return false;
    }
    const Byte* address = static_cast<const Byte*>(pointer);
    return address >= m_buffer && address < m_buffer + m_capacity;
}

AllocatorStats FreeListAllocator::Stats() const noexcept
{
    return AllocatorStats{m_capacity, m_used, m_peak, m_allocations};
}

bool FreeListAllocator::IsValid() const noexcept
{
    if (m_buffer == nullptr || m_capacity < kHeaderSize)
    {
        return m_capacity == 0;
    }

    usize offset = 0;
    usize previousSize = 0;
    while (offset < m_capacity)
    {
        BlockHeader* block = reinterpret_cast<BlockHeader*>(m_buffer + offset);
        if (block->magic != kFreeListMagic)
        {
            return false;
        }
        if (block->size < kHeaderSize || !IsAligned(block->size, kHeaderSize))
        {
            return false;
        }
        if (block->prevSize != previousSize)
        {
            return false;
        }
        if (block->size > m_capacity - offset)
        {
            return false;
        }
        previousSize = block->size;
        offset += block->size;
    }

    usize freeCount = 0;
    for (BlockHeader* block = m_freeHead; block != nullptr; block = block->nextFree)
    {
        if (block->magic != kFreeListMagic || !IsFree(block))
        {
            return false;
        }
        if (block->nextFree != nullptr && block->nextFree->prevFree != block)
        {
            return false;
        }
        ++freeCount;
        if (freeCount > m_capacity / kHeaderSize)
        {
            return false;
        }
    }
    return true;
}

usize FreeListAllocator::Capacity() const noexcept
{
    return m_capacity;
}

usize FreeListAllocator::Used() const noexcept
{
    return m_used;
}

usize FreeListAllocator::FreeBlockCount() const noexcept
{
    usize count = 0;
    for (BlockHeader* block = m_freeHead; block != nullptr; block = block->nextFree)
    {
        ++count;
    }
    return count;
}

usize FreeListAllocator::LargestFreeBlock() const noexcept
{
    usize largest = 0;
    for (BlockHeader* block = m_freeHead; block != nullptr; block = block->nextFree)
    {
        if (block->size > largest)
        {
            largest = block->size;
        }
    }
    return largest;
}

}  // namespace cyber::memory
