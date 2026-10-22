#include <cyber/platform/library.h>

#include <dlfcn.h>
#include <utility>

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

    dlerror();
    void* handle = dlopen(utf8Path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        return core::Result<DynamicLibrary, core::Error>::Err(
            core::Error{core::ErrorCode::NotFound, "dlopen failed"});
    }

    DynamicLibrary result;
    result.m_handle = handle;
    return core::Result<DynamicLibrary, core::Error>::Ok(std::move(result));
}

void DynamicLibrary::Close() noexcept
{
    if (m_handle != nullptr)
    {
        dlclose(m_handle);
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
    return dlsym(m_handle, name);
}

void* DynamicLibrary::NativeHandle() const noexcept
{
    return m_handle;
}

}  // namespace cyber::platform
