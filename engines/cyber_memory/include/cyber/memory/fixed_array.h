#pragma once

#include <cyber/core/assert.h>
#include <cyber/memory/types.h>

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace cyber::memory
{

// Inline fixed-capacity array. It never allocates and stores at most N values.
// Copy and move are supported when T supports them.
template <typename T, usize N>
class FixedArray
{
    static_assert(N > 0, "FixedArray requires a non-zero capacity");

public:
    using Iterator = T*;
    using ConstIterator = const T*;

    FixedArray() noexcept = default;

    FixedArray(const FixedArray& other) noexcept
    {
        for (usize index = 0; index < other.m_size; ++index)
        {
            std::construct_at(Data() + index, other[static_cast<usize>(index)]);
        }
        m_size = other.m_size;
    }

    FixedArray& operator=(const FixedArray& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            for (usize index = 0; index < other.m_size; ++index)
            {
                std::construct_at(Data() + index, other[index]);
            }
            m_size = other.m_size;
        }
        return *this;
    }

    FixedArray(FixedArray&& other) noexcept
    {
        for (usize index = 0; index < other.m_size; ++index)
        {
            std::construct_at(Data() + index, std::move(other[index]));
        }
        m_size = other.m_size;
        other.Clear();
    }

    FixedArray& operator=(FixedArray&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            for (usize index = 0; index < other.m_size; ++index)
            {
                std::construct_at(Data() + index, std::move(other[index]));
            }
            m_size = other.m_size;
            other.Clear();
        }
        return *this;
    }

    ~FixedArray()
    {
        Clear();
    }

    [[nodiscard]] bool TryPushBack(const T& value) noexcept
    {
        if (m_size >= N)
        {
            return false;
        }
        std::construct_at(Data() + m_size, value);
        ++m_size;
        return true;
    }

    [[nodiscard]] bool TryPushBack(T&& value) noexcept
    {
        if (m_size >= N)
        {
            return false;
        }
        std::construct_at(Data() + m_size, std::move(value));
        ++m_size;
        return true;
    }

    template <typename... Args>
    [[nodiscard]] T* TryEmplaceBack(Args&&... args) noexcept
    {
        if (m_size >= N)
        {
            return nullptr;
        }
        T* result = std::construct_at(Data() + m_size, std::forward<Args>(args)...);
        ++m_size;
        return result;
    }

    void PopBack() noexcept
    {
        CG_ASSERT(m_size > 0);
        if (m_size > 0)
        {
            --m_size;
            std::destroy_at(Data() + m_size);
        }
    }

    void Clear() noexcept
    {
        for (usize index = 0; index < m_size; ++index)
        {
            std::destroy_at(Data() + index);
        }
        m_size = 0;
    }

    [[nodiscard]] T& operator[](usize index) noexcept
    {
        CG_ASSERT(index < m_size);
        return Data()[index];
    }

    [[nodiscard]] const T& operator[](usize index) const noexcept
    {
        CG_ASSERT(index < m_size);
        return Data()[index];
    }

    [[nodiscard]] T* Data() noexcept
    {
        return reinterpret_cast<T*>(m_storage);
    }

    [[nodiscard]] const T* Data() const noexcept
    {
        return reinterpret_cast<const T*>(m_storage);
    }

    [[nodiscard]] usize Size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] static constexpr usize Capacity() noexcept
    {
        return N;
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] bool Full() const noexcept
    {
        return m_size == N;
    }

    [[nodiscard]] T& Front() noexcept
    {
        CG_ASSERT(m_size > 0);
        return Data()[0];
    }

    [[nodiscard]] const T& Front() const noexcept
    {
        CG_ASSERT(m_size > 0);
        return Data()[0];
    }

    [[nodiscard]] T& Back() noexcept
    {
        CG_ASSERT(m_size > 0);
        return Data()[m_size - 1];
    }

    [[nodiscard]] const T& Back() const noexcept
    {
        CG_ASSERT(m_size > 0);
        return Data()[m_size - 1];
    }

    [[nodiscard]] T* Begin() noexcept
    {
        return Data();
    }

    [[nodiscard]] const T* Begin() const noexcept
    {
        return Data();
    }

    [[nodiscard]] T* End() noexcept
    {
        return Data() + m_size;
    }

    [[nodiscard]] const T* End() const noexcept
    {
        return Data() + m_size;
    }

private:
    alignas(T) Byte m_storage[sizeof(T) * N]{};
    usize m_size = 0;
};

}  // namespace cyber::memory
