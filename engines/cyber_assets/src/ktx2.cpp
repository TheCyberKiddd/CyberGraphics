#include "deflate.h"

#include <cyber/assets/texture.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::assets::detail::ReadBigEndian32;
using cyber::core::ErrorCode;
using cyber::core::u64;
using cyber::core::u8;

constexpr u8 kIdentifier[12] = {0xab, 0x4b, 0x54, 0x58, 0x20, 0x32,
                                0x30, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a};
constexpr usize kHeaderSize = 80;
constexpr usize kLevelIndexSize = 24;

constexpr u32 kVkR8Unorm = 9u;
constexpr u32 kVkRgba8Unorm = 37u;
constexpr u32 kVkBgra8Unorm = 44u;
constexpr u32 kVkBc1RgbUnorm = 131u;
constexpr u32 kVkBc1RgbaUnorm = 132u;
constexpr u32 kVkBc2Unorm = 135u;
constexpr u32 kVkBc3Unorm = 137u;
constexpr u32 kVkBc4Unorm = 139u;
constexpr u32 kVkBc5Unorm = 141u;
constexpr u32 kVkBc6hUfloat = 143u;
constexpr u32 kVkBc7Unorm = 145u;

[[nodiscard]] u32 ReadLittleEndian32(const Byte* data) noexcept
{
    return static_cast<u32>(std::to_integer<u8>(data[0])) |
           (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u) |
           (static_cast<u32>(std::to_integer<u8>(data[2])) << 16u) |
           (static_cast<u32>(std::to_integer<u8>(data[3])) << 24u);
}

[[nodiscard]] u64 ReadLittleEndian64(const Byte* data) noexcept
{
    const u64 low = ReadLittleEndian32(data);
    const u64 high = ReadLittleEndian32(data + 4);
    return low | (high << 32u);
}

[[nodiscard]] TextureFormat FormatFromVulkan(u32 vkFormat) noexcept
{
    switch (vkFormat)
    {
    case kVkR8Unorm:
        return TextureFormat::R8Unorm;
    case kVkRgba8Unorm:
        return TextureFormat::Rgba8Unorm;
    case kVkBgra8Unorm:
        return TextureFormat::Bgra8Unorm;
    case kVkBc1RgbUnorm:
    case kVkBc1RgbaUnorm:
        return TextureFormat::Bc1Rgba;
    case kVkBc2Unorm:
        return TextureFormat::Bc2Rgba;
    case kVkBc3Unorm:
        return TextureFormat::Bc3Rgba;
    case kVkBc4Unorm:
        return TextureFormat::Bc4R;
    case kVkBc5Unorm:
        return TextureFormat::Bc5Rg;
    case kVkBc6hUfloat:
        return TextureFormat::Bc6hRgb;
    case kVkBc7Unorm:
        return TextureFormat::Bc7Rgba;
    default:
        return TextureFormat::Unknown;
    }
}

}  // namespace

ImportResult<TextureAsset> ParseKtx2(Span<const Byte> bytes, Allocator& allocator) noexcept
{
    if (bytes.Size() < kHeaderSize)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "KTX2 file is smaller than its header", 0));
    }
    if (std::memcmp(bytes.Data(), kIdentifier, sizeof(kIdentifier)) != 0)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "KTX2 identifier is missing", 0));
    }

    const u32 vkFormat = ReadLittleEndian32(bytes.Data() + 12);
    const u32 typeSize = ReadLittleEndian32(bytes.Data() + 16);
    const u32 pixelWidth = ReadLittleEndian32(bytes.Data() + 20);
    const u32 pixelHeight = ReadLittleEndian32(bytes.Data() + 24);
    const u32 pixelDepth = ReadLittleEndian32(bytes.Data() + 28);
    const u32 layerCount = ReadLittleEndian32(bytes.Data() + 32);
    const u32 faceCount = ReadLittleEndian32(bytes.Data() + 36);
    u32 levelCount = ReadLittleEndian32(bytes.Data() + 40);
    const u32 supercompression = ReadLittleEndian32(bytes.Data() + 44);

    if (supercompression != 0)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::Unsupported, "KTX2 supercompression is not supported", 44));
    }
    if (typeSize != 1u)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::Unsupported, "KTX2 type size is not 1", 16));
    }
    if (pixelDepth > 1)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::Unsupported, "KTX2 3D textures are not supported", 28));
    }
    if (pixelWidth == 0 || pixelHeight == 0 || pixelWidth > 32768 || pixelHeight > 32768)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::OutOfRange, "KTX2 dimensions are unsupported", 20));
    }
    if (faceCount != 1u && faceCount != 6u)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "KTX2 face count is invalid", 36));
    }
    if (levelCount == 0)
    {
        levelCount = 1;
    }
    if (levelCount > 32)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::OutOfRange, "KTX2 level count is invalid", 40));
    }

    const TextureFormat format = FormatFromVulkan(vkFormat);
    if (format == TextureFormat::Unknown)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::Unsupported, "KTX2 Vulkan format is not supported", 12));
    }

    const u32 surfaces = (layerCount == 0 ? 1u : layerCount) * faceCount;
    if (surfaces > 4096)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::OutOfRange, "KTX2 surface count is invalid", 32));
    }

    if (levelCount > (bytes.Size() - kHeaderSize) / kLevelIndexSize)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "KTX2 level index is truncated", kHeaderSize));
    }

    TextureAsset texture(allocator);
    texture.format = format;
    texture.width = pixelWidth;
    texture.height = pixelHeight;
    texture.depth = 1;
    texture.mipCount = levelCount;
    texture.layerCount = surfaces;
    texture.cubemap = faceCount == 6u;

    u32 levelWidth = pixelWidth;
    u32 levelHeight = pixelHeight;
    for (u32 level = 0; level < levelCount; ++level)
    {
        const usize entry = kHeaderSize + static_cast<usize>(level) * kLevelIndexSize;
        const u64 byteOffset = ReadLittleEndian64(bytes.Data() + entry);
        const u64 byteLength = ReadLittleEndian64(bytes.Data() + entry + 8);

        if (byteOffset < kHeaderSize + static_cast<u64>(levelCount) * kLevelIndexSize ||
            byteOffset > bytes.Size() || byteLength > bytes.Size() - byteOffset)
        {
            return ImportResult<TextureAsset>::Err(MakeOffsetError(
                ErrorCode::ParseError, "KTX2 level range is outside the file", entry));
        }

        u32 rowPitch = 0;
        auto surfaceSize = TextureLevelSize(format, levelWidth, levelHeight, rowPitch);
        if (surfaceSize.IsError())
        {
            return ImportResult<TextureAsset>::Err(surfaceSize.Error());
        }
        const u64 expectedSize = surfaceSize.Value() * surfaces;
        if (byteLength != expectedSize)
        {
            return ImportResult<TextureAsset>::Err(MakeOffsetError(
                ErrorCode::ParseError, "KTX2 level byte length is inconsistent", entry + 8));
        }

        TextureLevel record;
        record.offset = texture.data.Size();
        record.size = byteLength;
        record.width = levelWidth;
        record.height = levelHeight;
        record.rowPitch = rowPitch;
        if (!texture.levels.TryPushBack(record))
        {
            return ImportResult<TextureAsset>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory for KTX2 level table"));
        }

        const usize previous = texture.data.Size();
        if (!texture.data.TryResize(previous + static_cast<usize>(byteLength)))
        {
            return ImportResult<TextureAsset>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory for KTX2 payload"));
        }
        if (byteLength > 0)
        {
            std::memcpy(texture.data.Data() + previous, bytes.Data() + byteOffset,
                        static_cast<usize>(byteLength));
        }

        levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
        levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
    }

    auto validated = texture.Validate();
    if (validated.IsError())
    {
        return ImportResult<TextureAsset>::Err(validated.Error());
    }
    return ImportResult<TextureAsset>::Ok(std::move(texture));
}

}  // namespace cyber::assets
