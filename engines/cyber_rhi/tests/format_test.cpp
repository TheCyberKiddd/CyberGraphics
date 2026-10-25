#include <cyber/rhi/format.h>
#include <cyber/test/test.h>

#include <cstring>

using namespace cyber::rhi;
using cyber::core::u32;

namespace
{

bool HasName(Format format) noexcept
{
    const char* name = FormatName(format);
    return name != nullptr && std::strcmp(name, "unknown") != 0;
}

}  // namespace

CG_TEST(rhi_format_every_enumerator_is_named)
{
    CG_EXPECT_EQ(kFormatCount, static_cast<u32>(Format::Count));
    CG_EXPECT_TRUE(kFormatCount > 20);

    for (u32 index = 0; index < kFormatCount; ++index)
    {
        const Format format = static_cast<Format>(index);
        if (format == Format::Count)
        {
            continue;
        }
        // A missing name would make a validation message useless.
        CG_EXPECT_TRUE(HasName(format));
    }

    // Out-of-range values report "unknown" rather than reading past the switch.
    CG_EXPECT_STREQ(FormatName(Format::Count), "unknown");
    CG_EXPECT_STREQ(FormatName(static_cast<Format>(kFormatCount + 7)), "unknown");
}

CG_TEST(rhi_format_classification_is_exclusive)
{
    for (u32 index = 0; index < kFormatCount; ++index)
    {
        const Format format = static_cast<Format>(index);
        if (format == Format::Count)
        {
            continue;
        }

        const bool depth = IsDepthFormat(format);
        const bool sRGB = IsSrgbFormat(format);
        const bool compressed = IsBlockCompressedFormat(format);
        const bool integer = IsUnsignedIntegerFormat(format);

        // A depth format is never an sRGB color format and never compressed.
        if (depth)
        {
            CG_EXPECT_FALSE(sRGB);
            CG_EXPECT_FALSE(compressed);
            CG_EXPECT_FALSE(integer);
        }
        // A compressed format is never a depth format and never an integer one.
        if (compressed)
        {
            CG_EXPECT_FALSE(depth);
            CG_EXPECT_FALSE(integer);
        }
        // sRGB and unsigned-integer formats are mutually exclusive.
        CG_EXPECT_FALSE(sRGB && integer);
    }
}

CG_TEST(rhi_format_specific_cases)
{
    CG_EXPECT_TRUE(IsDepthFormat(Format::D32Float));
    CG_EXPECT_TRUE(IsDepthFormat(Format::D24UnormS8Uint));
    CG_EXPECT_TRUE(IsStencilFormat(Format::D24UnormS8Uint));
    CG_EXPECT_TRUE(IsStencilFormat(Format::D32FloatS8Uint));
    CG_EXPECT_FALSE(IsStencilFormat(Format::D32Float));
    CG_EXPECT_FALSE(IsDepthFormat(Format::RGBA32Float));

    CG_EXPECT_TRUE(IsSrgbFormat(Format::RGBA8UnormSrgb));
    CG_EXPECT_TRUE(IsSrgbFormat(Format::BGRA8UnormSrgb));
    CG_EXPECT_FALSE(IsSrgbFormat(Format::RGBA8Unorm));
    CG_EXPECT_FALSE(IsSrgbFormat(Format::BGRA8Unorm));

    // Every sRGB format is the encoded twin of an unorm format with the same
    // channels, which is what lets a backend share one mapping entry.
    CG_EXPECT_TRUE(IsSrgbFormat(Format::BC7UnormSrgb));
    CG_EXPECT_FALSE(IsSrgbFormat(Format::BC7Unorm));
    CG_EXPECT_TRUE(IsBlockCompressedFormat(Format::BC5Unorm));
    CG_EXPECT_FALSE(IsBlockCompressedFormat(Format::RGBA16Float));

    CG_EXPECT_TRUE(IsUnsignedIntegerFormat(Format::R32Uint));
    CG_EXPECT_FALSE(IsUnsignedIntegerFormat(Format::R32Float));

    CG_EXPECT_STREQ(FormatName(Format::BGRA8UnormSrgb), "BGRA8UnormSrgb");
    CG_EXPECT_STREQ(FormatName(Format::D32FloatS8Uint), "D32FloatS8Uint");
}
