#include <cyber/rhi/format.h>
#include <cyber/test/test.h>

#include <cstring>

using namespace cyber::rhi;
using cyber::core::u32;

namespace
{

struct ExpectedRow
{
    Format format;
    u32 bytesPerBlock;
    u32 blockWidth;
    u32 channelCount;
    FormatComponentType componentType;
    bool depth;
    bool stencil;
    bool srgb;
    bool compressed;
};

// One row per enumerator. The loop below checks that this table covers the whole
// enumeration, so a new format without a row fails the test rather than slipping
// through with a zeroed row.
constexpr ExpectedRow kExpectedRows[] = {
    {Format::R8Unorm, 1, 1, 1, FormatComponentType::Unorm, false, false, false, false},
    {Format::RG8Unorm, 2, 1, 2, FormatComponentType::Unorm, false, false, false, false},
    {Format::RGBA8Unorm, 4, 1, 4, FormatComponentType::Unorm, false, false, false, false},
    {Format::RGBA8UnormSrgb, 4, 1, 4, FormatComponentType::Unorm, false, false, true, false},
    {Format::BGRA8Unorm, 4, 1, 4, FormatComponentType::Unorm, false, false, false, false},
    {Format::BGRA8UnormSrgb, 4, 1, 4, FormatComponentType::Unorm, false, false, true, false},
    {Format::R16Float, 2, 1, 1, FormatComponentType::Float, false, false, false, false},
    {Format::RG16Float, 4, 1, 2, FormatComponentType::Float, false, false, false, false},
    {Format::RGBA16Float, 8, 1, 4, FormatComponentType::Float, false, false, false, false},
    {Format::R32Float, 4, 1, 1, FormatComponentType::Float, false, false, false, false},
    {Format::RG32Float, 8, 1, 2, FormatComponentType::Float, false, false, false, false},
    {Format::RGBA32Float, 16, 1, 4, FormatComponentType::Float, false, false, false, false},
    {Format::R32Uint, 4, 1, 1, FormatComponentType::Uint, false, false, false, false},
    {Format::RG32Uint, 8, 1, 2, FormatComponentType::Uint, false, false, false, false},
    {Format::RGBA32Uint, 16, 1, 4, FormatComponentType::Uint, false, false, false, false},
    {Format::R10G10B10A2Unorm, 4, 1, 4, FormatComponentType::Unorm, false, false, false, false},
    {Format::RG11B10Float, 4, 1, 3, FormatComponentType::Float, false, false, false, false},
    {Format::D16Unorm, 2, 1, 1, FormatComponentType::Depth, true, false, false, false},
    {Format::D32Float, 4, 1, 1, FormatComponentType::Depth, true, false, false, false},
    {Format::D24UnormS8Uint, 4, 1, 2, FormatComponentType::DepthStencil, true, true, false, false},
    {Format::D32FloatS8Uint, 8, 1, 2, FormatComponentType::DepthStencil, true, true, false, false},
    {Format::BC1Unorm, 8, 4, 4, FormatComponentType::Float, false, false, false, true},
    {Format::BC1UnormSrgb, 8, 4, 4, FormatComponentType::Float, false, false, true, true},
    {Format::BC3Unorm, 16, 4, 4, FormatComponentType::Float, false, false, false, true},
    {Format::BC3UnormSrgb, 16, 4, 4, FormatComponentType::Float, false, false, true, true},
    {Format::BC5Unorm, 16, 4, 2, FormatComponentType::Float, false, false, false, true},
    {Format::BC7Unorm, 16, 4, 4, FormatComponentType::Float, false, false, false, true},
    {Format::BC7UnormSrgb, 16, 4, 4, FormatComponentType::Float, false, false, true, true},
};

}  // namespace

CG_TEST(rhi_format_table_matches_the_enumeration)
{
    // Every real format has at least one row, and every row names a real format.
    u32 realFormats = 0;
    for (u32 index = 0; index < kFormatCount; ++index)
    {
        const FormatInfo info = FormatInfoOf(static_cast<Format>(index));
        CG_EXPECT_TRUE(info.bytesPerBlock > 0);
        CG_EXPECT_TRUE(info.channelCount >= 1 && info.channelCount <= 4);
        CG_EXPECT_TRUE((info.blockWidth == 1 && info.blockHeight == 1) ||
                       (info.blockWidth == 4 && info.blockHeight == 4));
        CG_EXPECT_EQ(info.compressed, info.blockWidth == 4);
        if (info.compressed)
        {
            CG_EXPECT_TRUE(info.bytesPerBlock == 8 || info.bytesPerBlock == 16);
        }
        if (info.depth)
        {
            CG_EXPECT_FALSE(info.srgb);
        }
        if (info.stencil)
        {
            CG_EXPECT_TRUE(info.depth);
        }
        CG_EXPECT_TRUE(std::strcmp(FormatName(static_cast<Format>(index)), "unknown") != 0);
        ++realFormats;
    }
    CG_EXPECT_EQ(realFormats, u32{28});

    // The sentinel row classifies as nothing.
    const FormatInfo sentinel = FormatInfoOf(Format::Count);
    CG_EXPECT_EQ(sentinel.bytesPerBlock, u32{0});
    CG_EXPECT_EQ(sentinel.channelCount, u32{0});
    CG_EXPECT_STREQ(FormatName(Format::Count), "unknown");
    CG_EXPECT_STREQ(FormatName(static_cast<Format>(200)), "unknown");
    CG_EXPECT_STREQ(FormatComponentTypeName(static_cast<FormatComponentType>(200)), "unknown");

    const u32 rowCount = sizeof(kExpectedRows) / sizeof(kExpectedRows[0]);
    CG_EXPECT_EQ(rowCount, realFormats);
    for (u32 index = 0; index < rowCount; ++index)
    {
        const ExpectedRow& row = kExpectedRows[index];
        const FormatInfo info = FormatInfoOf(row.format);
        CG_EXPECT_EQ(info.bytesPerBlock, row.bytesPerBlock);
        CG_EXPECT_EQ(info.blockWidth, row.blockWidth);
        CG_EXPECT_EQ(info.channelCount, row.channelCount);
        CG_EXPECT_EQ(info.componentType, row.componentType);
        CG_EXPECT_EQ(info.depth, row.depth);
        CG_EXPECT_EQ(info.stencil, row.stencil);
        CG_EXPECT_EQ(info.srgb, row.srgb);
        CG_EXPECT_EQ(info.compressed, row.compressed);
    }
}

CG_TEST(rhi_format_table_accessors)
{
    CG_EXPECT_EQ(FormatBytesPerBlock(Format::RGBA32Float), u32{16});
    CG_EXPECT_EQ(FormatBytesPerBlock(Format::BC5Unorm), u32{16});
    CG_EXPECT_EQ(FormatBytesPerBlock(Format::Count), u32{0});
    CG_EXPECT_EQ(FormatBlockWidth(Format::BC7Unorm), u32{4});
    CG_EXPECT_EQ(FormatBlockWidth(Format::R8Unorm), u32{1});
    CG_EXPECT_EQ(FormatBlockHeight(Format::BC1Unorm), u32{4});
    CG_EXPECT_EQ(FormatChannelCount(Format::RG11B10Float), u32{3});
    CG_EXPECT_EQ(FormatChannelCount(Format::D24UnormS8Uint), u32{2});
    CG_EXPECT_EQ(FormatComponentTypeOf(Format::R32Uint), FormatComponentType::Uint);
    CG_EXPECT_EQ(FormatComponentTypeOf(Format::D16Unorm), FormatComponentType::Depth);

    CG_EXPECT_TRUE(IsDepthFormat(Format::D32Float));
    CG_EXPECT_FALSE(IsDepthFormat(Format::R32Float));
    CG_EXPECT_TRUE(IsStencilFormat(Format::D24UnormS8Uint));
    CG_EXPECT_FALSE(IsStencilFormat(Format::D32Float));
    CG_EXPECT_TRUE(IsSrgbFormat(Format::BC7UnormSrgb));
    CG_EXPECT_TRUE(IsColorFormat(Format::RGBA8Unorm));
    CG_EXPECT_FALSE(IsColorFormat(Format::D32Float));
    CG_EXPECT_FALSE(IsColorFormat(Format::Count));
    CG_EXPECT_TRUE(IsBlockCompressedFormat(Format::BC1Unorm));
    CG_EXPECT_TRUE(IsUnsignedIntegerFormat(Format::RG32Uint));
    CG_EXPECT_FALSE(IsUnsignedIntegerFormat(Format::RG32Float));
}

CG_TEST(rhi_format_table_view_compatibility)
{
    // A format is compatible with itself.
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::RGBA8Unorm, Format::RGBA8Unorm));
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::BC7Unorm, Format::BC7Unorm));

    // The sRGB and linear encodings of one layout are interchangeable views.
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::RGBA8UnormSrgb, Format::RGBA8Unorm));
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::RGBA8Unorm, Format::RGBA8UnormSrgb));
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::BGRA8UnormSrgb, Format::BGRA8Unorm));
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::BC1UnormSrgb, Format::BC1Unorm));
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::BC3UnormSrgb, Format::BC3Unorm));
    CG_EXPECT_TRUE(IsFormatViewCompatible(Format::BC7UnormSrgb, Format::BC7Unorm));

    // Different layouts, channel counts, or component types are not views of
    // each other, even when the byte size matches.
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::BGRA8Unorm, Format::RGBA8Unorm));
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::R32Float, Format::R32Uint));
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::RG32Float, Format::RGBA16Float));
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::R8Unorm, Format::RGBA8Unorm));
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::D32Float, Format::D24UnormS8Uint));
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::D32Float, Format::R32Float));
    CG_EXPECT_FALSE(IsFormatViewCompatible(Format::Count, Format::Count));
    CG_EXPECT_FALSE(IsFormatViewCompatible(static_cast<Format>(99), Format::R8Unorm));
}

CG_TEST(rhi_format_table_srgb_variants)
{
    Format srgb = Format::Count;
    CG_EXPECT_TRUE(TrySrgbVariant(Format::RGBA8Unorm, &srgb));
    CG_EXPECT_EQ(srgb, Format::RGBA8UnormSrgb);
    CG_EXPECT_TRUE(TrySrgbVariant(Format::BGRA8Unorm, &srgb));
    CG_EXPECT_EQ(srgb, Format::BGRA8UnormSrgb);
    CG_EXPECT_TRUE(TrySrgbVariant(Format::BC1Unorm, &srgb));
    CG_EXPECT_EQ(srgb, Format::BC1UnormSrgb);
    CG_EXPECT_TRUE(TrySrgbVariant(Format::BC3Unorm, &srgb));
    CG_EXPECT_EQ(srgb, Format::BC3UnormSrgb);
    CG_EXPECT_TRUE(TrySrgbVariant(Format::BC7Unorm, &srgb));
    CG_EXPECT_EQ(srgb, Format::BC7UnormSrgb);

    // No variant exists, or the format is already encoded.
    CG_EXPECT_FALSE(TrySrgbVariant(Format::R8Unorm, &srgb));
    CG_EXPECT_FALSE(TrySrgbVariant(Format::R32Float, &srgb));
    CG_EXPECT_FALSE(TrySrgbVariant(Format::RGBA8UnormSrgb, &srgb));
    CG_EXPECT_FALSE(TrySrgbVariant(Format::RGBA8Unorm, nullptr));

    Format linear = Format::Count;
    CG_EXPECT_TRUE(TryLinearVariant(Format::RGBA8UnormSrgb, &linear));
    CG_EXPECT_EQ(linear, Format::RGBA8Unorm);
    CG_EXPECT_TRUE(TryLinearVariant(Format::BGRA8UnormSrgb, &linear));
    CG_EXPECT_EQ(linear, Format::BGRA8Unorm);
    CG_EXPECT_TRUE(TryLinearVariant(Format::BC1UnormSrgb, &linear));
    CG_EXPECT_EQ(linear, Format::BC1Unorm);
    CG_EXPECT_FALSE(TryLinearVariant(Format::RGBA8Unorm, &linear));
    CG_EXPECT_FALSE(TryLinearVariant(Format::RGBA8UnormSrgb, nullptr));

    // The two lookups are inverses.
    linear = Format::RGBA8Unorm;
    CG_EXPECT_TRUE(TrySrgbVariant(linear, &srgb));
    CG_EXPECT_EQ(srgb, Format::RGBA8UnormSrgb);
    CG_EXPECT_TRUE(TryLinearVariant(srgb, &linear));
    CG_EXPECT_EQ(linear, Format::RGBA8Unorm);
}

CG_TEST(rhi_format_table_support_rules)
{
    // Multisampling: everything uncompressed, nothing block compressed.
    CG_EXPECT_TRUE(FormatSupportsMultisample(Format::RGBA8Unorm));
    CG_EXPECT_TRUE(FormatSupportsMultisample(Format::D24UnormS8Uint));
    CG_EXPECT_FALSE(FormatSupportsMultisample(Format::BC1Unorm));
    CG_EXPECT_FALSE(FormatSupportsMultisample(Format::Count));

    // Render targets: uncompressed color only.
    CG_EXPECT_TRUE(FormatSupportsRenderTarget(Format::RGBA8UnormSrgb));
    CG_EXPECT_TRUE(FormatSupportsRenderTarget(Format::RGBA16Float));
    CG_EXPECT_TRUE(FormatSupportsRenderTarget(Format::R32Uint));
    CG_EXPECT_FALSE(FormatSupportsRenderTarget(Format::D32Float));
    CG_EXPECT_FALSE(FormatSupportsRenderTarget(Format::BC7Unorm));

    // Storage: no sRGB, no depth, no compression.
    CG_EXPECT_TRUE(FormatSupportsStorage(Format::RGBA16Float));
    CG_EXPECT_TRUE(FormatSupportsStorage(Format::R32Uint));
    CG_EXPECT_FALSE(FormatSupportsStorage(Format::RGBA8UnormSrgb));
    CG_EXPECT_FALSE(FormatSupportsStorage(Format::D32Float));
    CG_EXPECT_FALSE(FormatSupportsStorage(Format::BC3Unorm));

    // Blending: unorm and float render targets.
    CG_EXPECT_TRUE(FormatSupportsBlending(Format::RGBA8Unorm));
    CG_EXPECT_TRUE(FormatSupportsBlending(Format::RGBA16Float));
    CG_EXPECT_FALSE(FormatSupportsBlending(Format::R32Uint));
    CG_EXPECT_FALSE(FormatSupportsBlending(Format::D32Float));
    CG_EXPECT_FALSE(FormatSupportsBlending(Format::BC1Unorm));
}
