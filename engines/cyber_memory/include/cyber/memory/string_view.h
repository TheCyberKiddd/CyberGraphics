#pragma once

#include <cyber/core/hash.h>
#include <cyber/memory/types.h>

#include <string_view>

namespace cyber::memory
{

// Non-owning view over UTF-8 bytes. The view is not null-terminated and does not
// keep the source alive. A default-constructed view is empty.
class StringView
{
public:
    constexpr StringView() noexcept = default;

    constexpr StringView(const char* text) noexcept
        : m_data(text), m_size(text == nullptr ? 0 : Length(text))
    {
    }

    constexpr StringView(const char* data, usize size) noexcept : m_data(data), m_size(size) {}

    constexpr StringView(std::string_view text) noexcept : m_data(text.data()), m_size(text.size())
    {
    }

    [[nodiscard]] constexpr const char* Data() const noexcept
    {
        return m_data;
    }

    [[nodiscard]] constexpr usize Size() const noexcept
    {
        return m_size;
    }

    [[nodiscard]] constexpr bool Empty() const noexcept
    {
        return m_size == 0;
    }

    [[nodiscard]] constexpr char operator[](usize index) const noexcept
    {
        return m_data[index];
    }

    [[nodiscard]] constexpr StringView Substr(usize offset,
                                              usize count = kInvalidIndex) const noexcept
    {
        if (offset > m_size)
        {
            return StringView{};
        }
        const usize available = m_size - offset;
        const usize actual = count < available ? count : available;
        return StringView{m_data + offset, actual};
    }

    [[nodiscard]] constexpr bool StartsWith(StringView prefix) const noexcept
    {
        if (prefix.m_size > m_size)
        {
            return false;
        }
        for (usize index = 0; index < prefix.m_size; ++index)
        {
            if (m_data[index] != prefix.m_data[index])
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool EndsWith(StringView suffix) const noexcept
    {
        if (suffix.m_size > m_size)
        {
            return false;
        }
        const usize offset = m_size - suffix.m_size;
        for (usize index = 0; index < suffix.m_size; ++index)
        {
            if (m_data[offset + index] != suffix.m_data[index])
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr usize Find(char value) const noexcept
    {
        for (usize index = 0; index < m_size; ++index)
        {
            if (m_data[index] == value)
            {
                return index;
            }
        }
        return kInvalidIndex;
    }

    [[nodiscard]] constexpr bool Contains(char value) const noexcept
    {
        return Find(value) != kInvalidIndex;
    }

    [[nodiscard]] constexpr bool Equals(StringView other) const noexcept
    {
        if (m_size != other.m_size)
        {
            return false;
        }
        for (usize index = 0; index < m_size; ++index)
        {
            if (m_data[index] != other.m_data[index])
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr std::string_view ToStd() const noexcept
    {
        return std::string_view(m_data, m_size);
    }

    [[nodiscard]] u64 Hash(u64 seed = 0) const noexcept
    {
        return cyber::core::HashBytes(m_data, m_size, seed);
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return ToStd();
    }

    friend constexpr bool operator==(StringView lhs, StringView rhs) noexcept
    {
        return lhs.Equals(rhs);
    }

    friend constexpr bool operator!=(StringView lhs, StringView rhs) noexcept
    {
        return !lhs.Equals(rhs);
    }

private:
    static constexpr usize Length(const char* text) noexcept
    {
        usize size = 0;
        while (text[size] != '\0')
        {
            ++size;
        }
        return size;
    }

    const char* m_data = nullptr;
    usize m_size = 0;
};

}  // namespace cyber::memory
