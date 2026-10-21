#include <cyber/core/assert.h>
#include <cyber/memory/string.h>

#include <cstring>

namespace cyber::memory
{

String::String(Allocator& allocator) noexcept : m_allocator(&allocator) {}

String::String(Allocator& allocator, StringView text) noexcept : m_allocator(&allocator)
{
    (void)TryAssign(text);
}

String::~String()
{
    Release();
}

String::String(String&& other) noexcept
    : m_allocator(other.m_allocator), m_data(other.m_data), m_size(other.m_size),
      m_capacity(other.m_capacity)
{
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
}

String& String::operator=(String&& other) noexcept
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

bool String::TryAssign(StringView text) noexcept
{
    if (text.Size() > kInvalidIndex - 1)
    {
        return false;
    }

    const char* source = text.Data();
    usize sourceOffset = 0;
    bool aliased = false;
    if (source != nullptr && m_data != nullptr)
    {
        const auto sourceAddress = reinterpret_cast<usize>(source);
        const auto dataAddress = reinterpret_cast<usize>(m_data);
        if (sourceAddress >= dataAddress && sourceAddress - dataAddress < m_capacity)
        {
            sourceOffset = static_cast<usize>(source - m_data);
            aliased = true;
        }
    }

    if (!EnsureCapacity(text.Size()))
    {
        return false;
    }

    if (aliased)
    {
        source = m_data + sourceOffset;
    }

    if (text.Size() > 0)
    {
        std::memmove(m_data, source, text.Size());
    }
    m_size = text.Size();
    m_data[m_size] = '\0';
    return true;
}

bool String::TryAppend(StringView text) noexcept
{
    if (text.Size() > kInvalidIndex - m_size)
    {
        return false;
    }

    const usize newSize = m_size + text.Size();
    if (newSize > kInvalidIndex - 1)
    {
        return false;
    }

    const char* source = text.Data();
    usize sourceOffset = 0;
    bool aliased = false;
    if (source != nullptr && m_data != nullptr)
    {
        const auto sourceAddress = reinterpret_cast<usize>(source);
        const auto dataAddress = reinterpret_cast<usize>(m_data);
        if (sourceAddress >= dataAddress && sourceAddress - dataAddress < m_capacity)
        {
            sourceOffset = static_cast<usize>(source - m_data);
            aliased = true;
        }
    }

    if (!EnsureCapacity(newSize))
    {
        return false;
    }

    if (aliased)
    {
        source = m_data + sourceOffset;
    }

    if (text.Size() > 0)
    {
        std::memmove(m_data + m_size, source, text.Size());
    }
    m_size = newSize;
    m_data[m_size] = '\0';
    return true;
}

bool String::TryAppend(char value) noexcept
{
    if (m_size == kInvalidIndex - 1)
    {
        return false;
    }
    if (!EnsureCapacity(m_size + 1))
    {
        return false;
    }
    m_data[m_size] = value;
    ++m_size;
    m_data[m_size] = '\0';
    return true;
}

bool String::TryReserve(usize capacity) noexcept
{
    return EnsureCapacity(capacity);
}

void String::Clear() noexcept
{
    m_size = 0;
    if (m_data != nullptr)
    {
        m_data[0] = '\0';
    }
}

const char* String::CStr() const noexcept
{
    return m_data == nullptr ? "" : m_data;
}

StringView String::View() const noexcept
{
    return StringView(CStr(), m_size);
}

char* String::Data() noexcept
{
    return m_data;
}

const char* String::Data() const noexcept
{
    return m_data;
}

usize String::Size() const noexcept
{
    return m_size;
}

usize String::Capacity() const noexcept
{
    return m_capacity;
}

bool String::Empty() const noexcept
{
    return m_size == 0;
}

char& String::operator[](usize index) noexcept
{
    CG_ASSERT(m_data != nullptr && index < m_size);
    return m_data[index];
}

const char& String::operator[](usize index) const noexcept
{
    CG_ASSERT(m_data != nullptr && index < m_size);
    return m_data[index];
}

bool String::EnsureCapacity(usize required) noexcept
{
    if (m_data != nullptr && required <= m_capacity)
    {
        return true;
    }
    if (required > kInvalidIndex - 1)
    {
        return false;
    }

    usize newCapacity = m_capacity == 0 ? 16 : m_capacity;
    while (newCapacity < required)
    {
        if (newCapacity > kInvalidIndex / 2)
        {
            newCapacity = required;
            break;
        }
        newCapacity *= 2;
    }
    if (newCapacity > kInvalidIndex - 1)
    {
        return false;
    }

    char* newData = static_cast<char*>(m_allocator->Allocate(newCapacity + 1, alignof(char)));
    if (newData == nullptr)
    {
        return false;
    }

    if (m_data != nullptr)
    {
        if (m_size > 0)
        {
            std::memcpy(newData, m_data, m_size);
        }
        m_allocator->Deallocate(m_data);
    }

    m_data = newData;
    m_capacity = newCapacity;
    m_data[m_size] = '\0';
    return true;
}

void String::Release() noexcept
{
    if (m_data != nullptr && m_allocator != nullptr)
    {
        m_allocator->Deallocate(m_data);
    }
    m_data = nullptr;
    m_size = 0;
    m_capacity = 0;
}

}  // namespace cyber::memory
