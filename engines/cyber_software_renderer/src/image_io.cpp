#include <cyber/core/error.h>
#include <cyber/platform/filesystem.h>
#include <cyber/software/image_io.h>

#include <cstring>
#include <utility>

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

namespace
{

using FramebufferResult = core::Result<Framebuffer, core::Error>;

constexpr u32 kMaxImageDimension = 16384;

bool IsSpace(u8 value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' ||
           value == '\f';
}

struct ByteCursor
{
    const u8* data = nullptr;
    usize size = 0;
    usize offset = 0;
};

bool AtEnd(const ByteCursor& cursor) noexcept
{
    return cursor.offset >= cursor.size;
}

void SkipWhitespaceAndComments(ByteCursor& cursor) noexcept
{
    for (;;)
    {
        while (!AtEnd(cursor) && IsSpace(cursor.data[cursor.offset]))
        {
            ++cursor.offset;
        }
        if (!AtEnd(cursor) && cursor.data[cursor.offset] == '#')
        {
            while (!AtEnd(cursor) && cursor.data[cursor.offset] != '\n')
            {
                ++cursor.offset;
            }
            continue;
        }
        break;
    }
}

bool ReadUnsigned(ByteCursor& cursor, u32& value) noexcept
{
    SkipWhitespaceAndComments(cursor);
    if (AtEnd(cursor) || cursor.data[cursor.offset] < '0' || cursor.data[cursor.offset] > '9')
    {
        return false;
    }
    u32 result = 0;
    while (!AtEnd(cursor) && cursor.data[cursor.offset] >= '0' && cursor.data[cursor.offset] <= '9')
    {
        const u32 digit = static_cast<u32>(cursor.data[cursor.offset] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u)
        {
            return false;
        }
        result = result * 10u + digit;
        ++cursor.offset;
    }
    value = result;
    return true;
}

bool ConsumeHeaderTerminator(ByteCursor& cursor) noexcept
{
    if (AtEnd(cursor))
    {
        return false;
    }
    if (cursor.data[cursor.offset] == '\r' && cursor.offset + 1 < cursor.size &&
        cursor.data[cursor.offset + 1] == '\n')
    {
        cursor.offset += 2;
        return true;
    }
    if (!IsSpace(cursor.data[cursor.offset]))
    {
        return false;
    }
    ++cursor.offset;
    return true;
}

f32 DecodeChannel(u32 value, u32 maxValue) noexcept
{
    const f32 normalized = static_cast<f32>(value) / static_cast<f32>(maxValue);
    return cyber::math::SrgbToLinear(normalized);
}

FramebufferResult MakeTarget(u32 width, u32 height, memory::Allocator& allocator) noexcept
{
    FramebufferDesc desc{};
    desc.width = static_cast<i32>(width);
    desc.height = static_cast<i32>(height);
    desc.format = PixelFormat::RGBA32F;
    return Framebuffer::Create(desc, allocator);
}

FramebufferResult ReadPpm(const ByteCursor& input, memory::Allocator& allocator) noexcept
{
    ByteCursor cursor = input;
    if (cursor.size < 2 || cursor.data[0] != 'P')
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "not a PPM image"});
    }
    const u8 kind = cursor.data[1];
    if (kind != '3' && kind != '5' && kind != '6')
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "unsupported PPM variant"});
    }
    cursor.offset = 2;

    u32 width = 0;
    u32 height = 0;
    u32 maxValue = 0;
    if (!ReadUnsigned(cursor, width) || !ReadUnsigned(cursor, height) ||
        !ReadUnsigned(cursor, maxValue))
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "malformed PPM header"});
    }
    if (width == 0 || height == 0 || width > kMaxImageDimension || height > kMaxImageDimension)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "PPM dimensions are out of range"});
    }
    if (maxValue == 0 || maxValue > 255)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "PPM maximum value is unsupported"});
    }

    FramebufferResult created = MakeTarget(width, height, allocator);
    if (!created)
    {
        return created;
    }
    Framebuffer target = std::move(created.Value());

    if (kind == '3')
    {
        for (u32 y = 0; y < height; ++y)
        {
            for (u32 x = 0; x < width; ++x)
            {
                u32 red = 0;
                u32 green = 0;
                u32 blue = 0;
                if (!ReadUnsigned(cursor, red) || !ReadUnsigned(cursor, green) ||
                    !ReadUnsigned(cursor, blue))
                {
                    return FramebufferResult::Err(
                        core::Error{core::ErrorCode::InvalidArgument, "truncated PPM data"});
                }
                target.SetPixel(static_cast<i32>(x), static_cast<i32>(y),
                                math::Color{DecodeChannel(red, maxValue),
                                            DecodeChannel(green, maxValue),
                                            DecodeChannel(blue, maxValue), 1.0f});
            }
        }
        return FramebufferResult::Ok(std::move(target));
    }

    if (!ConsumeHeaderTerminator(cursor))
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "missing PPM data separator"});
    }

    const usize channels = kind == '6' ? 3u : 1u;
    const usize required = static_cast<usize>(width) * static_cast<usize>(height) * channels;
    if (cursor.size - cursor.offset < required)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "truncated PPM data"});
    }

    const u8* pixels = cursor.data + cursor.offset;
    usize index = 0;
    for (u32 y = 0; y < height; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            math::Color color;
            if (channels == 3)
            {
                color = math::Color{DecodeChannel(pixels[index + 0], maxValue),
                                    DecodeChannel(pixels[index + 1], maxValue),
                                    DecodeChannel(pixels[index + 2], maxValue), 1.0f};
                index += 3;
            }
            else
            {
                const f32 gray = DecodeChannel(pixels[index], maxValue);
                color = math::Color{gray, gray, gray, 1.0f};
                index += 1;
            }
            target.SetPixel(static_cast<i32>(x), static_cast<i32>(y), color);
        }
    }
    return FramebufferResult::Ok(std::move(target));
}

FramebufferResult ReadTga(const ByteCursor& input, memory::Allocator& allocator) noexcept
{
    if (input.size < 18)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "truncated TGA header"});
    }
    const u8* header = input.data;
    const u32 idLength = header[0];
    const u8 colorMapType = header[1];
    const u8 imageType = header[2];
    const u32 width = static_cast<u32>(header[12]) | (static_cast<u32>(header[13]) << 8);
    const u32 height = static_cast<u32>(header[14]) | (static_cast<u32>(header[15]) << 8);
    const u8 depth = header[16];
    const u8 descriptor = header[17];
    const bool topDown = (descriptor & 0x20u) != 0;

    if (colorMapType != 0)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::Unsupported, "colormapped TGA is unsupported"});
    }
    if (imageType != 2 && imageType != 3)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::Unsupported, "compressed TGA is unsupported"});
    }
    if (width == 0 || height == 0 || width > kMaxImageDimension || height > kMaxImageDimension)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "TGA dimensions are out of range"});
    }

    usize bytesPerPixel = 0;
    if (imageType == 2)
    {
        if (depth != 24 && depth != 32)
        {
            return FramebufferResult::Err(
                core::Error{core::ErrorCode::Unsupported, "unsupported TGA depth"});
        }
        bytesPerPixel = depth / 8;
    }
    else
    {
        if (depth != 8)
        {
            return FramebufferResult::Err(
                core::Error{core::ErrorCode::Unsupported, "unsupported gray TGA depth"});
        }
        bytesPerPixel = 1;
    }

    const usize start = 18u + static_cast<usize>(idLength);
    if (start > input.size)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "truncated TGA data"});
    }
    const usize required = static_cast<usize>(width) * static_cast<usize>(height) * bytesPerPixel;
    if (input.size - start < required)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "truncated TGA data"});
    }

    FramebufferResult created = MakeTarget(width, height, allocator);
    if (!created)
    {
        return created;
    }
    Framebuffer target = std::move(created.Value());

    const u8* pixels = input.data + start;
    for (u32 y = 0; y < height; ++y)
    {
        const u32 sourceY = topDown ? y : (height - 1 - y);
        for (u32 x = 0; x < width; ++x)
        {
            const u8* pixel = pixels + (static_cast<usize>(sourceY) * width + x) * bytesPerPixel;
            math::Color color;
            if (bytesPerPixel == 1)
            {
                const f32 gray = DecodeChannel(pixel[0], 255);
                color = math::Color{gray, gray, gray, 1.0f};
            }
            else
            {
                // TGA stores blue, green, red, then optional alpha.
                color = math::Color{DecodeChannel(pixel[2], 255), DecodeChannel(pixel[1], 255),
                                    DecodeChannel(pixel[0], 255),
                                    bytesPerPixel == 4 ? DecodeChannel(pixel[3], 255) : 1.0f};
            }
            target.SetPixel(static_cast<i32>(x), static_cast<i32>(y), color);
        }
    }
    return FramebufferResult::Ok(std::move(target));
}

}  // namespace

core::Result<Framebuffer, core::Error> ReadImageFromMemory(memory::Span<const memory::Byte> bytes,
                                                           memory::Allocator& allocator) noexcept
{
    if (bytes.Data() == nullptr && bytes.Size() != 0)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "image span is null"});
    }
    if (bytes.Size() < 2)
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "image data is too short"});
    }

    ByteCursor cursor;
    cursor.data = reinterpret_cast<const u8*>(bytes.Data());
    cursor.size = bytes.Size();

    if (cursor.data[0] == 'P' &&
        (cursor.data[1] == '3' || cursor.data[1] == '5' || cursor.data[1] == '6'))
    {
        return ReadPpm(cursor, allocator);
    }
    return ReadTga(cursor, allocator);
}

core::Result<Framebuffer, core::Error> LoadImage(const char* utf8Path,
                                                 memory::Allocator& allocator) noexcept
{
    if (utf8Path == nullptr || utf8Path[0] == '\0')
    {
        return FramebufferResult::Err(
            core::Error{core::ErrorCode::InvalidArgument, "image path is empty"});
    }
    auto file = platform::ReadFile(utf8Path, allocator);
    if (!file)
    {
        return FramebufferResult::Err(file.Error());
    }
    ByteArray& bytes = file.Value();
    return ReadImageFromMemory(memory::Span<const memory::Byte>(bytes.Data(), bytes.Size()),
                               allocator);
}

}  // namespace cyber::software
