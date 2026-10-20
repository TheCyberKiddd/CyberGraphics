#include <cyber/core/build_config.h>
#include <cyber/test/test.h>

#include <string_view>

CG_TEST(build_config_kind)
{
    using cyber::core::BuildConfig;
    using cyber::core::BuildKind;

#if defined(CG_DEBUG)
    CG_EXPECT_EQ(BuildConfig::kKind, BuildKind::Debug);
    CG_EXPECT_TRUE(BuildConfig::kDebug);
    CG_EXPECT_FALSE(BuildConfig::kRelease);
    CG_EXPECT_FALSE(BuildConfig::kShipping);
    CG_EXPECT_STREQ(cyber::core::BuildKindName(BuildConfig::kKind), "debug");
#elif defined(CG_SHIPPING)
    CG_EXPECT_EQ(BuildConfig::kKind, BuildKind::Shipping);
    CG_EXPECT_FALSE(BuildConfig::kDebug);
    CG_EXPECT_FALSE(BuildConfig::kRelease);
    CG_EXPECT_TRUE(BuildConfig::kShipping);
    CG_EXPECT_STREQ(cyber::core::BuildKindName(BuildConfig::kKind), "shipping");
#else
    CG_EXPECT_EQ(BuildConfig::kKind, BuildKind::Release);
    CG_EXPECT_FALSE(BuildConfig::kDebug);
    CG_EXPECT_TRUE(BuildConfig::kRelease);
    CG_EXPECT_FALSE(BuildConfig::kShipping);
    CG_EXPECT_STREQ(cyber::core::BuildKindName(BuildConfig::kKind), "release");
#endif

    static_assert(!(BuildConfig::kDebug && BuildConfig::kRelease));
    static_assert(!(BuildConfig::kDebug && BuildConfig::kShipping));
    static_assert(!(BuildConfig::kRelease && BuildConfig::kShipping));
    CG_EXPECT_EQ(BuildConfig::kAssertionsEnabled, !BuildConfig::kShipping);
}

CG_TEST(build_config_platform)
{
    using cyber::core::BuildConfig;
    using cyber::core::Platform;

#if defined(CG_PLATFORM_WINDOWS)
    CG_EXPECT_EQ(BuildConfig::kPlatform, Platform::Windows);
    CG_EXPECT_STREQ(cyber::core::PlatformName(BuildConfig::kPlatform), "windows");
#elif defined(CG_PLATFORM_LINUX)
    CG_EXPECT_EQ(BuildConfig::kPlatform, Platform::Linux);
    CG_EXPECT_STREQ(cyber::core::PlatformName(BuildConfig::kPlatform), "linux");
#elif defined(CG_PLATFORM_MACOS)
    CG_EXPECT_EQ(BuildConfig::kPlatform, Platform::MacOS);
    CG_EXPECT_STREQ(cyber::core::PlatformName(BuildConfig::kPlatform), "macos");
#else
    CG_EXPECT_EQ(BuildConfig::kPlatform, Platform::Unknown);
    CG_EXPECT_STREQ(cyber::core::PlatformName(BuildConfig::kPlatform), "unknown");
#endif
}

CG_TEST(build_config_sanitizers)
{
    using cyber::core::BuildConfig;

#if defined(CG_ASAN)
    CG_EXPECT_TRUE(BuildConfig::kAddressSanitizer);
#else
    CG_EXPECT_FALSE(BuildConfig::kAddressSanitizer);
#endif

#if defined(CG_UBSAN)
    CG_EXPECT_TRUE(BuildConfig::kUndefinedBehaviorSanitizer);
#else
    CG_EXPECT_FALSE(BuildConfig::kUndefinedBehaviorSanitizer);
#endif

#if defined(CG_TSAN)
    CG_EXPECT_TRUE(BuildConfig::kThreadSanitizer);
#else
    CG_EXPECT_FALSE(BuildConfig::kThreadSanitizer);
#endif
}

CG_TEST(build_config_version)
{
    CG_EXPECT_TRUE(!cyber::core::BuildConfig::kVersion.empty());
    CG_EXPECT_TRUE(cyber::core::BuildConfig::kVersion ==
                   std::string_view(cyber::core::kVersionString));
}
