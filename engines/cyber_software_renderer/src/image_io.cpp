#include <cyber/core/error.h>
#include <cyber/platform/filesystem.h>
#include <cyber/software/image_io.h>

#include <cstring>

namespace cyber::software
{

using cyber::core::i32;
using cyber::core::kInvalidIndex;
using cyber::core::u32;
using cyber::core::usize;

namespace
{

using ByteArray = memory::DynamicArray<memory::Byte>;

bool CheckedMultiply(usize lhs, usize rhs, usize& result) noexcept
{
    if (lhs != 0 && rhs > kInvalidIndex / lhs)
    {
        return false;
    }
    result = lhs * rhs;
    return true;
}

// Writes the ASCII decimal form of value and returns the number of bytes written.
usize WriteDecimal(char* buffer, u32 value) noexcept
{
    char digits[10] = {};
    usize count = 0;
    do
    {
        digits[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0);
    for (usize index = 0; index < count; ++index)
    {
        buffer[index] = digits[count - 1 - index];
    }
    return count;
}

usize BuildPpmHeader(char* buffer, i32 width, i32 height) noexcept
{
    usize offset = 0;
    buffer[offset++] = 'P';
    buffer[offset++] = '6';
    buffer[offset++] = '\n';
    offset += WriteDecimal(buffer + offset, static_cast<u32>(width));
    buffer[offset++] = ' ';
    offset += WriteDecimal(buffer + offset, static_cast<u32>(height));
    buffer[offset++] = '\n';
    buffer[offset++] = '2';
    buffer[offset++] = '5';
    buffer[offset++] = '5';
    buffer[offset++] = '\n';
    return offset;
}

void BuildTgaHeader(u8* header, i32 width, i32 height) noexcept
{
    std::memset(header, 0, 18);
    header[2] = 2;  // Uncompressed true-color image.
    header[12] = static_cast<u8>(width & 0xFF);
    header[13] = static_cast<u8>((width >> 8) & 0xFF);
    header[14] = static_cast<u8>(height & 0xFF);
    header[15] = static_cast<u8>((height >> 8) & 0xFF);
    header[16] = 24;
    header[17] = 0x20;  // Top-down origin, zero alpha bits.
}

f32 ReadFloat(const u8* pointer) noexcept
{
    f32 value = 0.0f;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}

u8 EncodeChannel(f32 linear, bool encodeSrgb) noexcept
{
    return encodeSrgb ? EncodeSrgb8(linear) : QuantizeUnorm8(linear);
}

void EncodePpmPixel(u8* destination, const u8* pixel, PixelFormat format, bool encodeSrgb) noexcept
{
    if (format == PixelFormat::RGBA8Unorm)
    {
        constexpr f32 kScale = 1.0f / 255.0f;
        destination[0] = EncodeChannel(static_cast<f32>(pixel[0]) * kScale, encodeSrgb);
        destination[1] = EncodeChannel(static_cast<f32>(pixel[1]) * kScale, encodeSrgb);
        destination[2] = EncodeChannel(static_cast<f32>(pixel[2]) * kScale, encodeSrgb);
        return;
    }

    destination[0] = EncodeChannel(ReadFloat(pixel + 0), encodeSrgb);
    destination[1] = EncodeChannel(ReadFloat(pixel + 4), encodeSrgb);
    destination[2] = EncodeChannel(ReadFloat(pixel + 8), encodeSrgb);
}

void EncodeTgaPixel(u8* destination, const u8* pixel, PixelFormat format, bool encodeSrgb) noexcept
{
    if (format == PixelFormat::RGBA8Unorm)
    {
        constexpr f32 kScale = 1.0f / 255.0f;
        // TGA stores BGR.
        destination[0] = EncodeChannel(static_cast<f32>(pixel[2]) * kScale, encodeSrgb);
        destination[1] = EncodeChannel(static_cast<f32>(pixel[1]) * kScale, encodeSrgb);
        destination[2] = EncodeChannel(static_cast<f32>(pixel[0]) * kScale, encodeSrgb);
        return;
    }

    destination[0] = EncodeChannel(ReadFloat(pixel + 8), encodeSrgb);
    destination[1] = EncodeChannel(ReadFloat(pixel + 4), encodeSrgb);
    destination[2] = EncodeChannel(ReadFloat(pixel + 0), encodeSrgb);
}

}  // namespace

core::Result<ByteArray, core::Error> EncodeImage(const Framebuffer& source,
                                                 const ImageWriteDesc& desc,
                                                 memory::Allocator& allocator) noexcept
{
    if (!source.IsValid())
    {
        return core::Result<ByteArray, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "source framebuffer is invalid"});
    }
    if (desc.format != ImageFormat::Ppm && desc.format != ImageFormat::Tga)
    {
        return core::Result<ByteArray, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "unknown image format"});
    }

    usize pixelCount = 0;
    usize pixelBytes = 0;
    if (!CheckedMultiply(static_cast<usize>(source.Width()), static_cast<usize>(source.Height()),
                         pixelCount) ||
        !CheckedMultiply(pixelCount, static_cast<usize>(3), pixelBytes))
    {
        return core::Result<ByteArray, core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "image size overflows"});
    }

    u8 tgaHeader[18] = {};
    char ppmHeader[32] = {};
    usize headerBytes = 0;
    if (desc.format == ImageFormat::Ppm)
    {
        headerBytes = BuildPpmHeader(ppmHeader, source.Width(), source.Height());
    }
    else
    {
        BuildTgaHeader(tgaHeader, source.Width(), source.Height());
        headerBytes = sizeof(tgaHeader);
    }

    usize totalBytes = 0;
    if (headerBytes > kInvalidIndex - pixelBytes)
    {
        return core::Result<ByteArray, core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "image buffer size overflows"});
    }
    totalBytes = headerBytes + pixelBytes;

    ByteArray bytes(allocator);
    if (!bytes.TryReserve(totalBytes) || !bytes.TryResize(totalBytes))
    {
        return core::Result<ByteArray, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate the image buffer"});
    }

    u8* output = reinterpret_cast<u8*>(bytes.Data());
    if (desc.format == ImageFormat::Ppm)
    {
        std::memcpy(output, ppmHeader, headerBytes);
    }
    else
    {
        std::memcpy(output, tgaHeader, headerBytes);
    }

    u8* pixels = output + headerBytes;
    const usize pixelSize = PixelSizeBytes(source.Format());
    for (i32 row = 0; row < source.Height(); ++row)
    {
        const u8* sourceRow = source.Row(row);
        for (i32 column = 0; column < source.Width(); ++column)
        {
            const u8* pixel = sourceRow + static_cast<usize>(column) * pixelSize;
            if (desc.format == ImageFormat::Ppm)
            {
                EncodePpmPixel(pixels, pixel, source.Format(), desc.encodeSrgb);
            }
            else
            {
                EncodeTgaPixel(pixels, pixel, source.Format(), desc.encodeSrgb);
            }
            pixels += 3;
        }
    }

    return core::Result<ByteArray, core::Error>::Ok(std::move(bytes));
}

core::Status<core::Error> WriteImage(const char* utf8Path, const Framebuffer& source,
                                     memory::Allocator& allocator,
                                     const ImageWriteDesc& desc) noexcept
{
    if (utf8Path == nullptr || utf8Path[0] == '\0')
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "image path is empty"});
    }

    core::Result<ByteArray, core::Error> encoded = EncodeImage(source, desc, allocator);
    if (!encoded)
    {
        return core::Status<core::Error>::Err(encoded.Error());
    }

    ByteArray& bytes = encoded.Value();
    const memory::Span<const memory::Byte> view(bytes.Data(), bytes.Size());
    return platform::WriteFile(utf8Path, view);
}

}  // namespace cyber::software
