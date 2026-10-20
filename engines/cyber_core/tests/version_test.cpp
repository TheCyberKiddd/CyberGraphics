#include <cyber/core/version.h>
#include <cyber/test/test.h>

CG_TEST(version_number)
{
    const cyber::core::Version version = cyber::core::VersionNumber();
    CG_EXPECT_EQ(version.major, cyber::core::kVersionMajor);
    CG_EXPECT_EQ(version.minor, cyber::core::kVersionMinor);
    CG_EXPECT_EQ(version.patch, cyber::core::kVersionPatch);
}

CG_TEST(version_string)
{
    const char* text = cyber::core::VersionString();
    CG_REQUIRE(text != nullptr);
    CG_EXPECT_STREQ(text, cyber::core::kVersionString);
    CG_EXPECT_TRUE(cyber::core::kVersionString != nullptr);
    CG_EXPECT_TRUE(cyber::core::kVersionString[0] != '\0');
}
