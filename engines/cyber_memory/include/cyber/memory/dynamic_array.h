#pragma once

#include <cyber/core/assert.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/types.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace cyber::memory
{

// Contiguous dynamic array with explicit allocator ownership.
//
// Copy construction and copy assignment are deleted. Move construction and move
// assignment transfer the allocation and leave the source empty. Capacity grows
// by doubling with a minimum of 8. A failed growth returns false and leaves the
// old allocation and contents intact.
template <typename T>
class DynamicArray
{
public:
    using Iterator = T*;
    using ConstIterator = const T*;

    explicit DynamicArray(Allocator& allocator) noexcept : m_allocator(&allocator) {}

    DynamicArray(DynamicArray&& other) noexcept
        : m_allocator(other.m_allocator), m_data(other.m_data), m_size(other.m_size),
          m_capacity(other.m_capacity)
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    DynamicArray& operator=(DynamicArray&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_allocator = other.m_allocator;
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    DynamicArray(const DynamicArray&) = delete;
    DynamicArray& operator=(const DynamicArray&) = delete;

    ~DynamicArray()
    {
        Release();
    }

    [[nodiscard]] bool TryReserve(usize capacity) noexcept
    {
        if (capacity <= m_capacity)
        {
            return true;
        }
        if (capacity > kInvalidIndex / sizeof(T))
        {
            return false;
        }

        usize newCapacity = m_capacity == 0 ? 8 : m_capacity;
        while (newCapacity < capacity)
        {
            if (newCapacity > kInvalidIndex / 2)
            {
                newCapacity = capacity;
                break;
            }
            newCapacity *= 2;
        }
        if (newCapacity > kInvalidIndex / sizeof(T))
        {
            return false;
        }

        T* newData = static_cast<T*>(m_allocator->Allocate(newCapacity * sizeof(T), alignof(T)));
        if (newData == nullptr)
        {
            return false;
        }

        for (usize index = 0; index < m_size; ++index)
        {
            if constexpr (std::is_nothrow_move_constructible_v<T> ||
                          !std::is_copy_constructible_v<T>)
            {
                std::construct_at(newData + index, std::move(m_data[index]));
            }
            else
            {
                std::construct_at(newData + index, m_data[index]);
            }
        }

        for (usize index = 0; index < m_size; ++index)
        {
            std::destroy_at(m_data + index);
        }
        if (m_data != nullptr)
        {
            m_allocator->Deallocate(m_data);
        }

        m_data = newData;
        m_capacity = newCapacity;
        return true;
    }

    [[nodiscard]] bool TryResize(usize size) noexcept
    {
        if (size <= m_size)
        {
            for (usize index = size; index < m_size; ++index)
            {
                std::destroy_at(m_data + index);
            }
            m_size = size;
            return true;
        }

        if (!TryReserve(size))
        {
            return false;
        }

        for (usize index = m_size; index < size; ++index)
        {
            std::construct_at(m_data + index);
        }
        m_size = size;
        return true;
    }

    [[nodiscard]] bool TryPushBack(const T& value) noexcept
    {
        if (m_size == kInvalidIndex || !TryReserve(m_size + 1))
        {
            return false;
        }
        std::construct_at(m_data + m_size, value);
        ++m_size;
        return true;
    }

    [[nodiscard]] bool TryPushBack(T&& value) noexcept
    {
        if (m_size == kInvalidIndex || !TryReserve(m_size + 1))
        {
            return false;
        }
        std::construct_at(m_data + m_size, std::move(value));
        ++m_size;
        return true;
    }

    template <typename... Args>
    [[nodiscard]] T* TryEmplaceBack(Args&&... args) noexcept
    {
        if (m_size == kInvalidIndex || !TryReserve(m_size + 1))
        {
            return nullptr;
        }
        T* result = std::construct_at(m_data + m_size, std::forward<Args>(args)...);
        ++m_size;
        return result;
    }

    void PopBack() noexcept
    {
        CG_ASSERT(m_size > 0);
        if (m_size > 0)
        {
            --m_size;
            std::destroy_at(m_data + m_size);
        }
    }

    void Clear() noexcept
    {
        for (usize index = 0; index < m_size; ++index)
        {
            std::destroy_at(m_data + index);
        }
        m_size = 0;
    }

    [[nodiscard]] T& operator[](usize index) noexcept
    {
        CG_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] const T& operator[](usize index) const noexcept
    {
        CG_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] T* Data() noexcept
    {
        return m_data;
    }

    [[nodiscard]] const T* Data() const noexcept
    {
        return m_data;
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

    [[nodiscard]] T& Front() noexcept
    {
        CG_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] const T& Front() const noexcept
    {
        CG_ASSERT(m_size > 0);
        return m_data[0];
    }

    [[nodiscard]] T& Back() noexcept
    {
        CG_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] const T& Back() const noexcept
    {
        CG_ASSERT(m_size > 0);
        return m_data[m_size - 1];
    }

    [[nodiscard]] T* Begin() noexcept
    {
        return m_data;
    }

    [[nodiscard]] const T* Begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] T* End() noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] const T* End() const noexcept
    {
        return m_data + m_size;
    }

private:
    void Release() noexcept
    {
        for (usize index = 0; index < m_size; ++index)
        {
            std::destroy_at(m_data + index);
        }
        if (m_data != nullptr)
        {
            m_allocator->Deallocate(m_data);
        }
        m_data = nullptr;
        m_size = 0;
        m_capacity = 0;
    }

    Allocator* m_allocator = nullptr;
    T* m_data = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
};

}  // namespace cyber::memory
