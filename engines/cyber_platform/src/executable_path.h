#pragma once

#include <filesystem>

namespace cyber::platform::detail
{

// Resolves the current executable path. Implemented by the platform-specific
// translation unit so Win32 headers stay under src/win32/.
bool ExecutablePath(std::filesystem::path& path) noexcept;

}  // namespace cyber::platform::detail
