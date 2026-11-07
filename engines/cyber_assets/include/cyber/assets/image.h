#pragma once

#include <cyber/assets/asset.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>

namespace cyber::assets
{

using cyber::core::Byte;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::Span;

/// Pixel layout of a decoded image. Decoders normalize every input to
/// premultiplied-free, straight-alpha RGBA8 in the source's encoded color space;
/// color management is not applied.
enum class ImageFormat : u8
{
    Unknown = 0,
    Rgba8Unorm = 1,
};

/// Decoded 2D image. Rows are tightly packed and the allocator must outlive the
/// value. Pixels are always `ImageFormat::Rgba8Unorm`.
class Image
{
public:
    explicit Image(Allocator& allocator) noexcept : m_pixels(allocator) {}

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) noexcept = default;
    Image& operator=(Image&&) noexcept = default;
    ~Image() = default;

    /// Allocates `width * height * 4` bytes. Dimensions are limited to 32768 per
    /// axis and 256 MiB of pixels.
    [[nodiscard]] ImportStatus Resize(u32 width, u32 height) noexcept;

    [[nodiscard]] u32 Width() const noexcept
    {
        return m_width;
    }
    [[nodiscard]] u32 Height() const noexcept
    {
        return m_height;
    }
    [[nodiscard]] ImageFormat Format() const noexcept
    {
        return ImageFormat::Rgba8Unorm;
    }
    [[nodiscard]] u32 RowPitch() const noexcept
    {
        return m_width * 4u;
    }
    [[nodiscard]] usize SizeBytes() const noexcept
    {
        return m_pixels.Size();
    }
    [[nodiscard]] Span<const Byte> Pixels() const noexcept
    {
        return Span<const Byte>(m_pixels.Data(), m_pixels.Size());
    }
    [[nodiscard]] Span<Byte> MutablePixels() noexcept
    {
        return Span<Byte>(m_pixels.Data(), m_pixels.Size());
    }
    /// Returns one pixel as four components. Writes zeros when out of range.
    void GetPixel(u32 x, u32 y, Byte out[4]) const noexcept;
    /// Sets one pixel from four components. Out-of-range writes are ignored.
    void SetPixel(u32 x, u32 y, Byte r, Byte g, Byte b, Byte a) noexcept;

private:
    DynamicArray<Byte> m_pixels;
    u32 m_width = 0;
    u32 m_height = 0;
};

/// Formats the image decoders can identify from their signature.
enum class ImageFileFormat : u8
{
    Unknown = 0,
    Png = 1,
    Jpeg = 2,
};

/// Identifies an image by its signature. Does not validate the whole file.
[[nodiscard]] ImageFileFormat IdentifyImage(Span<const Byte> bytes) noexcept;

/// Decodes a PNG file, including palette, gray, RGB, and RGBA sources, 8- or
/// 16-bit depth, and Adam7 interlacing. CRC and zlib checksums are verified.
[[nodiscard]] ImportResult<Image> DecodePng(Span<const Byte> bytes, Allocator& allocator) noexcept;

/// Decodes a baseline (sequential) JPEG: 8-bit precision, one or three
/// components, any sampling factors, restart markers allowed. Progressive and
/// arithmetic-coded files are rejected with `Unsupported`.
[[nodiscard]] ImportResult<Image> DecodeJpeg(Span<const Byte> bytes, Allocator& allocator) noexcept;

/// Sniffs the signature and dispatches to the matching decoder.
[[nodiscard]] ImportResult<Image> DecodeImage(Span<const Byte> bytes,
                                              Allocator& allocator) noexcept;

}  // namespace cyber::assets
