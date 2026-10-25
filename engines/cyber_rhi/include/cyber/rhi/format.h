#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;

// Resource pixel formats. The list is the subset the renderer needs through
// Phase 15, and it is closed on purpose: a format is added when a phase's design
// note names the use, not on demand from a backend.
//
// Names follow the D3D12/Vulkan convention: channel letters, then the component
// type, then "Srgb" when the format applies the sRGB transfer function on read
// and the inverse on write. Storage is always linear in the RHI's terms; an sRGB
// format tells the hardware to convert, so a shader sees linear values.
//
// The numeric table (bytes per texel, block size, channel count) lives in
// Phase 11's `docs/formats.md`; this header only classifies.
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

// Enumerators a format has, `Format::Count` included.
inline constexpr u32 kFormatCount = static_cast<u32>(Format::Count);

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

// True when a format may be bound as a depth attachment or sampled as depth.
[[nodiscard]] constexpr bool IsDepthFormat(Format format) noexcept
{
    switch (format)
    {
    case Format::D16Unorm:
    case Format::D32Float:
    case Format::D24UnormS8Uint:
    case Format::D32FloatS8Uint:
        return true;
    default:
        return false;
    }
}

// True when a depth format also carries stencil. A backend maps these to its
// combined depth-stencil formats so one attachment serves both.
[[nodiscard]] constexpr bool IsStencilFormat(Format format) noexcept
{
    return format == Format::D24UnormS8Uint || format == Format::D32FloatS8Uint;
}

// True when the format converts through the sRGB transfer function. A color
// attachment, sample, or swapchain format with this flag is encoded, not linear.
[[nodiscard]] constexpr bool IsSrgbFormat(Format format) noexcept
{
    switch (format)
    {
    case Format::RGBA8UnormSrgb:
    case Format::BGRA8UnormSrgb:
    case Format::BC1UnormSrgb:
    case Format::BC3UnormSrgb:
    case Format::BC7UnormSrgb:
        return true;
    default:
        return false;
    }
}

// True for a block-compressed color format. Compressed formats cannot be render
// targets, cannot be written from a shader, and their rows are 4x4 texel blocks.
[[nodiscard]] constexpr bool IsBlockCompressedFormat(Format format) noexcept
{
    switch (format)
    {
    case Format::BC1Unorm:
    case Format::BC1UnormSrgb:
    case Format::BC3Unorm:
    case Format::BC3UnormSrgb:
    case Format::BC5Unorm:
    case Format::BC7Unorm:
    case Format::BC7UnormSrgb:
        return true;
    default:
        return false;
    }
}

// True when a 32-bit integer format stores unsigned integers rather than floats.
// Integer targets need an integer clear value and cannot be blended with floats.
[[nodiscard]] constexpr bool IsUnsignedIntegerFormat(Format format) noexcept
{
    switch (format)
    {
    case Format::R32Uint:
    case Format::RG32Uint:
    case Format::RGBA32Uint:
        return true;
    default:
        return false;
    }
}

}  // namespace cyber::rhi
