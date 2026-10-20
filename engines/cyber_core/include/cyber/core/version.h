#pragma once

#include <cyber/core/version_generated.h>

#include <cstdint>

namespace cyber::core
{

struct Version
{
    std::uint32_t major = kVersionMajor;
    std::uint32_t minor = kVersionMinor;
    std::uint32_t patch = kVersionPatch;
};

[[nodiscard]] constexpr Version VersionNumber() noexcept
{
    return Version{};
}

// Returns a static string such as "0.1.0". The returned pointer remains valid
// for the lifetime of the program.
[[nodiscard]] const char* VersionString() noexcept;

}  // namespace cyber::core
