#pragma once

#include <cyber/memory/allocator.h>
#include <cyber/memory/string_view.h>
#include <cyber/memory/types.h>

namespace cyber::memory
{

// Owned, null-terminated byte string. Copy is deleted; move transfers the
// allocation. Capacity counts characters excluding the null terminator. A
// failed operation leaves the previous contents intact.
class String
{
public:
    explicit String(Allocator& allocator) noexcept;
    String(Allocator& allocator, StringView text) noexcept;
    ~String();

    String(const String&) = delete;
    String& operator=(const String&) = delete;

    String(String&& other) noexcept;
    String& operator=(String&& other) noexcept;

    [[nodiscard]] bool TryAssign(StringView text) noexcept;
    [[nodiscard]] bool TryAppend(StringView text) noexcept;
    [[nodiscard]] bool TryAppend(char value) noexcept;
    [[nodiscard]] bool TryReserve(usize capacity) noexcept;

    void Clear() noexcept;

    [[nodiscard]] const char* CStr() const noexcept;
    [[nodiscard]] StringView View() const noexcept;
    [[nodiscard]] char* Data() noexcept;
    [[nodiscard]] const char* Data() const noexcept;
    [[nodiscard]] usize Size() const noexcept;
    [[nodiscard]] usize Capacity() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

    [[nodiscard]] char& operator[](usize index) noexcept;
    [[nodiscard]] const char& operator[](usize index) const noexcept;

private:
    [[nodiscard]] bool EnsureCapacity(usize required) noexcept;
    void Release() noexcept;

    Allocator* m_allocator = nullptr;
    char* m_data = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
};

}  // namespace cyber::memory
