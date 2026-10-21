#pragma once

#include <cyber/core/assert.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/memory/types.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace cyber::memory
{

// Default hasher used by HashMap. StringView gets the deterministic project hash;
// other keys use std::hash.
template <typename T>
struct DefaultHash
{
    [[nodiscard]] usize operator()(const T& value) const noexcept
    {
        return static_cast<usize>(std::hash<T>{}(value));
    }
};

template <>
struct DefaultHash<StringView>
{
    [[nodiscard]] usize operator()(StringView value) const noexcept
    {
        return static_cast<usize>(value.Hash());
    }
};

// Open-addressing hash map with linear probing, tombstones, and power-of-two
// capacity. It grows when the occupied plus tombstone load approaches 70%.
//
// Keys and values must be default constructible and move assignable. Copy is
// deleted; move transfers the table. StringView keys are non-owning, so the
// backing text must outlive the map entry.
template <typename K, typename V, typename Hasher = DefaultHash<K>>
class HashMap
{
    static_assert(std::is_default_constructible_v<K>, "HashMap keys must be default constructible");
    static_assert(std::is_default_constructible_v<V>,
                  "HashMap values must be default constructible");

public:
    explicit HashMap(Allocator& allocator) noexcept : m_allocator(&allocator) {}

    HashMap(HashMap&& other) noexcept
        : m_allocator(other.m_allocator), m_slots(other.m_slots), m_capacity(other.m_capacity),
          m_size(other.m_size), m_tombstones(other.m_tombstones),
          m_hasher(std::move(other.m_hasher))
    {
        other.m_slots = nullptr;
        other.m_capacity = 0;
        other.m_size = 0;
        other.m_tombstones = 0;
    }

    HashMap& operator=(HashMap&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_allocator = other.m_allocator;
            m_slots = other.m_slots;
            m_capacity = other.m_capacity;
            m_size = other.m_size;
            m_tombstones = other.m_tombstones;
            m_hasher = std::move(other.m_hasher);
            other.m_slots = nullptr;
            other.m_capacity = 0;
            other.m_size = 0;
            other.m_tombstones = 0;
        }
        return *this;
    }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    ~HashMap()
    {
        Release();
    }

    [[nodiscard]] bool TryReserve(usize expected) noexcept
    {
        if (expected == 0)
        {
            return true;
        }
        if (expected > kInvalidIndex / 3)
        {
            return false;
        }

        const usize required = expected + expected / 2 + 1;
        usize capacity = 8;
        while (capacity < required)
        {
            if (capacity > kInvalidIndex / 2)
            {
                return false;
            }
            capacity *= 2;
        }
        if (capacity <= m_capacity)
        {
            return true;
        }
        return Rehash(capacity);
    }

    [[nodiscard]] bool TryInsert(const K& key, const V& value, bool* inserted = nullptr) noexcept
    {
        if (!EnsureInsertCapacity())
        {
            return false;
        }

        const usize hash = HashKey(key);
        usize index = FindInsertIndex(key, hash);
        if (index == kInvalidIndex)
        {
            return false;
        }
        if (m_slots[index].state == kOccupied)
        {
            m_slots[index].value = value;
            if (inserted != nullptr)
            {
                *inserted = false;
            }
            return true;
        }

        if (m_slots[index].state == kTombstone)
        {
            CG_ASSERT(m_tombstones > 0);
            --m_tombstones;
        }
        m_slots[index].key = key;
        m_slots[index].value = value;
        m_slots[index].state = kOccupied;
        ++m_size;
        if (inserted != nullptr)
        {
            *inserted = true;
        }
        return true;
    }

    [[nodiscard]] bool TryInsert(K&& key, V&& value, bool* inserted = nullptr) noexcept
    {
        if (!EnsureInsertCapacity())
        {
            return false;
        }

        const usize hash = HashKey(key);
        usize index = FindInsertIndex(key, hash);
        if (index == kInvalidIndex)
        {
            return false;
        }
        if (m_slots[index].state == kOccupied)
        {
            m_slots[index].value = std::move(value);
            if (inserted != nullptr)
            {
                *inserted = false;
            }
            return true;
        }

        if (m_slots[index].state == kTombstone)
        {
            CG_ASSERT(m_tombstones > 0);
            --m_tombstones;
        }
        m_slots[index].key = std::move(key);
        m_slots[index].value = std::move(value);
        m_slots[index].state = kOccupied;
        ++m_size;
        if (inserted != nullptr)
        {
            *inserted = true;
        }
        return true;
    }

    [[nodiscard]] V* Find(const K& key) noexcept
    {
        const usize index = FindIndex(key);
        return index == kInvalidIndex ? nullptr : &m_slots[index].value;
    }

    [[nodiscard]] const V* Find(const K& key) const noexcept
    {
        const usize index = FindIndex(key);
        return index == kInvalidIndex ? nullptr : &m_slots[index].value;
    }

    [[nodiscard]] bool Contains(const K& key) const noexcept
    {
        return FindIndex(key) != kInvalidIndex;
    }

    [[nodiscard]] bool Remove(const K& key) noexcept
    {
        const usize index = FindIndex(key);
        if (index == kInvalidIndex)
        {
            return false;
        }
        m_slots[index].state = kTombstone;
        --m_size;
        ++m_tombstones;
        return true;
    }

    void Clear() noexcept
    {
        for (usize index = 0; index < m_capacity; ++index)
        {
            std::destroy_at(m_slots + index);
            std::construct_at(m_slots + index);
        }
        m_size = 0;
        m_tombstones = 0;
    }

    [[nodiscard]] usize Size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] usize Capacity() const noexcept
    {
        return m_capacity;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return m_size == 0;
    }

    template <typename Fn>
    void ForEach(Fn&& function) noexcept
    {
        for (usize index = 0; index < m_capacity; ++index)
        {
            if (m_slots[index].state == kOccupied)
            {
                function(static_cast<const K&>(m_slots[index].key), m_slots[index].value);
            }
        }
    }

    template <typename Fn>
    void ForEach(Fn&& function) const noexcept
    {
        for (usize index = 0; index < m_capacity; ++index)
        {
            if (m_slots[index].state == kOccupied)
            {
                function(static_cast<const K&>(m_slots[index].key), m_slots[index].value);
            }
        }
    }

private:
    static constexpr u8 kEmpty = 0;
    static constexpr u8 kOccupied = 1;
    static constexpr u8 kTombstone = 2;

    struct Slot
    {
        K key{};
        V value{};
        u8 state = kEmpty;
    };

    [[nodiscard]] usize HashKey(const K& key) const noexcept
    {
        return m_hasher(key);
    }

    [[nodiscard]] usize FindIndex(const K& key) const noexcept
    {
        if (m_capacity == 0)
        {
            return kInvalidIndex;
        }
        const usize mask = m_capacity - 1;
        usize index = HashKey(key) & mask;
        for (usize probe = 0; probe < m_capacity; ++probe)
        {
            const Slot& slot = m_slots[index];
            if (slot.state == kEmpty)
            {
                return kInvalidIndex;
            }
            if (slot.state == kOccupied && slot.key == key)
            {
                return index;
            }
            index = (index + 1) & mask;
        }
        return kInvalidIndex;
    }

    [[nodiscard]] usize FindInsertIndex(const K& key, usize hash) const noexcept
    {
        const usize mask = m_capacity - 1;
        usize index = hash & mask;
        usize firstTombstone = kInvalidIndex;
        for (usize probe = 0; probe < m_capacity; ++probe)
        {
            const Slot& slot = m_slots[index];
            if (slot.state == kEmpty)
            {
                return firstTombstone != kInvalidIndex ? firstTombstone : index;
            }
            if (slot.state == kTombstone && firstTombstone == kInvalidIndex)
            {
                firstTombstone = index;
            }
            else if (slot.state == kOccupied && slot.key == key)
            {
                return index;
            }
            index = (index + 1) & mask;
        }
        return firstTombstone;
    }

    [[nodiscard]] bool EnsureInsertCapacity() noexcept
    {
        if (m_size == kInvalidIndex)
        {
            return false;
        }

        const usize limit = (m_capacity * 7) / 10;
        const bool needsGrowth = m_capacity == 0 || m_size + 1 > limit;
        const bool needsCleanup = m_capacity != 0 && m_tombstones > m_capacity / 4;
        if (!needsGrowth && !needsCleanup)
        {
            return true;
        }

        usize newCapacity = m_capacity;
        if (needsGrowth)
        {
            if (newCapacity == 0)
            {
                newCapacity = 8;
            }
            else
            {
                if (newCapacity > kInvalidIndex / 2)
                {
                    return false;
                }
                newCapacity *= 2;
            }
        }
        return Rehash(newCapacity);
    }

    [[nodiscard]] bool Rehash(usize newCapacity) noexcept
    {
        if (newCapacity < 8 || !IsPowerOfTwo(newCapacity) ||
            newCapacity > kInvalidIndex / sizeof(Slot))
        {
            return false;
        }

        Slot* newSlots =
            static_cast<Slot*>(m_allocator->Allocate(newCapacity * sizeof(Slot), alignof(Slot)));
        if (newSlots == nullptr)
        {
            return false;
        }

        for (usize index = 0; index < newCapacity; ++index)
        {
            std::construct_at(newSlots + index);
        }

        const usize mask = newCapacity - 1;
        for (usize oldIndex = 0; oldIndex < m_capacity; ++oldIndex)
        {
            Slot& oldSlot = m_slots[oldIndex];
            if (oldSlot.state != kOccupied)
            {
                continue;
            }
            usize index = HashKey(oldSlot.key) & mask;
            while (newSlots[index].state == kOccupied)
            {
                index = (index + 1) & mask;
            }
            newSlots[index].key = std::move(oldSlot.key);
            newSlots[index].value = std::move(oldSlot.value);
            newSlots[index].state = kOccupied;
        }

        for (usize index = 0; index < m_capacity; ++index)
        {
            std::destroy_at(m_slots + index);
        }
        if (m_slots != nullptr)
        {
            m_allocator->Deallocate(m_slots);
        }

        m_slots = newSlots;
        m_capacity = newCapacity;
        m_tombstones = 0;
        return true;
    }

    void Release() noexcept
    {
        for (usize index = 0; index < m_capacity; ++index)
        {
            std::destroy_at(m_slots + index);
        }
        if (m_slots != nullptr)
        {
            m_allocator->Deallocate(m_slots);
        }
        m_slots = nullptr;
        m_capacity = 0;
        m_size = 0;
        m_tombstones = 0;
    }

    Allocator* m_allocator = nullptr;
    Slot* m_slots = nullptr;
    usize m_capacity = 0;
    usize m_size = 0;
    usize m_tombstones = 0;
    Hasher m_hasher{};
};

}  // namespace cyber::memory
