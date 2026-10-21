#pragma once

#include <cyber/memory/types.h>

#include <type_traits>

namespace cyber::memory
{

// Non-owning view over a contiguous range. Span never allocates and does not
// extend the lifetime of the range it points at.
template <typename T>
class Span
{
public:
    using Element = T;
    using Value = std::remove_cv_t<T>;
    using Pointer = T*;
    using Reference = T&;
    using Iterator = T*;

    constexpr Span() noexcept = default;

    constexpr Span(T* data, usize size) noexcept : m_data(data), m_size(size) {}

    template <usize N>
    constexpr Span(T (&array)[N]) noexcept : m_data(array), m_size(N)
    {
    }

    template <typename U>
        requires std::is_convertible_v<U*, T*>
    constexpr Span(const Span<U>& other) noexcept : m_data(other.Data()), m_size(other.Size())
    {
    }

    [[nodiscard]] constexpr T* Data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr usize Size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr usize SizeBytes() const noexcept
    {
        return m_size * sizeof(T);
    }

    [[nodiscard]] constexpr bool Empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] constexpr T& operator[](usize index) const noexcept
    {
        return m_data[index];
    }

    [[nodiscard]] constexpr T& Front() const noexcept
    {
        return m_data[0];
    }

    [[nodiscard]] constexpr T& Back() const noexcept
    {
        return m_data[m_size - 1];
    }

    [[nodiscard]] constexpr T* Begin() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr T* End() const noexcept
    {
        return m_data + m_size;
    }

    [[nodiscard]] constexpr Span Subspan(usize offset, usize count = kInvalidIndex) const noexcept
    {
        if (offset > m_size)
        {
            return Span{};
        }
        const usize available = m_size - offset;
        const usize actual = count < available ? count : available;
        return Span{m_data + offset, actual};
    }

    [[nodiscard]] constexpr Span First(usize count) const noexcept
    {
        return Subspan(0, count);
    }

    [[nodiscard]] constexpr Span Last(usize count) const noexcept
    {
        return count < m_size ? Subspan(m_size - count) : *this;
    }

private:
    T* m_data = nullptr;
    usize m_size = 0;
};

}  // namespace cyber::memory
