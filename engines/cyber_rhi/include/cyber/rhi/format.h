#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u8;

// Resource pixel formats. The list is the subset the renderer needs through
// Phase 15, and it is closed on purpose: a format is added when a phase's design
// note names the use, not on demand from a backend.
//
// Names follow the D3D12/Vulkan convention: channel letters, then the component
// type, then "Srgb" when the format applies the sRGB transfer function on read
// and the inverse on write. Storage is always linear in the RHI's terms; an sRGB
// format tells the hardware to convert, so a shader sees linear values.
//
// The numeric rows (bytes per block, block extent, channel count, component
// type) are the table in `FormatInfoOf` below and the rendered copy in
// `docs/formats.md`. A test walks every enumerator, so a new format with no row
// fails the test step instead of a frame.
enum class Format : u32
{
    // 8-bit unorm color.
    R8Unorm = 0,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8UnormSrgb,
    // 8-bit unorm color with the blue and red channels swapped in memory, which
    // is what a Windows desktop surface usually is.
    BGRA8Unorm,
    BGRA8UnormSrgb,
    // 16-bit float color.
    R16Float,
    RG16Float,
    RGBA16Float,
    // 32-bit float color.
    R32Float,
    RG32Float,
    RGBA32Float,
    // 32-bit unsigned integer, for ids and counters.
    R32Uint,
    RG32Uint,
    RGBA32Uint,
    // Packed 32-bit color.
    R10G10B10A2Unorm,
    RG11B10Float,
    // Depth and depth-stencil. Depth is in the 0..1 range the RHI requires.
    D16Unorm,
    D32Float,
    D24UnormS8Uint,
    D32FloatS8Uint,
    // Block-compressed color. Block size is 4x4 texels for every BC format.
    BC1Unorm,
    BC1UnormSrgb,
    BC3Unorm,
    BC3UnormSrgb,
    BC5Unorm,
    BC7Unorm,
    BC7UnormSrgb,

    // Count of enumerators, for table tests. Not a format.
    Count
};

// Number of real formats, not counting `Format::Count`: the last real
// enumerator is `kFormatCount - 1` and `Format::Count` equals it plus one.
inline constexpr u32 kFormatCount = static_cast<u32>(Format::Count);

// How one channel of a format is interpreted. `DepthStencil` marks a format
// whose channels are one depth value and one stencil value, which no shader
// reads as a numeric vector.
enum class FormatComponentType : u8
{
    // Unsigned normalized integer, mapped to 0..1 on read.
    Unorm = 0,
    // IEEE 754 float, 16 or 32 bits.
    Float,
    // Unsigned integer, read unchanged.
    Uint,
    // Unsigned normalized depth in 0..1, reverse-Z at the RHI boundary.
    Depth,
    // A depth channel plus an 8-bit stencil channel.
    DepthStencil,
};

// Everything the RHI needs to know about one format. A value type; the default
// is the zeroed row, which never describes a real format.
struct FormatInfo
{
    // Bytes for one texel, or for one whole block when `compressed` is true.
    u32 bytesPerBlock = 0;
    // Texels per block in each direction: 1 for uncompressed, 4 for BC.
    u32 blockWidth = 1;
    u32 blockHeight = 1;
    // Channels the format stores, counting depth and stencil as channels.
    u32 channelCount = 0;
    // How a channel is interpreted.
    FormatComponentType componentType = FormatComponentType::Unorm;
    // The format carries a depth channel.
    bool depth = false;
    // The format carries a stencil channel.
    bool stencil = false;
    // The format applies the sRGB transfer function on read and write.
    bool srgb = false;
    // The format is block compressed, so its extent is in 4x4 blocks.
    bool compressed = false;
};

namespace detail
{

// Helper constructors for the format table. They exist so each row reads as the
// three numbers that actually differ, instead of nine positional fields.
[[nodiscard]] constexpr FormatInfo ColorRow(u32 bytesPerBlock, u32 channelCount,
                                            FormatComponentType type) noexcept
{
    FormatInfo info;
    info.bytesPerBlock = bytesPerBlock;
    info.channelCount = channelCount;
    info.componentType = type;
    return info;
}

[[nodiscard]] constexpr FormatInfo SrgbRow(u32 bytesPerBlock, u32 channelCount) noexcept
{
    FormatInfo info = ColorRow(bytesPerBlock, channelCount, FormatComponentType::Unorm);
    info.srgb = true;
    return info;
}

[[nodiscard]] constexpr FormatInfo BlockRow(u32 bytesPerBlock, u32 channelCount, bool srgb) noexcept
{
    FormatInfo info = ColorRow(bytesPerBlock, channelCount, FormatComponentType::Float);
    info.blockWidth = 4;
    info.blockHeight = 4;
    info.compressed = true;
    info.srgb = srgb;
    return info;
}

[[nodiscard]] constexpr FormatInfo DepthRow(u32 bytesPerBlock, u32 channelCount,
                                            bool stencil) noexcept
{
    FormatInfo info;
    info.bytesPerBlock = bytesPerBlock;
    info.channelCount = channelCount;
    info.componentType = stencil ? FormatComponentType::DepthStencil : FormatComponentType::Depth;
    info.depth = true;
    info.stencil = stencil;
    return info;
}

}  // namespace detail

// The table. Every classification predicate in this header is a projection of
// this function, so a format's byte size and its depth-ness cannot disagree.
// An out-of-range value returns the zeroed row, which classifies as nothing.
[[nodiscard]] constexpr FormatInfo FormatInfoOf(Format format) noexcept
{
    switch (format)
    {
    case Format::R8Unorm:
        return detail::ColorRow(1, 1, FormatComponentType::Unorm);
    case Format::RG8Unorm:
        return detail::ColorRow(2, 2, FormatComponentType::Unorm);
    case Format::RGBA8Unorm:
        return detail::ColorRow(4, 4, FormatComponentType::Unorm);
    case Format::RGBA8UnormSrgb:
        return detail::SrgbRow(4, 4);
    case Format::BGRA8Unorm:
        return detail::ColorRow(4, 4, FormatComponentType::Unorm);
    case Format::BGRA8UnormSrgb:
        return detail::SrgbRow(4, 4);
    case Format::R16Float:
        return detail::ColorRow(2, 1, FormatComponentType::Float);
    case Format::RG16Float:
        return detail::ColorRow(4, 2, FormatComponentType::Float);
    case Format::RGBA16Float:
        return detail::ColorRow(8, 4, FormatComponentType::Float);
    case Format::R32Float:
        return detail::ColorRow(4, 1, FormatComponentType::Float);
    case Format::RG32Float:
        return detail::ColorRow(8, 2, FormatComponentType::Float);
    case Format::RGBA32Float:
        return detail::ColorRow(16, 4, FormatComponentType::Float);
    case Format::R32Uint:
        return detail::ColorRow(4, 1, FormatComponentType::Uint);
    case Format::RG32Uint:
        return detail::ColorRow(8, 2, FormatComponentType::Uint);
    case Format::RGBA32Uint:
        return detail::ColorRow(16, 4, FormatComponentType::Uint);
    case Format::R10G10B10A2Unorm:
        return detail::ColorRow(4, 4, FormatComponentType::Unorm);
    case Format::RG11B10Float:
        return detail::ColorRow(4, 3, FormatComponentType::Float);
    case Format::D16Unorm:
        return detail::DepthRow(2, 1, false);
    case Format::D32Float:
        return detail::DepthRow(4, 1, false);
    case Format::D24UnormS8Uint:
        return detail::DepthRow(4, 2, true);
    case Format::D32FloatS8Uint:
        return detail::DepthRow(8, 2, true);
    case Format::BC1Unorm:
        return detail::BlockRow(8, 4, false);
    case Format::BC1UnormSrgb:
        return detail::BlockRow(8, 4, true);
    case Format::BC3Unorm:
        return detail::BlockRow(16, 4, false);
    case Format::BC3UnormSrgb:
        return detail::BlockRow(16, 4, true);
    case Format::BC5Unorm:
        return detail::BlockRow(16, 2, false);
    case Format::BC7Unorm:
        return detail::BlockRow(16, 4, false);
    case Format::BC7UnormSrgb:
        return detail::BlockRow(16, 4, true);
    case Format::Count:
        break;
    }
    return FormatInfo{};
}

// Human-readable name, or "unknown" for an out-of-range value. The string has
// static lifetime.
[[nodiscard]] constexpr const char* FormatName(Format format) noexcept
{
    switch (format)
    {
    case Format::R8Unorm:
        return "R8Unorm";
    case Format::RG8Unorm:
        return "RG8Unorm";
    case Format::RGBA8Unorm:
        return "RGBA8Unorm";
    case Format::RGBA8UnormSrgb:
        return "RGBA8UnormSrgb";
    case Format::BGRA8Unorm:
        return "BGRA8Unorm";
    case Format::BGRA8UnormSrgb:
        return "BGRA8UnormSrgb";
    case Format::R16Float:
        return "R16Float";
    case Format::RG16Float:
        return "RG16Float";
    case Format::RGBA16Float:
        return "RGBA16Float";
    case Format::R32Float:
        return "R32Float";
    case Format::RG32Float:
        return "RG32Float";
    case Format::RGBA32Float:
        return "RGBA32Float";
    case Format::R32Uint:
        return "R32Uint";
    case Format::RG32Uint:
        return "RG32Uint";
    case Format::RGBA32Uint:
        return "RGBA32Uint";
    case Format::R10G10B10A2Unorm:
        return "R10G10B10A2Unorm";
    case Format::RG11B10Float:
        return "RG11B10Float";
    case Format::D16Unorm:
        return "D16Unorm";
    case Format::D32Float:
        return "D32Float";
    case Format::D24UnormS8Uint:
        return "D24UnormS8Uint";
    case Format::D32FloatS8Uint:
        return "D32FloatS8Uint";
    case Format::BC1Unorm:
        return "BC1Unorm";
    case Format::BC1UnormSrgb:
        return "BC1UnormSrgb";
    case Format::BC3Unorm:
        return "BC3Unorm";
    case Format::BC3UnormSrgb:
        return "BC3UnormSrgb";
    case Format::BC5Unorm:
        return "BC5Unorm";
    case Format::BC7Unorm:
        return "BC7Unorm";
    case Format::BC7UnormSrgb:
        return "BC7UnormSrgb";
    case Format::Count:
        break;
    }
    return "unknown";
}

// Human-readable component type, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* FormatComponentTypeName(FormatComponentType type) noexcept
{
    switch (type)
    {
    case FormatComponentType::Unorm:
        return "unorm";
    case FormatComponentType::Float:
        return "float";
    case FormatComponentType::Uint:
        return "uint";
    case FormatComponentType::Depth:
        return "depth";
    case FormatComponentType::DepthStencil:
        return "depth_stencil";
    }
    return "unknown";
}

// Bytes a format needs for one texel, or for one whole block when the format is
// block compressed. Zero for an out-of-range format.
[[nodiscard]] constexpr u32 FormatBytesPerBlock(Format format) noexcept
{
    return FormatInfoOf(format).bytesPerBlock;
}

// Texels per block horizontally: 1 for uncompressed, 4 for a BC format.
[[nodiscard]] constexpr u32 FormatBlockWidth(Format format) noexcept
{
    return FormatInfoOf(format).blockWidth;
}

// Texels per block vertically: 1 for uncompressed, 4 for a BC format.
[[nodiscard]] constexpr u32 FormatBlockHeight(Format format) noexcept
{
    return FormatInfoOf(format).blockHeight;
}

// Channels the format stores, counting depth and stencil as channels.
[[nodiscard]] constexpr u32 FormatChannelCount(Format format) noexcept
{
    return FormatInfoOf(format).channelCount;
}

// How the format's channels are interpreted.
[[nodiscard]] constexpr FormatComponentType FormatComponentTypeOf(Format format) noexcept
{
    return FormatInfoOf(format).componentType;
}

// True when a format may be bound as a depth attachment or sampled as depth.
[[nodiscard]] constexpr bool IsDepthFormat(Format format) noexcept
{
    return FormatInfoOf(format).depth;
}

// True when a depth format also carries stencil. A backend maps these to its
// combined depth-stencil formats so one attachment serves both.
[[nodiscard]] constexpr bool IsStencilFormat(Format format) noexcept
{
    return FormatInfoOf(format).stencil;
}

// True when a format carries neither depth nor stencil, so it can be used as a
// color attachment or a color sample.
[[nodiscard]] constexpr bool IsColorFormat(Format format) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    return info.bytesPerBlock != 0 && !info.depth && !info.stencil;
}

// True when the format converts through the sRGB transfer function. A color
// attachment, sample, or swapchain format with this flag is encoded, not linear.
[[nodiscard]] constexpr bool IsSrgbFormat(Format format) noexcept
{
    return FormatInfoOf(format).srgb;
}

// True for a block-compressed color format. Compressed formats cannot be render
// targets, cannot be written from a shader, and their rows are 4x4 texel blocks.
[[nodiscard]] constexpr bool IsBlockCompressedFormat(Format format) noexcept
{
    return FormatInfoOf(format).compressed;
}

// True when a format stores unsigned integers rather than normalized or float
// values. Integer targets need an integer clear value and cannot be blended.
[[nodiscard]] constexpr bool IsUnsignedIntegerFormat(Format format) noexcept
{
    return FormatComponentTypeOf(format) == FormatComponentType::Uint;
}

// True when the format may be a multisampled texture. Block-compressed formats
// cannot, because the block is the smallest unit and it has no per-sample
// representation.
[[nodiscard]] constexpr bool FormatSupportsMultisample(Format format) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    return info.bytesPerBlock != 0 && !info.compressed;
}

// True when the format may be a color render target.
[[nodiscard]] constexpr bool FormatSupportsRenderTarget(Format format) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    return info.bytesPerBlock != 0 && !info.compressed && !info.depth && !info.stencil;
}

// True when the format may be written from a shader as a storage texture. sRGB
// formats are excluded because a storage write is a raw value, not a color to
// encode; a shader that wants encoded output writes linear unorm and lets the
// render target encode it.
[[nodiscard]] constexpr bool FormatSupportsStorage(Format format) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    return info.bytesPerBlock != 0 && !info.compressed && !info.depth && !info.stencil &&
           !info.srgb;
}

// True when a blend equation may read and write this format. Integer formats
// need an integer blend path that Phase 12 adds; until then they are excluded.
[[nodiscard]] constexpr bool FormatSupportsBlending(Format format) noexcept
{
    const FormatComponentType type = FormatComponentTypeOf(format);
    return FormatSupportsRenderTarget(format) &&
           (type == FormatComponentType::Unorm || type == FormatComponentType::Float);
}

// True when a view may use `viewFormat` over a texture created with
// `baseFormat`. The two must agree on layout, block size, channel count, and
// component type; the only permitted difference is the sRGB encoding, which
// D3D12 and Vulkan both allow to differ between a resource and its view.
//
// Depth-stencil reinterpretation is not permitted here: a texture created as
// D24UnormS8Uint has a D24UnormS8Uint view. Phase 14 adds typeless formats when
// an adapter's feature support can be read.
[[nodiscard]] constexpr bool IsFormatViewCompatible(Format viewFormat, Format baseFormat) noexcept
{
    if (viewFormat == baseFormat)
    {
        return viewFormat != Format::Count;
    }
    const FormatInfo base = FormatInfoOf(baseFormat);
    const FormatInfo view = FormatInfoOf(viewFormat);
    if (base.bytesPerBlock == 0 || view.bytesPerBlock == 0)
    {
        return false;
    }
    if (base.depth != view.depth || base.stencil != view.stencil)
    {
        return false;
    }
    if (base.compressed != view.compressed || base.bytesPerBlock != view.bytesPerBlock ||
        base.channelCount != view.channelCount || base.componentType != view.componentType)
    {
        return false;
    }
    // Layout agrees; the encodings must differ, otherwise the equality check
    // above would have returned already.
    return base.srgb != view.srgb;
}

// Finds the sRGB-encoded format with the same layout as `linearFormat`. Returns
// true and writes `*out` when one exists, false otherwise (including for a
// format that is already sRGB). `out` must be non-null.
[[nodiscard]] constexpr bool TrySrgbVariant(Format linearFormat, Format* out) noexcept
{
    if (out == nullptr)
    {
        return false;
    }
    switch (linearFormat)
    {
    case Format::RGBA8Unorm:
        *out = Format::RGBA8UnormSrgb;
        return true;
    case Format::BGRA8Unorm:
        *out = Format::BGRA8UnormSrgb;
        return true;
    case Format::BC1Unorm:
        *out = Format::BC1UnormSrgb;
        return true;
    case Format::BC3Unorm:
        *out = Format::BC3UnormSrgb;
        return true;
    case Format::BC7Unorm:
        *out = Format::BC7UnormSrgb;
        return true;
    default:
        return false;
    }
}

// Finds the linear format with the same layout as `srgbFormat`. Returns true and
// writes `*out` when one exists, false otherwise (including for a format that is
// already linear). `out` must be non-null.
[[nodiscard]] constexpr bool TryLinearVariant(Format srgbFormat, Format* out) noexcept
{
    if (out == nullptr)
    {
        return false;
    }
    switch (srgbFormat)
    {
    case Format::RGBA8UnormSrgb:
        *out = Format::RGBA8Unorm;
        return true;
    case Format::BGRA8UnormSrgb:
        *out = Format::BGRA8Unorm;
        return true;
    case Format::BC1UnormSrgb:
        *out = Format::BC1Unorm;
        return true;
    case Format::BC3UnormSrgb:
        *out = Format::BC3Unorm;
        return true;
    case Format::BC7UnormSrgb:
        *out = Format::BC7Unorm;
        return true;
    default:
        return false;
    }
}

}  // namespace cyber::rhi
