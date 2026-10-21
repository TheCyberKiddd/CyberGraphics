#include <cyber/memory/tracking_allocator.h>

#include <memory>
#include <new>

namespace cyber::memory
{

TrackingAllocator::TrackingAllocator(Allocator& upstream, const char* defaultTag) noexcept
    : m_upstream(&upstream), m_defaultTag(defaultTag == nullptr ? "untracked" : defaultTag)
{
}

TrackingAllocator::~TrackingAllocator()
{
    Reset();
}

void* TrackingAllocator::Allocate(usize size, usize alignment) noexcept
{
    return AllocateTagged(size, alignment, m_defaultTag);
}

void* TrackingAllocator::AllocateTagged(usize size, usize alignment, const char* tag) noexcept
{
    if (m_upstream == nullptr || size == 0 || !IsPowerOfTwo(alignment) ||
        alignment > kMaxTrackedAlignment)
    {
        return nullptr;
    }
    if (size > kInvalidIndex - sizeof(Header))
    {
        return nullptr;
    }

    void* base = m_upstream->Allocate(sizeof(Header) + size, kMaxTrackedAlignment);
    if (base == nullptr)
    {
        return nullptr;
    }

    Header* header = std::construct_at(reinterpret_cast<Header*>(base));
    header->size = size;
    header->tag = tag == nullptr ? "untagged" : tag;
    header->magic = kMagic;
    header->prev = nullptr;
    header->next = m_head;
    if (m_head != nullptr)
    {
        m_head->prev = header;
    }
    m_head = header;

    m_used += size;
    if (m_used > m_peak)
    {
        m_peak = m_used;
    }
    ++m_allocations;

    return static_cast<Byte*>(base) + sizeof(Header);
}

void TrackingAllocator::Deallocate(void* pointer) noexcept
{
    if (pointer == nullptr || m_upstream == nullptr)
    {
        return;
    }

    Header* header = reinterpret_cast<Header*>(static_cast<Byte*>(pointer) - sizeof(Header));
    if (header->magic != kMagic)
    {
        return;
    }

    Unlink(header);
    if (header->size <= m_used)
    {
        m_used -= header->size;
    }
    else
    {
        m_used = 0;
    }
    header->magic = 0;
    m_upstream->Deallocate(header);
}

void TrackingAllocator::Reset() noexcept
{
    Header* header = m_head;
    while (header != nullptr)
    {
        Header* next = header->next;
        header->magic = 0;
        if (m_upstream != nullptr)
        {
            m_upstream->Deallocate(header);
        }
        header = next;
    }
    m_head = nullptr;
    m_used = 0;
    m_peak = 0;
    m_allocations = 0;
}

bool TrackingAllocator::Owns(const void* pointer) const noexcept
{
    if (pointer == nullptr)
    {
        return false;
    }
    for (const Header* header = m_head; header != nullptr; header = header->next)
    {
        const void* payload = reinterpret_cast<const Byte*>(header) + sizeof(Header);
        if (payload == pointer)
        {
            return true;
        }
    }
    return false;
}

AllocatorStats TrackingAllocator::Stats() const noexcept
{
    const usize capacity = m_upstream == nullptr ? 0 : m_upstream->Stats().capacity;
    return AllocatorStats{capacity, m_used, m_peak, m_allocations};
}

usize TrackingAllocator::LeakCount() const noexcept
{
    usize count = 0;
    for (const Header* header = m_head; header != nullptr; header = header->next)
    {
        ++count;
    }
    return count;
}

usize TrackingAllocator::ReportLeaks(LeakCallback callback, void* context) const noexcept
{
    usize count = 0;
    for (const Header* header = m_head; header != nullptr; header = header->next)
    {
        if (callback != nullptr)
        {
            const LeakInfo info{reinterpret_cast<const Byte*>(header) + sizeof(Header),
                                header->size, header->tag};
            callback(context, info);
        }
        ++count;
    }
    return count;
}

Allocator& TrackingAllocator::Upstream() noexcept
{
    return *m_upstream;
}

const Allocator& TrackingAllocator::Upstream() const noexcept
{
    return *m_upstream;
}

void TrackingAllocator::SetDefaultTag(const char* tag) noexcept
{
    m_defaultTag = tag == nullptr ? "untracked" : tag;
}

const char* TrackingAllocator::DefaultTag() const noexcept
{
    return m_defaultTag;
}

void TrackingAllocator::Unlink(Header* header) noexcept
{
    if (header->prev != nullptr)
    {
        header->prev->next = header->next;
    }
    else
    {
        m_head = header->next;
    }

    if (header->next != nullptr)
    {
        header->next->prev = header->prev;
    }
    header->prev = nullptr;
    header->next = nullptr;
}

}  // namespace cyber::memory
