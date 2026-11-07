#include "deflate.h"

#include <cyber/assets/texture.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::assets::detail::ReadBigEndian32;
using cyber::core::ErrorCode;
using cyber::core::u8;

constexpr u32 kDdsMagic = 0x20534444u;  // "DDS "
constexpr u32 kHeaderSize = 124;
constexpr u32 kPixelFormatSize = 32;

constexpr u32 kFlagCaps = 0x1u;
constexpr u32 kFlagHeight = 0x2u;
constexpr u32 kFlagWidth = 0x4u;
constexpr u32 kFlagPixelFormat = 0x1000u;
constexpr u32 kFlagMipMapCount = 0x20000u;
constexpr u32 kFlagLinearSize = 0x80000u;

constexpr u32 kCaps2Cubemap = 0x200u;
constexpr u32 kCaps2AllFaces = 0xfc00u;

constexpr u32 kPixelFormatFourCc = 0x4u;
constexpr u32 kPixelFormatRgb = 0x40u;
constexpr u32 kPixelFormatLuminance = 0x20000u;

constexpr u32 kFourCcDxt1 = 0x31545844u;
constexpr u32 kFourCcDxt3 = 0x33545844u;
constexpr u32 kFourCcDxt5 = 0x35545844u;
constexpr u32 kFourCcAti1 = 0x31495441u;
constexpr u32 kFourCcBc4U = 0x55344342u;
constexpr u32 kFourCcBc5U = 0x55354342u;
constexpr u32 kFourCcAti2 = 0x32495441u;
constexpr u32 kFourCcDx10 = 0x30315844u;

constexpr u32 kDxgiR8Unorm = 61u;
constexpr u32 kDxgiRgba8Unorm = 28u;
constexpr u32 kDxgiBgra8Unorm = 87u;
constexpr u32 kDxgiBc1Unorm = 71u;
constexpr u32 kDxgiBc2Unorm = 74u;
constexpr u32 kDxgiBc3Unorm = 77u;
constexpr u32 kDxgiBc4Unorm = 80u;
constexpr u32 kDxgiBc5Unorm = 83u;
constexpr u32 kDxgiBc6hUf16 = 95u;
constexpr u32 kDxgiBc7Unorm = 98u;

[[nodiscard]] u32 ReadLittleEndian32(const Byte* data) noexcept
{
    return static_cast<u32>(std::to_integer<u8>(data[0])) |
           (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u) |
           (static_cast<u32>(std::to_integer<u8>(data[2])) << 16u) |
           (static_cast<u32>(std::to_integer<u8>(data[3])) << 24u);
}

[[nodiscard]] TextureFormat FormatFromFourCc(u32 fourCc) noexcept
{
    switch (fourCc)
    {
    case kFourCcDxt1:
        return TextureFormat::Bc1Rgba;
    case kFourCcDxt3:
        return TextureFormat::Bc2Rgba;
    case kFourCcDxt5:
        return TextureFormat::Bc3Rgba;
    case kFourCcAti1:
    case kFourCcBc4U:
        return TextureFormat::Bc4R;
    case kFourCcAti2:
    case kFourCcBc5U:
        return TextureFormat::Bc5Rg;
    default:
        return TextureFormat::Unknown;
    }
}

[[nodiscard]] TextureFormat FormatFromDxgi(u32 dxgiFormat) noexcept
{
    switch (dxgiFormat)
    {
    case kDxgiR8Unorm:
        return TextureFormat::R8Unorm;
    case kDxgiRgba8Unorm:
        return TextureFormat::Rgba8Unorm;
    case kDxgiBgra8Unorm:
        return TextureFormat::Bgra8Unorm;
    case kDxgiBc1Unorm:
        return TextureFormat::Bc1Rgba;
    case kDxgiBc2Unorm:
        return TextureFormat::Bc2Rgba;
    case kDxgiBc3Unorm:
        return TextureFormat::Bc3Rgba;
    case kDxgiBc4Unorm:
        return TextureFormat::Bc4R;
    case kDxgiBc5Unorm:
        return TextureFormat::Bc5Rg;
    case kDxgiBc6hUf16:
        return TextureFormat::Bc6hRgb;
    case kDxgiBc7Unorm:
        return TextureFormat::Bc7Rgba;
    default:
        return TextureFormat::Unknown;
    }
}

/// Copies one contiguous source range into the destination at `targetOffset`,
/// validating the source range first.
[[nodiscard]] bool CopyRange(const Byte* source, usize sourceSize, u64 sourceOffset, u64 size,
                             DynamicArray<Byte>& destination, u64 targetOffset) noexcept
{
    if (sourceOffset > sourceSize || size > sourceSize - sourceOffset ||
        targetOffset > destination.Size() || size > destination.Size() - targetOffset)
    {
        return false;
    }
    if (size == 0)
    {
        return true;
    }
    std::memcpy(destination.Data() + targetOffset, source + sourceOffset, static_cast<usize>(size));
    return true;
}

}  // namespace

ImportResult<TextureAsset> ParseDds(Span<const Byte> bytes, Allocator& allocator) noexcept
{
    if (bytes.Size() < 4 + kHeaderSize)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "DDS file is smaller than its header", 0));
    }
    if (ReadLittleEndian32(bytes.Data()) != kDdsMagic)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "DDS magic value is missing", 0));
    }
    const Byte* header = bytes.Data() + 4;
    if (ReadLittleEndian32(header) != kHeaderSize)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "DDS header size is not 124", 4));
    }

    const u32 flags = ReadLittleEndian32(header + 4);
    const u32 height = ReadLittleEndian32(header + 8);
    const u32 width = ReadLittleEndian32(header + 12);
    const u32 depth = ReadLittleEndian32(header + 20);
    const u32 mipMapCount = ReadLittleEndian32(header + 24);
    const u32 pixelFormatOffset = 72;
    const u32 pixelFormatSize = ReadLittleEndian32(header + pixelFormatOffset);
    const u32 pixelFormatFlags = ReadLittleEndian32(header + pixelFormatOffset + 4);
    const u32 fourCc = ReadLittleEndian32(header + pixelFormatOffset + 8);
    const u32 rgbBitCount = ReadLittleEndian32(header + pixelFormatOffset + 12);
    const u32 rgbAlphaMask = ReadLittleEndian32(header + pixelFormatOffset + 28);
    const u32 caps2 = ReadLittleEndian32(header + 108);

    const u32 requiredFlags = kFlagCaps | kFlagHeight | kFlagWidth | kFlagPixelFormat;
    if ((flags & requiredFlags) != requiredFlags)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "DDS header flags are incomplete", 4));
    }
    if ((flags & kFlagLinearSize) == 0)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError, "DDS header has no linear size", 4));
    }
    if (pixelFormatSize != kPixelFormatSize)
    {
        return ImportResult<TextureAsset>::Err(MakeOffsetError(
            ErrorCode::ParseError, "DDS pixel format size is not 32", 4 + pixelFormatOffset));
    }
    if (width == 0 || height == 0 || width > 32768 || height > 32768 || depth > 1)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::OutOfRange, "DDS dimensions are unsupported", 4));
    }

    const bool isCube = (caps2 & kCaps2Cubemap) != 0;
    if (isCube && (caps2 & kCaps2AllFaces) != kCaps2AllFaces)
    {
        return ImportResult<TextureAsset>::Err(MakeOffsetError(
            ErrorCode::ParseError, "DDS cube map does not declare all six faces", 4));
    }

    TextureFormat format = TextureFormat::Unknown;
    usize payloadOffset = 4 + kHeaderSize;
    if ((pixelFormatFlags & kPixelFormatFourCc) != 0)
    {
        if (fourCc == kFourCcDx10)
        {
            if (bytes.Size() < payloadOffset + 20)
            {
                return ImportResult<TextureAsset>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "DDS DX10 header is truncated", payloadOffset));
            }
            const Byte* dx10 = bytes.Data() + payloadOffset;
            const u32 dxgiFormat = ReadLittleEndian32(dx10);
            const u32 resourceDimension = ReadLittleEndian32(dx10 + 4);
            const u32 arraySize = ReadLittleEndian32(dx10 + 12);
            if (resourceDimension != 3u)
            {
                return ImportResult<TextureAsset>::Err(MakeOffsetError(
                    ErrorCode::Unsupported, "DDS resource is not a 2D texture", payloadOffset));
            }
            if (arraySize != 1u)
            {
                return ImportResult<TextureAsset>::Err(MakeOffsetError(
                    ErrorCode::Unsupported, "DDS texture arrays are not supported", payloadOffset));
            }
            format = FormatFromDxgi(dxgiFormat);
            payloadOffset += 20;
        }
        else
        {
            format = FormatFromFourCc(fourCc);
        }
    }
    else if ((pixelFormatFlags & kPixelFormatRgb) != 0 && rgbBitCount == 32)
    {
        format = (rgbAlphaMask & 0xff000000u) != 0 ? TextureFormat::Rgba8Unorm
                                                   : TextureFormat::Bgra8Unorm;
    }
    else if ((pixelFormatFlags & kPixelFormatLuminance) != 0 && rgbBitCount == 8)
    {
        format = TextureFormat::R8Unorm;
    }

    if (format == TextureFormat::Unknown)
    {
        return ImportResult<TextureAsset>::Err(MakeOffsetError(
            ErrorCode::Unsupported, "DDS pixel format is not supported", 4 + pixelFormatOffset));
    }

    u32 mipCount = (flags & kFlagMipMapCount) != 0 ? mipMapCount : 1u;
    if (mipCount == 0)
    {
        mipCount = 1;
    }
    if (mipCount > 32)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::OutOfRange, "DDS mip count is invalid", 4));
    }

    const u32 faceCount = isCube ? 6u : 1u;

    // Measure every level first so the payload allocation is exact.
    struct LevelSize
    {
        u32 width;
        u32 height;
        u32 rowPitch;
        u64 size;
    };
    LevelSize sizes[32] = {};
    u64 payloadSize = 0;
    u32 levelWidth = width;
    u32 levelHeight = height;
    for (u32 level = 0; level < mipCount; ++level)
    {
        u32 rowPitch = 0;
        auto levelSize = TextureLevelSize(format, levelWidth, levelHeight, rowPitch);
        if (levelSize.IsError())
        {
            return ImportResult<TextureAsset>::Err(levelSize.Error());
        }
        sizes[level] = {levelWidth, levelHeight, rowPitch, levelSize.Value()};
        payloadSize += levelSize.Value() * faceCount;
        levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
        levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
    }
    if (payloadSize > bytes.Size() - payloadOffset)
    {
        return ImportResult<TextureAsset>::Err(
            MakeOffsetError(ErrorCode::ParseError,
                            "DDS payload is shorter than the declared levels", payloadOffset));
    }

    TextureAsset texture(allocator);
    texture.format = format;
    texture.width = width;
    texture.height = height;
    texture.depth = 1;
    texture.mipCount = mipCount;
    texture.layerCount = faceCount;
    texture.cubemap = isCube;

    // Normalize to mip-major order. DDS stores face-major (all mips of face 0,
    // then face 1), so a cube map needs a deterministic reorder on import.
    if (!texture.data.TryResize(static_cast<usize>(payloadSize)))
    {
        return ImportResult<TextureAsset>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory for DDS payload"));
    }
    u64 faceLocalOffset[32] = {};
    u64 perFaceSize = 0;
    for (u32 level = 0; level < mipCount; ++level)
    {
        faceLocalOffset[level] = perFaceSize;
        perFaceSize += sizes[level].size;
    }

    u64 target = 0;
    for (u32 level = 0; level < mipCount; ++level)
    {
        for (u32 face = 0; face < faceCount; ++face)
        {
            const u64 sourceOffset =
                payloadOffset + static_cast<u64>(face) * perFaceSize + faceLocalOffset[level];
            const u64 destination = target + static_cast<u64>(face) * sizes[level].size;
            if (!CopyRange(bytes.Data(), bytes.Size(), sourceOffset, sizes[level].size,
                           texture.data, destination))
            {
                return ImportResult<TextureAsset>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "DDS level range is invalid", payloadOffset));
            }
        }
        target += sizes[level].size * faceCount;
    }

    u64 levelOffset = 0;
    for (u32 level = 0; level < mipCount; ++level)
    {
        TextureLevel record;
        record.offset = levelOffset;
        record.size = sizes[level].size * faceCount;
        record.width = sizes[level].width;
        record.height = sizes[level].height;
        record.rowPitch = sizes[level].rowPitch;
        if (!texture.levels.TryPushBack(record))
        {
            return ImportResult<TextureAsset>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory for DDS level table"));
        }
        levelOffset += record.size;
    }

    auto validated = texture.Validate();
    if (validated.IsError())
    {
        return ImportResult<TextureAsset>::Err(validated.Error());
    }
    return ImportResult<TextureAsset>::Ok(std::move(texture));
}

}  // namespace cyber::assets
