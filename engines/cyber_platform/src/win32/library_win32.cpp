#include <cyber/platform/library.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <filesystem>
#include <string>
#include <utility>
#include <windows.h>

namespace cyber::platform
{

DynamicLibrary::~DynamicLibrary()
{
    Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : m_handle(other.m_handle)
{
    other.m_handle = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        m_handle = other.m_handle;
        other.m_handle = nullptr;
    }
    return *this;
}

core::Result<DynamicLibrary, core::Error> DynamicLibrary::Open(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Result<DynamicLibrary, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "library path is null"});
    }

    const std::wstring path =
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Path))).wstring();
    HMODULE handle = LoadLibraryW(path.c_str());
    if (handle == nullptr)
    {
        const DWORD error = GetLastError();
        const core::ErrorCode code = error == ERROR_MOD_NOT_FOUND || error == ERROR_FILE_NOT_FOUND
                                         ? core::ErrorCode::NotFound
                                         : core::ErrorCode::Internal;
        return core::Result<DynamicLibrary, core::Error>::Err(
            core::Error{code, "LoadLibraryW failed"});
    }

    DynamicLibrary result;
    result.m_handle = handle;
    return core::Result<DynamicLibrary, core::Error>::Ok(std::move(result));
}

void DynamicLibrary::Close() noexcept
{
    if (m_handle != nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(m_handle));
        m_handle = nullptr;
    }
}

bool DynamicLibrary::IsOpen() const noexcept
{
    return m_handle != nullptr;
}

void* DynamicLibrary::GetSymbol(const char* name) const noexcept
{
    if (m_handle == nullptr || name == nullptr)
    {
        return nullptr;
    }
    FARPROC symbol = GetProcAddress(static_cast<HMODULE>(m_handle), name);
    return reinterpret_cast<void*>(symbol);
}

void* DynamicLibrary::NativeHandle() const noexcept
{
    return m_handle;
}

}  // namespace cyber::platform
