#include "executable_path.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <string>
#include <windows.h>

namespace cyber::platform::detail
{

bool ExecutablePath(std::filesystem::path& path) noexcept
{
    wchar_t buffer[1024] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 1024);
    if (length == 0 || length >= 1024)
    {
        return false;
    }
    path = std::filesystem::path(std::wstring(buffer, length));
    return true;
}

}  // namespace cyber::platform::detail
