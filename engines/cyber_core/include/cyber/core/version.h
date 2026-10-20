#pragma once

#include <cstdint>

namespace cyber::core {

inline constexpr std::uint32_t kVersionMajor = 0;
inline constexpr std::uint32_t kVersionMinor = 1;
inline constexpr std::uint32_t kVersionPatch = 0;

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

} // namespace cyber::core
