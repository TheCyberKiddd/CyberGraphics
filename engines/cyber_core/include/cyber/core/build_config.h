#pragma once

#include <cyber/core/types.h>
#include <cyber/core/version_generated.h>

#include <string_view>

namespace cyber::core
{

enum class BuildKind : u8
{
    Debug,
    Release,
    Shipping,
};

enum class Platform : u8
{
    Windows,
    Linux,
    MacOS,
    Unknown,
};

constexpr const char* BuildKindName(BuildKind kind) noexcept
{
    switch (kind)
    {
    case BuildKind::Debug:
        return "debug";
    case BuildKind::Release:
        return "release";
    case BuildKind::Shipping:
        return "shipping";
    }
    return "unknown";
}

constexpr const char* PlatformName(Platform platform) noexcept
{
    switch (platform)
    {
    case Platform::Windows:
        return "windows";
    case Platform::Linux:
        return "linux";
    case Platform::MacOS:
        return "macos";
    case Platform::Unknown:
        return "unknown";
    }
    return "unknown";
}

// Compile-time view of the build. This reads CMake-provided definitions only;
// it never probes the operating system or reads configuration files.
struct BuildConfig
{
#if defined(CG_SHIPPING)
    static constexpr BuildKind kKind = BuildKind::Shipping;
#elif defined(CG_DEBUG)
    static constexpr BuildKind kKind = BuildKind::Debug;
#else
    static constexpr BuildKind kKind = BuildKind::Release;
#endif

#if defined(CG_PLATFORM_WINDOWS)
    static constexpr Platform kPlatform = Platform::Windows;
#elif defined(CG_PLATFORM_LINUX)
    static constexpr Platform kPlatform = Platform::Linux;
#elif defined(CG_PLATFORM_MACOS)
    static constexpr Platform kPlatform = Platform::MacOS;
#else
    static constexpr Platform kPlatform = Platform::Unknown;
#endif

    static constexpr bool kDebug = kKind == BuildKind::Debug;
    static constexpr bool kRelease = kKind == BuildKind::Release;
    static constexpr bool kShipping = kKind == BuildKind::Shipping;
    static constexpr bool kAssertionsEnabled = !kShipping;

#if defined(CG_ASAN)
    static constexpr bool kAddressSanitizer = true;
#else
    static constexpr bool kAddressSanitizer = false;
#endif

#if defined(CG_UBSAN)
    static constexpr bool kUndefinedBehaviorSanitizer = true;
#else
    static constexpr bool kUndefinedBehaviorSanitizer = false;
#endif

#if defined(CG_TSAN)
    static constexpr bool kThreadSanitizer = true;
#else
    static constexpr bool kThreadSanitizer = false;
#endif

    static constexpr std::string_view kVersion{kVersionString};
};

}  // namespace cyber::core
