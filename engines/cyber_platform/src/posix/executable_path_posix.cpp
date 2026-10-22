#include "executable_path.h"

#include <system_error>

namespace cyber::platform::detail
{

bool ExecutablePath(std::filesystem::path& path) noexcept
{
    std::error_code error;
    path = std::filesystem::read_symlink("/proc/self/exe", error);
    return !error;
}

}  // namespace cyber::platform::detail
