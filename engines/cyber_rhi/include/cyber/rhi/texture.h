#pragma once

#include <cyber/core/types.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

// Shape of a texture. A cube is a 2D texture with six layers per array
// element, not a separate kind of storage, so a cube view is a window onto a
// 2D array exactly as the APIs treat it.
enum class TextureDimension : u8
{
    // One-dimensional; height and depth are 1.
    Dim1D = 0,
    // Two-dimensional; depth is 1 and layers index the array.
    Dim2D,
    // Three-dimensional; array layers must be 1.
    Dim3D,
    // Cube; width equals height and the layer count is a multiple of six.
    Cube,
    // Count of enumerators, for table tests. Not a dimension.
    Count
};

// Number of real enumerators, not counting `Count`.
inline constexpr u32 kTextureDimensionCount = static_cast<u32>(TextureDimension::Count);

// Human-readable dimension, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* TextureDimensionName(TextureDimension dimension) noexcept
{
    switch (dimension)
    {
    case TextureDimension::Dim1D:
        return "1d";
    case TextureDimension::Dim2D:
        return "2d";
    case TextureDimension::Dim3D:
        return "3d";
    case TextureDimension::Cube:
        return "cube";
    case TextureDimension::Count:
        break;
    }
    return "unknown";
}

// What a texture may be used for.
enum class TextureUsage : u32
{
    // No usage. A texture created with this set is invalid.
    None = 0,
    // Sampled by a shader through a view.
    Sampled = 1U << 0,
    // Written from a shader as a storage image. Requires
    // `Feature::StorageTextures`, one sample, no mips beyond the first, and a
    // format `FormatSupportsStorage` accepts.
    Storage = 1U << 1,
    // Bound as a color attachment.
    RenderTarget = 1U << 2,
    // Bound as a depth or depth-stencil attachment.
    DepthStencil = 1U << 3,
    // Read by a copy command.
    CopySource = 1U << 4,
    // Written by a copy command.
    CopyDestination = 1U << 5,
};

// Set union.
[[nodiscard]] constexpr TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) noexcept
{
    return static_cast<TextureUsage>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

// Set intersection.
[[nodiscard]] constexpr TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) noexcept
{
    return static_cast<TextureUsage>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
}

// Set union in place.
constexpr TextureUsage& operator|=(TextureUsage& lhs, TextureUsage rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

// The raw bits, for serialization and logging.
[[nodiscard]] constexpr u32 TextureUsageBits(TextureUsage usage) noexcept
{
    return static_cast<u32>(usage);
}

// True when `set` contains `flag`. A zero `flag` is never contained.
[[nodiscard]] constexpr bool HasUsage(TextureUsage set, TextureUsage flag) noexcept
{
    return flag != TextureUsage::None && (set & flag) != TextureUsage::None;
}

// Human-readable name for a single usage flag, or "unknown" for a combination
// or a zero value. The string has static lifetime.
[[nodiscard]] constexpr const char* TextureUsageName(TextureUsage usage) noexcept
{
    switch (usage)
    {
    case TextureUsage::Sampled:
        return "sampled";
    case TextureUsage::Storage:
        return "storage";
    case TextureUsage::RenderTarget:
        return "render_target";
    case TextureUsage::DepthStencil:
        return "depth_stencil";
    case TextureUsage::CopySource:
        return "copy_source";
    case TextureUsage::CopyDestination:
        return "copy_destination";
    case TextureUsage::None:
        break;
    }
    return "unknown";
}

// Which channels of a texture a view or a barrier addresses. A depth-stencil
// format stores both; a depth-only format stores only depth, and a color format
// stores only color.
enum class TextureAspect : u8
{
    // Color channels.
    Color = 0,
    // The depth channel only.
    Depth,
    // The stencil channel only.
    Stencil,
    // Both channels of a depth-stencil format, which is the whole subresource.
    DepthStencil,
    // Count of enumerators, for table tests. Not an aspect.
    Count
};

// Number of real enumerators, not counting `Count`.
inline constexpr u32 kTextureAspectCount = static_cast<u32>(TextureAspect::Count);

// Human-readable aspect, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* TextureAspectName(TextureAspect aspect) noexcept
{
    switch (aspect)
    {
    case TextureAspect::Color:
        return "color";
    case TextureAspect::Depth:
        return "depth";
    case TextureAspect::Stencil:
        return "stencil";
    case TextureAspect::DepthStencil:
        return "depth_stencil";
    case TextureAspect::Count:
        break;
    }
    return "unknown";
}

// A rectangular window over a texture's mip chain and array layers.
//
// A zero `mipLevelCount` or `layerCount` means "every remaining level or layer
// from the base", which is Vulkan's `VK_REMAINING_MIP_LEVELS` and lets a caller
// describe a whole texture without knowing its shape. The resolved count is
// always at least one, so a zero count is never an empty range.
struct SubresourceRange
{
    u32 baseMipLevel = 0;
    // Zero means every remaining mip level.
    u32 mipLevelCount = 0;
    u32 baseArrayLayer = 0;
    // Zero means every remaining array layer.
    u32 layerCount = 0;
    // Channels the range addresses. Defaults to color, which is also the only
    // legal aspect for a color format.
    TextureAspect aspect = TextureAspect::Color;
};

// Description of a texture to create. A zeroed descriptor is invalid: no usage
// and a zero width.
struct TextureDesc
{
    TextureDimension dimension = TextureDimension::Dim2D;
    // Texel format. `Format::Count` is invalid.
    Format format = Format::RGBA8Unorm;
    // What the texture may be used for.
    TextureUsage usage = TextureUsage::None;
    // Extent in texels. Width is required; height is 1 for 1D and depth is 1
    // for 1D and 2D.
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
    // Array layers. 1 for a 3D texture, a multiple of 6 for a cube.
    u32 arrayLayers = 1;
    // Mip levels from 1 to `MaxMipLevels(dimension, width, height, depth)`.
    u32 mipLevels = 1;
    // Samples per texel: 1, 2, 4, or 8. A value above 1 requires one mip level,
    // a format that `FormatSupportsMultisample` accepts, and a shape no API
    // multisamples (not 1D, not 3D, not a cube).
    u32 sampleCount = 1;
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// What a created texture reports about itself. `valid` is false for a stale
// handle, in which case every other field is zero.
struct TextureInfo
{
    bool valid = false;
    TextureDimension dimension = TextureDimension::Dim2D;
    Format format = Format::RGBA8Unorm;
    TextureUsage usage = TextureUsage::None;
    u32 width = 0;
    u32 height = 0;
    u32 depth = 0;
    u32 arrayLayers = 0;
    u32 mipLevels = 0;
    u32 sampleCount = 1;
    // Bytes the backend reserves for the whole texture, using
    // `TextureByteSize` with the default row alignment. Backends that choose a
    // different layout report their own number.
    u64 sizeBytes = 0;
};

// Description of a view to create. A view borrows its texture and dies when the
// texture is destroyed.
struct TextureViewDesc
{
    // Texture the view reads. Must be a live handle.
    TextureHandle texture;
    // Format the view exposes. It must be view-compatible with the texture's
    // format: identical, or the sRGB/linear counterpart of the same layout.
    Format format = Format::RGBA8Unorm;
    // Shape the view exposes. A 2D texture with six layers can expose a cube
    // view; a cube texture cannot expose a 3D view.
    TextureDimension dimension = TextureDimension::Dim2D;
    // Mip levels, layers, and channels the view addresses.
    SubresourceRange range{};
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// What a created view reports about itself. `valid` is false for a stale handle.
struct TextureViewInfo
{
    bool valid = false;
    Format format = Format::RGBA8Unorm;
    TextureDimension dimension = TextureDimension::Dim2D;
    SubresourceRange range{};
};

// Largest mip count a texture of this shape can have: the count that halves
// each dimension until every one of them is 1. Returns 0 for a zero extent.
[[nodiscard]] constexpr u32 MaxMipLevels(TextureDimension dimension, u32 width, u32 height,
                                         u32 depth) noexcept
{
    // A missing dimension has no mip chain. Checking before taking the maximum
    // keeps a zero height from being masked by a non-zero width.
    if (width == 0)
    {
        return 0;
    }
    if (dimension != TextureDimension::Dim1D && height == 0)
    {
        return 0;
    }
    if (dimension == TextureDimension::Dim3D && depth == 0)
    {
        return 0;
    }

    u32 extent = width;
    if (dimension != TextureDimension::Dim1D && height > extent)
    {
        extent = height;
    }
    if (dimension == TextureDimension::Dim3D && depth > extent)
    {
        extent = depth;
    }
    u32 levels = 1;
    while (extent > 1)
    {
        extent >>= 1;
        ++levels;
    }
    return levels;
}

// Row pitch alignment `TextureByteSize` uses by default. 256 bytes is
// `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` and a safe upper bound for Vulkan's
// optimal row-pitch alignment on the targets this project supports.
inline constexpr u32 kTextureRowAlignment = 256;

namespace detail
{

[[nodiscard]] constexpr bool TryMultiplyU64(u64 a, u64 b, u64* out) noexcept
{
    if (a != 0 && b > (~u64{0}) / a)
    {
        return false;
    }
    *out = a * b;
    return true;
}

[[nodiscard]] constexpr bool TryAddU64(u64 a, u64 b, u64* out) noexcept
{
    if (b > (~u64{0}) - a)
    {
        return false;
    }
    *out = a + b;
    return true;
}

}  // namespace detail

// Bytes the whole texture needs with every row padded to `rowAlignment`,
// counting every mip level and array layer. Returns 0 when the descriptor is
// invalid or the total does not fit in 64 bits; every descriptor the limits
// accept is far below that.
[[nodiscard]] constexpr u64 TextureByteSize(const TextureDesc& desc,
                                            u32 rowAlignment = kTextureRowAlignment) noexcept
{
    const FormatInfo info = FormatInfoOf(desc.format);
    if (info.bytesPerBlock == 0 || desc.width == 0 || desc.height == 0 || desc.depth == 0 ||
        desc.mipLevels == 0 || desc.mipLevels > 32 || desc.arrayLayers == 0 ||
        info.blockWidth == 0 || info.blockHeight == 0 || rowAlignment == 0)
    {
        return 0;
    }

    u64 total = 0;
    for (u32 level = 0; level < desc.mipLevels; ++level)
    {
        u32 width = desc.width >> level;
        u32 height = desc.height >> level;
        u32 depth = desc.depth >> level;
        width = width == 0 ? 1 : width;
        height = height == 0 ? 1 : height;
        depth = depth == 0 ? 1 : depth;

        const u64 blocksWide = (width + info.blockWidth - 1) / info.blockWidth;
        const u64 blocksHigh = (height + info.blockHeight - 1) / info.blockHeight;

        u64 rowBytes = 0;
        if (!detail::TryMultiplyU64(blocksWide, info.bytesPerBlock, &rowBytes))
        {
            return 0;
        }
        rowBytes = ((rowBytes + rowAlignment - 1) / rowAlignment) * rowAlignment;

        u64 sliceBytes = 0;
        if (!detail::TryMultiplyU64(rowBytes, blocksHigh, &sliceBytes))
        {
            return 0;
        }
        if (!detail::TryMultiplyU64(sliceBytes, depth, &sliceBytes))
        {
            return 0;
        }
        if (!detail::TryMultiplyU64(sliceBytes, desc.arrayLayers, &sliceBytes))
        {
            return 0;
        }
        if (!detail::TryAddU64(total, sliceBytes, &total))
        {
            return 0;
        }
    }
    return total;
}

}  // namespace cyber::rhi
