#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/platform/types.h>

#include <type_traits>

namespace cyber::platform
{

// Dynamic library wrapper. The handle is owned by this object and unloaded on
// destruction or Close. Symbol names are UTF-8/ASCII and not owned.
class DynamicLibrary
{
public:
    DynamicLibrary() noexcept = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    [[nodiscard]] static core::Result<DynamicLibrary, core::Error>
    Open(const char* utf8Path) noexcept;

    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] void* GetSymbol(const char* name) const noexcept;
    [[nodiscard]] void* NativeHandle() const noexcept;

    // Typed symbol lookup. T must be a function pointer type. The caller owns the
    // returned pointer; it stays valid while the library is open.
    template <typename T>
    [[nodiscard]] core::Result<T, core::Error> GetFunction(const char* name) const noexcept
    {
        static_assert(std::is_pointer_v<T>, "T must be a function pointer type");
        static_assert(std::is_function_v<std::remove_pointer_t<T>>,
                      "T must be a function pointer type");

        void* symbol = GetSymbol(name);
        if (symbol == nullptr)
        {
            return core::Result<T, core::Error>::Err(
                core::Error{core::ErrorCode::NotFound, "dynamic library symbol was not found"});
        }
        return core::Result<T, core::Error>::Ok(reinterpret_cast<T>(symbol));
    }

private:
    void* m_handle = nullptr;
};

}  // namespace cyber::platform
