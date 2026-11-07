#pragma once

#include <cyber/assets/asset.h>
#include <cyber/assets/image.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>

#include <utility>

namespace cyber::assets
{

using cyber::core::Byte;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::Span;
using cyber::memory::StringView;

/// Texture payload formats the containers can carry. Block-compressed payloads
/// stay compressed; the renderer decides how to upload them.
enum class TextureFormat : u16
{
    Unknown = 0,
    Rgba8Unorm = 1,
    Bgra8Unorm = 2,
    R8Unorm = 3,
    Bc1Rgba = 4,
    Bc2Rgba = 5,
    Bc3Rgba = 6,
    Bc4R = 7,
    Bc5Rg = 8,
    Bc6hRgb = 9,
    Bc7Rgba = 10,
};

[[nodiscard]] constexpr bool IsBlockCompressed(TextureFormat format) noexcept
{
    return format == TextureFormat::Bc1Rgba || format == TextureFormat::Bc2Rgba ||
           format == TextureFormat::Bc3Rgba || format == TextureFormat::Bc4R ||
           format == TextureFormat::Bc5Rg || format == TextureFormat::Bc6hRgb ||
           format == TextureFormat::Bc7Rgba;
}

/// Bytes per 4x4 block, or per pixel for uncompressed formats.
[[nodiscard]] constexpr u32 TextureBlockBytes(TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::Rgba8Unorm:
    case TextureFormat::Bgra8Unorm:
        return 4;
    case TextureFormat::R8Unorm:
        return 1;
    case TextureFormat::Bc1Rgba:
    case TextureFormat::Bc4R:
        return 8;
    case TextureFormat::Bc2Rgba:
    case TextureFormat::Bc3Rgba:
    case TextureFormat::Bc5Rg:
    case TextureFormat::Bc6hRgb:
    case TextureFormat::Bc7Rgba:
        return 16;
    case TextureFormat::Unknown:
        return 0;
    }
    return 0;
}

/// Number of 4x4 blocks covering a dimension. Every compressed format rounds up.
[[nodiscard]] constexpr u32 TextureBlocksAcross(u32 dimension) noexcept
{
    return (dimension + 3u) / 4u;
}

[[nodiscard]] constexpr const char* TextureFormatName(TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::Unknown:
        return "unknown";
    case TextureFormat::Rgba8Unorm:
        return "rgba8";
    case TextureFormat::Bgra8Unorm:
        return "bgra8";
    case TextureFormat::R8Unorm:
        return "r8";
    case TextureFormat::Bc1Rgba:
        return "bc1";
    case TextureFormat::Bc2Rgba:
        return "bc2";
    case TextureFormat::Bc3Rgba:
        return "bc3";
    case TextureFormat::Bc4R:
        return "bc4";
    case TextureFormat::Bc5Rg:
        return "bc5";
    case TextureFormat::Bc6hRgb:
        return "bc6h";
    case TextureFormat::Bc7Rgba:
        return "bc7";
    }
    return "unknown";
}

/// One mip level inside `TextureAsset::data`. Offsets are relative to the first
/// payload byte and 16-byte aligned.
struct TextureLevel
{
    u64 offset = 0;
    u64 size = 0;
    u32 width = 0;
    u32 height = 0;
    u32 rowPitch = 0;
};

/// Imported texture. The payload is copied out of the container so the source
/// buffer can be released; levels describe the copy.
class TextureAsset
{
public:
    explicit TextureAsset(Allocator& allocator) noexcept
        : data(allocator), levels(allocator), name(allocator)
    {
    }

    TextureAsset(const TextureAsset&) = delete;
    TextureAsset& operator=(const TextureAsset&) = delete;
    TextureAsset(TextureAsset&&) noexcept = default;
    TextureAsset& operator=(TextureAsset&&) noexcept = default;
    ~TextureAsset() = default;

    TextureFormat format = TextureFormat::Unknown;
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
    u32 mipCount = 1;
    u32 layerCount = 1;
    bool cubemap = false;
    DynamicArray<Byte> data;
    DynamicArray<TextureLevel> levels;
    cyber::memory::String name;

    /// Checks level ranges, alignment, and byte counts against the format.
    [[nodiscard]] ImportStatus Validate() const noexcept;
};

/// Byte size and row pitch of one mip level for one surface. Block-compressed
/// levels are measured in blocks, uncompressed ones in pixels.
[[nodiscard]] ImportResult<u64> TextureLevelSize(TextureFormat format, u32 width, u32 height,
                                                 u32& rowPitch) noexcept;

/// Copies a decoded image into a single-level RGBA8 texture. Cookers and
/// runtimes both need this conversion, so it lives beside the formats instead of
/// being repeated at each call site.
[[nodiscard]] ImportResult<TextureAsset> TextureFromImage(const Image& image, StringView name,
                                                          Allocator& allocator) noexcept;

/// Reads a DDS container, including the DX10 extension header. Supports
/// uncompressed RGBA8/BGRA8/R8 and BC1, BC2, BC3, BC4, BC5, BC6H, and BC7.
[[nodiscard]] ImportResult<TextureAsset> ParseDds(Span<const Byte> bytes,
                                                  Allocator& allocator) noexcept;

/// Reads a KTX2 container with `supercompressionScheme == 0`. Supports the same
/// formats plus the level index, cubemaps, and array layers.
[[nodiscard]] ImportResult<TextureAsset> ParseKtx2(Span<const Byte> bytes,
                                                   Allocator& allocator) noexcept;

}  // namespace cyber::assets
