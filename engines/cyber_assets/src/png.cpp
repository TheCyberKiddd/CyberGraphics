#include "deflate.h"

#include <cyber/assets/image.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::assets::detail::Crc32;
using cyber::assets::detail::InflateZlib;
using cyber::assets::detail::ReadBigEndian32;
using cyber::core::ErrorCode;
using cyber::core::u8;
using cyber::core::usize;

constexpr u8 kPngSignature[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
constexpr u32 kMaxDimension = 32768;
constexpr usize kMaxPixels = 64u * 1024u * 1024u;

/// Row layout of one Adam7 pass, or of the whole image when not interlaced.
struct PassGeometry
{
    u32 xStart = 0;
    u32 yStart = 0;
    u32 xStep = 1;
    u32 yStep = 1;
    u32 width = 0;
    u32 height = 0;
    usize rowBytes = 0;
};

constexpr u32 kAdam7XStart[7] = {0, 4, 0, 2, 0, 1, 0};
constexpr u32 kAdam7YStart[7] = {0, 0, 4, 0, 2, 0, 1};
constexpr u32 kAdam7XStep[7] = {8, 8, 4, 4, 2, 2, 1};
constexpr u32 kAdam7YStep[7] = {8, 8, 8, 4, 4, 2, 2};

[[nodiscard]] u32 ChannelsForColorType(u8 colorType) noexcept
{
    switch (colorType)
    {
    case 0:
        return 1;
    case 2:
        return 3;
    case 3:
        return 1;
    case 4:
        return 2;
    case 6:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] bool IsValidDepth(u8 colorType, u8 bitDepth) noexcept
{
    switch (colorType)
    {
    case 0:
        return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
    case 2:
    case 4:
    case 6:
        return bitDepth == 8 || bitDepth == 16;
    case 3:
        return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
    default:
        return false;
    }
}

/// Extracts one sample from a packed row. `bitDepth` of 8 or 16 reads whole
/// bytes; narrower depths read most-significant-bit first.
[[nodiscard]] u32 ReadSample(const Byte* row, u32 sampleIndex, u8 bitDepth) noexcept
{
    if (bitDepth == 8)
    {
        return std::to_integer<u8>(row[sampleIndex]);
    }
    if (bitDepth == 16)
    {
        return (static_cast<u32>(std::to_integer<u8>(row[sampleIndex * 2])) << 8u) |
               static_cast<u32>(std::to_integer<u8>(row[sampleIndex * 2 + 1]));
    }
    const u32 bitOffset = sampleIndex * bitDepth;
    const u32 byteIndex = bitOffset / 8u;
    const u32 shift = 8u - bitDepth - (bitOffset % 8u);
    const u32 mask = (1u << bitDepth) - 1u;
    return (static_cast<u32>(std::to_integer<u8>(row[byteIndex])) >> shift) & mask;
}

[[nodiscard]] u8 ToByte(u32 sample, u8 bitDepth) noexcept
{
    if (bitDepth == 16)
    {
        return static_cast<u8>(sample >> 8u);
    }
    if (bitDepth == 8)
    {
        return static_cast<u8>(sample);
    }
    // Scale a narrow sample to the full range (1 -> 255, 2 -> 85, 4 -> 17).
    const u32 maximum = (1u << bitDepth) - 1u;
    return static_cast<u8>((sample * 255u + maximum / 2u) / maximum);
}

[[nodiscard]] u8 Paeth(u8 left, u8 up, u8 upLeft) noexcept
{
    const int estimate = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(upLeft);
    const int leftDistance = estimate > static_cast<int>(left) ? estimate - left : left - estimate;
    const int upDistance = estimate > static_cast<int>(up) ? estimate - up : up - estimate;
    const int upLeftDistance =
        estimate > static_cast<int>(upLeft) ? estimate - upLeft : upLeft - estimate;
    if (leftDistance <= upDistance && leftDistance <= upLeftDistance)
    {
        return left;
    }
    return upDistance <= upLeftDistance ? up : upLeft;
}

/// Reverses the per-row filter in place. `previous` is the prior unfiltered row
/// for this pass, or a zero row for the first scanline.
void UnfilterRow(u8 filter, Byte* row, const Byte* previous, usize rowBytes, usize bytesPerPixel,
                 ImportError& error) noexcept
{
    switch (filter)
    {
    case 0:
        return;
    case 1:
        for (usize index = bytesPerPixel; index < rowBytes; ++index)
        {
            row[index] = static_cast<Byte>(std::to_integer<u8>(row[index]) +
                                           std::to_integer<u8>(row[index - bytesPerPixel]));
        }
        return;
    case 2:
        for (usize index = 0; index < rowBytes; ++index)
        {
            row[index] = static_cast<Byte>(std::to_integer<u8>(row[index]) +
                                           std::to_integer<u8>(previous[index]));
        }
        return;
    case 3:
        for (usize index = 0; index < rowBytes; ++index)
        {
            const u32 left = index >= bytesPerPixel
                                 ? static_cast<u32>(std::to_integer<u8>(row[index - bytesPerPixel]))
                                 : 0u;
            const u32 up = static_cast<u32>(std::to_integer<u8>(previous[index]));
            row[index] = static_cast<Byte>(std::to_integer<u8>(row[index]) +
                                           static_cast<u8>((left + up) / 2u));
        }
        return;
    case 4:
        for (usize index = 0; index < rowBytes; ++index)
        {
            const u8 left = index >= bytesPerPixel ? std::to_integer<u8>(row[index - bytesPerPixel])
                                                   : static_cast<u8>(0);
            const u8 up = std::to_integer<u8>(previous[index]);
            const u8 upLeft = index >= bytesPerPixel
                                  ? std::to_integer<u8>(previous[index - bytesPerPixel])
                                  : static_cast<u8>(0);
            row[index] =
                static_cast<Byte>(std::to_integer<u8>(row[index]) + Paeth(left, up, upLeft));
        }
        return;
    default:
        error = MakeError(ErrorCode::ParseError, "PNG row uses an unknown filter");
        return;
    }
}

}  // namespace

ImportStatus Image::Resize(u32 width, u32 height) noexcept
{
    if (width == 0 || height == 0)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "image dimensions must be nonzero"));
    }
    if (width > kMaxDimension || height > kMaxDimension)
    {
        return ImportStatus::Err(MakeError(ErrorCode::OutOfRange, "image dimension is too large"));
    }
    const usize pixels = static_cast<usize>(width) * static_cast<usize>(height);
    if (pixels > kMaxPixels || pixels > 0xffffffffull / 4u)
    {
        return ImportStatus::Err(MakeError(ErrorCode::OutOfRange, "image is too large"));
    }
    if (!m_pixels.TryResize(pixels * 4u))
    {
        return ImportStatus::Err(MakeError(ErrorCode::OutOfRange, "out of memory for image data"));
    }
    m_width = width;
    m_height = height;
    return ImportStatus::Ok();
}

void Image::GetPixel(u32 x, u32 y, Byte out[4]) const noexcept
{
    if (x >= m_width || y >= m_height || m_pixels.Empty())
    {
        out[0] = Byte{0};
        out[1] = Byte{0};
        out[2] = Byte{0};
        out[3] = Byte{0};
        return;
    }
    const usize offset = (static_cast<usize>(y) * m_width + x) * 4u;
    out[0] = m_pixels[offset];
    out[1] = m_pixels[offset + 1];
    out[2] = m_pixels[offset + 2];
    out[3] = m_pixels[offset + 3];
}

void Image::SetPixel(u32 x, u32 y, Byte r, Byte g, Byte b, Byte a) noexcept
{
    if (x >= m_width || y >= m_height || m_pixels.Empty())
    {
        return;
    }
    const usize offset = (static_cast<usize>(y) * m_width + x) * 4u;
    m_pixels[offset] = r;
    m_pixels[offset + 1] = g;
    m_pixels[offset + 2] = b;
    m_pixels[offset + 3] = a;
}

ImageFileFormat IdentifyImage(Span<const Byte> bytes) noexcept
{
    if (bytes.Size() >= 8 && std::memcmp(bytes.Data(), kPngSignature, sizeof(kPngSignature)) == 0)
    {
        return ImageFileFormat::Png;
    }
    if (bytes.Size() >= 3 && std::to_integer<u8>(bytes[0]) == 0xffu &&
        std::to_integer<u8>(bytes[1]) == 0xd8u && std::to_integer<u8>(bytes[2]) == 0xffu)
    {
        return ImageFileFormat::Jpeg;
    }
    return ImageFileFormat::Unknown;
}

ImportResult<Image> DecodeImage(Span<const Byte> bytes, Allocator& allocator) noexcept
{
    switch (IdentifyImage(bytes))
    {
    case ImageFileFormat::Png:
        return DecodePng(bytes, allocator);
    case ImageFileFormat::Jpeg:
        return DecodeJpeg(bytes, allocator);
    case ImageFileFormat::Unknown:
        break;
    }
    return ImportResult<Image>::Err(MakeError(ErrorCode::Unsupported, "unrecognized image format"));
}

ImportResult<Image> DecodePng(Span<const Byte> bytes, Allocator& allocator) noexcept
{
    if (bytes.Size() < sizeof(kPngSignature) + 12 ||
        std::memcmp(bytes.Data(), kPngSignature, sizeof(kPngSignature)) != 0)
    {
        return ImportResult<Image>::Err(MakeError(ErrorCode::ParseError, "missing PNG signature"));
    }

    u32 width = 0;
    u32 height = 0;
    u8 bitDepth = 0;
    u8 colorType = 0;
    bool interlaced = false;
    bool sawHeader = false;
    bool sawEnd = false;

    DynamicArray<Byte> palette(allocator);
    DynamicArray<Byte> transparency(allocator);
    DynamicArray<Byte> compressed(allocator);

    usize offset = sizeof(kPngSignature);
    while (offset + 12 <= bytes.Size())
    {
        const u32 length = ReadBigEndian32(bytes.Data() + offset);
        if (length > 0x7fffffffu)
        {
            return ImportResult<Image>::Err(
                MakeOffsetError(ErrorCode::ParseError, "PNG chunk length is invalid", offset));
        }
        const usize chunkStart = offset;
        if (length > bytes.Size() - offset - 12)
        {
            return ImportResult<Image>::Err(
                MakeOffsetError(ErrorCode::ParseError, "PNG chunk overruns the file", chunkStart));
        }
        const Byte* type = bytes.Data() + offset + 4;
        const Byte* data = bytes.Data() + offset + 8;
        const u32 storedCrc = ReadBigEndian32(data + length);
        offset += 12 + length;

        u32 crc = Crc32(0, type, 4);
        crc = Crc32(crc, data, length);
        if (crc != storedCrc)
        {
            return ImportResult<Image>::Err(
                MakeOffsetError(ErrorCode::ParseError, "PNG chunk CRC mismatch", chunkStart));
        }

        const char* typeName = reinterpret_cast<const char*>(type);
        if (std::memcmp(typeName, "IHDR", 4) == 0)
        {
            if (sawHeader || length != 13)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::ParseError, "PNG header is malformed or duplicated", chunkStart));
            }
            width = ReadBigEndian32(data);
            height = ReadBigEndian32(data + 4);
            bitDepth = std::to_integer<u8>(data[8]);
            colorType = std::to_integer<u8>(data[9]);
            const u8 compression = std::to_integer<u8>(data[10]);
            const u8 filterMethod = std::to_integer<u8>(data[11]);
            const u8 interlaceMethod = std::to_integer<u8>(data[12]);

            if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::OutOfRange, "PNG dimensions are unsupported", chunkStart));
            }
            if (static_cast<usize>(width) * static_cast<usize>(height) > kMaxPixels)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::OutOfRange, "PNG pixel count is unsupported", chunkStart));
            }
            if (!IsValidDepth(colorType, bitDepth) || compression != 0 || filterMethod != 0 ||
                interlaceMethod > 1)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::Unsupported, "PNG color type or method is unsupported", chunkStart));
            }
            interlaced = interlaceMethod == 1;
            sawHeader = true;
        }
        else if (std::memcmp(typeName, "PLTE", 4) == 0)
        {
            if (length == 0 || length % 3 != 0 || length > 768)
            {
                return ImportResult<Image>::Err(
                    MakeOffsetError(ErrorCode::ParseError, "PNG palette is malformed", chunkStart));
            }
            if (!palette.TryResize(length))
            {
                return ImportResult<Image>::Err(
                    MakeError(ErrorCode::OutOfRange, "out of memory for the PNG palette"));
            }
            std::memcpy(palette.Data(), data, length);
        }
        else if (std::memcmp(typeName, "tRNS", 4) == 0)
        {
            if (!transparency.TryResize(length))
            {
                return ImportResult<Image>::Err(
                    MakeError(ErrorCode::OutOfRange, "out of memory for PNG transparency"));
            }
            std::memcpy(transparency.Data(), data, length);
        }
        else if (std::memcmp(typeName, "IDAT", 4) == 0)
        {
            const usize previous = compressed.Size();
            if (length > 0xffffffffull - previous)
            {
                return ImportResult<Image>::Err(MakeOffsetError(
                    ErrorCode::Overflow, "PNG image data is too large", chunkStart));
            }
            if (!compressed.TryResize(previous + length))
            {
                return ImportResult<Image>::Err(
                    MakeError(ErrorCode::OutOfRange, "out of memory for PNG image data"));
            }
            std::memcpy(compressed.Data() + previous, data, length);
        }
        else if (std::memcmp(typeName, "IEND", 4) == 0)
        {
            sawEnd = true;
            break;
        }
        // Unknown critical chunks are rejected by the spec; known ancillary
        // chunks (gAMA, sRGB, pHYs, iCCP, tEXt, zTXt, iTXt, bKGD, sBIT) are
        // validated for CRC and otherwise ignored.
        else if (static_cast<unsigned char>(typeName[0]) >= 'A' &&
                 static_cast<unsigned char>(typeName[0]) <= 'Z')
        {
            return ImportResult<Image>::Err(MakeOffsetError(
                ErrorCode::Unsupported, "PNG uses an unknown critical chunk", chunkStart));
        }
    }

    if (!sawHeader || !sawEnd)
    {
        return ImportResult<Image>::Err(
            MakeError(ErrorCode::ParseError, "PNG is missing a required chunk"));
    }
    if (compressed.Empty())
    {
        return ImportResult<Image>::Err(MakeError(ErrorCode::ParseError, "PNG has no image data"));
    }
    if (colorType == 3)
    {
        if (palette.Empty())
        {
            return ImportResult<Image>::Err(
                MakeError(ErrorCode::ParseError, "palette PNG has no palette"));
        }
        const u32 entries = static_cast<u32>(palette.Size() / 3);
        if (entries > (1u << bitDepth))
        {
            return ImportResult<Image>::Err(MakeError(
                ErrorCode::ParseError, "PNG palette is larger than the bit depth allows"));
        }
    }

    const u32 channels = ChannelsForColorType(colorType);
    const u32 bitsPerPixel = channels * bitDepth;
    const usize bytesPerPixel = (bitsPerPixel + 7u) / 8u;

    // Compute the exact inflated size so the inflater cannot be used to allocate
    // more than the image needs.
    PassGeometry passes[7];
    u32 passCount = 0;
    if (interlaced)
    {
        for (u32 pass = 0; pass < 7; ++pass)
        {
            if (width <= kAdam7XStart[pass] || height <= kAdam7YStart[pass])
            {
                continue;
            }
            PassGeometry& geometry = passes[passCount++];
            geometry.xStart = kAdam7XStart[pass];
            geometry.yStart = kAdam7YStart[pass];
            geometry.xStep = kAdam7XStep[pass];
            geometry.yStep = kAdam7YStep[pass];
            geometry.width = (width - geometry.xStart + geometry.xStep - 1) / geometry.xStep;
            geometry.height = (height - geometry.yStart + geometry.yStep - 1) / geometry.yStep;
            geometry.rowBytes = (static_cast<usize>(geometry.width) * bitsPerPixel + 7u) / 8u;
        }
    }
    else
    {
        PassGeometry& geometry = passes[0];
        geometry.width = width;
        geometry.height = height;
        geometry.rowBytes = (static_cast<usize>(width) * bitsPerPixel + 7u) / 8u;
        passCount = 1;
    }

    usize expectedBytes = 0;
    for (u32 pass = 0; pass < passCount; ++pass)
    {
        const usize passBytes =
            static_cast<usize>(passes[pass].height) * (passes[pass].rowBytes + 1);
        if (passBytes > kMaxPixels * 16)
        {
            return ImportResult<Image>::Err(
                MakeError(ErrorCode::OutOfRange, "PNG scanline data is too large"));
        }
        expectedBytes += passBytes;
    }

    DynamicArray<Byte> raw(allocator);
    if (!raw.TryReserve(expectedBytes))
    {
        return ImportResult<Image>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory for PNG scanlines"));
    }
    ImportError inflateError;
    auto inflated = InflateZlib(Span<const Byte>(compressed.Data(), compressed.Size()), raw,
                                expectedBytes, inflateError);
    if (inflated.IsError())
    {
        return ImportResult<Image>::Err(inflateError);
    }
    if (raw.Size() != expectedBytes)
    {
        return ImportResult<Image>::Err(
            MakeError(ErrorCode::ParseError, "PNG scanline data has the wrong size"));
    }

    Image image(allocator);
    auto resized = image.Resize(width, height);
    if (resized.IsError())
    {
        return ImportResult<Image>::Err(resized.Error());
    }

    const bool hasKeyTransparency = !transparency.Empty() && (colorType == 0 || colorType == 2);
    const u32 keyGray = hasKeyTransparency && transparency.Size() >= 2
                            ? (static_cast<u32>(std::to_integer<u8>(transparency[0])) << 8u) |
                                  static_cast<u32>(std::to_integer<u8>(transparency[1]))
                            : 0u;
    const u32 keyRed = hasKeyTransparency && transparency.Size() >= 6
                           ? (static_cast<u32>(std::to_integer<u8>(transparency[0])) << 8u) |
                                 static_cast<u32>(std::to_integer<u8>(transparency[1]))
                           : 0u;
    const u32 keyGreen = hasKeyTransparency && transparency.Size() >= 6
                             ? (static_cast<u32>(std::to_integer<u8>(transparency[2])) << 8u) |
                                   static_cast<u32>(std::to_integer<u8>(transparency[3]))
                             : 0u;
    const u32 keyBlue = hasKeyTransparency && transparency.Size() >= 6
                            ? (static_cast<u32>(std::to_integer<u8>(transparency[4])) << 8u) |
                                  static_cast<u32>(std::to_integer<u8>(transparency[5]))
                            : 0u;

    DynamicArray<Byte> previousRow(allocator);
    if (!previousRow.TryResize(passes[0].rowBytes))
    {
        return ImportResult<Image>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory for PNG filters"));
    }

    usize rawOffset = 0;
    for (u32 pass = 0; pass < passCount; ++pass)
    {
        const PassGeometry& geometry = passes[pass];
        if (!previousRow.TryResize(geometry.rowBytes))
        {
            return ImportResult<Image>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory for PNG filters"));
        }
        std::memset(previousRow.Data(), 0, geometry.rowBytes);

        for (u32 row = 0; row < geometry.height; ++row)
        {
            const u8 filter = std::to_integer<u8>(raw[rawOffset]);
            ++rawOffset;
            Byte* rowData = raw.Data() + rawOffset;
            ImportError filterError;
            UnfilterRow(filter, rowData, previousRow.Data(), geometry.rowBytes, bytesPerPixel,
                        filterError);
            if (filterError.message != nullptr)
            {
                return ImportResult<Image>::Err(filterError);
            }
            rawOffset += geometry.rowBytes;

            const u32 y = geometry.yStart + row * geometry.yStep;
            for (u32 column = 0; column < geometry.width; ++column)
            {
                const u32 x = geometry.xStart + column * geometry.xStep;
                u8 red = 0;
                u8 green = 0;
                u8 blue = 0;
                u8 alpha = 255;
                switch (colorType)
                {
                case 0:
                {
                    const u32 gray = ReadSample(rowData, column, bitDepth);
                    if (hasKeyTransparency && gray == keyGray)
                    {
                        alpha = 0;
                    }
                    red = ToByte(gray, bitDepth);
                    green = red;
                    blue = red;
                    break;
                }
                case 2:
                {
                    const u32 r = ReadSample(rowData, column * 3, bitDepth);
                    const u32 g = ReadSample(rowData, column * 3 + 1, bitDepth);
                    const u32 b = ReadSample(rowData, column * 3 + 2, bitDepth);
                    if (hasKeyTransparency && r == keyRed && g == keyGreen && b == keyBlue)
                    {
                        alpha = 0;
                    }
                    red = ToByte(r, bitDepth);
                    green = ToByte(g, bitDepth);
                    blue = ToByte(b, bitDepth);
                    break;
                }
                case 3:
                {
                    const u32 entry = ReadSample(rowData, column, bitDepth);
                    if (entry * 3u + 2u >= palette.Size())
                    {
                        return ImportResult<Image>::Err(
                            MakeError(ErrorCode::ParseError, "PNG palette index is out of range"));
                    }
                    red = std::to_integer<u8>(palette[entry * 3]);
                    green = std::to_integer<u8>(palette[entry * 3 + 1]);
                    blue = std::to_integer<u8>(palette[entry * 3 + 2]);
                    if (entry < transparency.Size())
                    {
                        alpha = std::to_integer<u8>(transparency[entry]);
                    }
                    break;
                }
                case 4:
                {
                    const u32 gray = ReadSample(rowData, column * 2, bitDepth);
                    const u32 a = ReadSample(rowData, column * 2 + 1, bitDepth);
                    red = ToByte(gray, bitDepth);
                    green = red;
                    blue = red;
                    alpha = ToByte(a, bitDepth);
                    break;
                }
                case 6:
                {
                    red = ToByte(ReadSample(rowData, column * 4, bitDepth), bitDepth);
                    green = ToByte(ReadSample(rowData, column * 4 + 1, bitDepth), bitDepth);
                    blue = ToByte(ReadSample(rowData, column * 4 + 2, bitDepth), bitDepth);
                    alpha = ToByte(ReadSample(rowData, column * 4 + 3, bitDepth), bitDepth);
                    break;
                }
                default:
                    return ImportResult<Image>::Err(
                        MakeError(ErrorCode::Unsupported, "PNG color type is unsupported"));
                }
                image.SetPixel(x, y, static_cast<Byte>(red), static_cast<Byte>(green),
                               static_cast<Byte>(blue), static_cast<Byte>(alpha));
            }

            std::memcpy(previousRow.Data(), rowData, geometry.rowBytes);
        }
    }

    return ImportResult<Image>::Ok(std::move(image));
}

}  // namespace cyber::assets
