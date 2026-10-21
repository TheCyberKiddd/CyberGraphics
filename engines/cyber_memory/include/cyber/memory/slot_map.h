#pragma once

#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/handle.h>
#include <cyber/memory/types.h>

#include <type_traits>
#include <utility>

namespace cyber::memory
{

// Stable-handle container. Values live in slots and removed slots are recycled
// with a bumped generation. A handle from a previous lifetime never resolves.
// T must be default constructible and move assignable. Copy is deleted; move is
// supported.
template <typename T, typename Tag = T>
class SlotMap
{
    static_assert(std::is_default_constructible_v<T>,
                  "SlotMap values must be default constructible");

public:
    using HandleType = Handle<Tag>;

    explicit SlotMap(Allocator& allocator) noexcept : m_slots(allocator) {}

    SlotMap(SlotMap&&) noexcept = default;
    SlotMap& operator=(SlotMap&&) noexcept = default;

    SlotMap(const SlotMap&) = delete;
    SlotMap& operator=(const SlotMap&) = delete;

    [[nodiscard]] HandleType Insert(const T& value) noexcept
    {
        const u32 index = AllocateSlot();
        if (index == kInvalidHandleIndex)
        {
            return HandleType{};
        }
        m_slots[index].value = value;
        return HandleType{index, m_slots[index].generation};
    }

    [[nodiscard]] HandleType Insert(T&& value) noexcept
    {
        const u32 index = AllocateSlot();
        if (index == kInvalidHandleIndex)
        {
            return HandleType{};
        }
        m_slots[index].value = std::move(value);
        return HandleType{index, m_slots[index].generation};
    }

    [[nodiscard]] T* Get(HandleType handle) noexcept
    {
        if (!IsLive(handle))
        {
            return nullptr;
        }
        return &m_slots[handle.index].value;
    }

    [[nodiscard]] const T* Get(HandleType handle) const noexcept
    {
        if (!IsLive(handle))
        {
            return nullptr;
        }
        return &m_slots[handle.index].value;
    }

    [[nodiscard]] bool Contains(HandleType handle) const noexcept
    {
        return IsLive(handle);
    }

    [[nodiscard]] bool Remove(HandleType handle) noexcept
    {
        if (!IsLive(handle))
        {
            return false;
        }

        Slot& slot = m_slots[handle.index];
        slot.occupied = false;
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        slot.nextFree = m_freeHead;
        m_freeHead = handle.index;
        --m_size;
        return true;
    }

    void Clear() noexcept
    {
        for (usize index = 0; index < m_slots.Size(); ++index)
        {
            Slot& slot = m_slots[index];
            if (slot.occupied)
            {
                ++slot.generation;
                if (slot.generation == 0)
                {
                    slot.generation = 1;
                }
            }
            slot.occupied = false;
            slot.nextFree = static_cast<u32>(index + 1);
        }

        if (m_slots.Size() > 0)
        {
            m_slots[m_slots.Size() - 1].nextFree = kInvalidHandleIndex;
            m_freeHead = 0;
        }
        else
        {
            m_freeHead = kInvalidHandleIndex;
        }
        m_size = 0;
    }

    [[nodiscard]] bool TryReserve(usize capacity) noexcept
    {
        return m_slots.TryReserve(capacity);
    }

    [[nodiscard]] usize Size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] usize Capacity() const noexcept
    {
        return m_slots.Capacity();
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return m_size == 0;
    }

private:
    struct Slot
    {
        u32 generation = 1;
        u32 nextFree = kInvalidHandleIndex;
        bool occupied = false;
        T value{};
    };

    [[nodiscard]] bool IsLive(HandleType handle) const noexcept
    {
        if (handle.index == kInvalidHandleIndex || handle.index >= m_slots.Size())
        {
            return false;
        }
        const Slot& slot = m_slots[handle.index];
        return slot.occupied && slot.generation == handle.generation;
    }

    [[nodiscard]] u32 AllocateSlot() noexcept
    {
        if (m_freeHead != kInvalidHandleIndex)
        {
            const u32 index = m_freeHead;
            Slot& slot = m_slots[index];
            m_freeHead = slot.nextFree;
            slot.nextFree = kInvalidHandleIndex;
            slot.occupied = true;
            ++m_size;
            return index;
        }

        Slot* slot = m_slots.TryEmplaceBack();
        if (slot == nullptr)
        {
            return kInvalidHandleIndex;
        }
        slot->generation = 1;
        slot->nextFree = kInvalidHandleIndex;
        slot->occupied = true;
        ++m_size;
        return static_cast<u32>(m_slots.Size() - 1);
    }

    DynamicArray<Slot> m_slots;
    u32 m_freeHead = kInvalidHandleIndex;
    usize m_size = 0;
};

}  // namespace cyber::memory
