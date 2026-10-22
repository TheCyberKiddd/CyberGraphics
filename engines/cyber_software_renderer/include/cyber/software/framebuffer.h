#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/math/color.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/types.h>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u8;
using cyber::core::usize;

// Storage formats. Both store linear color; no transfer function is applied when
// a pixel is written. Conversion to sRGB happens only at present or export time.
enum class PixelFormat : u8
{
    // Four bytes per pixel: red, green, blue, alpha. Each channel is linear
    // unorm, so 128 means 128 / 255 linear.
    RGBA8Unorm = 0,
    // Four binary32 channels: red, green, blue, alpha, all linear.
    RGBA32F,
};

// Bytes per pixel for a format, or 0 for an unknown value.
[[nodiscard]] constexpr usize PixelSizeBytes(PixelFormat format) noexcept
{
    switch (format)
    {
    case PixelFormat::RGBA8Unorm:
        return 4;
    case PixelFormat::RGBA32F:
        return 16;
    }
    return 0;
}

// True when format is a recognized PixelFormat value.
[[nodiscard]] constexpr bool IsValidPixelFormat(PixelFormat format) noexcept
{
    return PixelSizeBytes(format) != 0;
}

// Clamps a linear value to [0, 1] and quantizes it to 8-bit unorm. NaN maps to
// 0 and infinities saturate.
[[nodiscard]] u8 QuantizeUnorm8(f32 value) noexcept;

// Applies the sRGB transfer function to a linear value and quantizes it to
// 8 bits. NaN maps to 0 and infinities saturate.
[[nodiscard]] u8 EncodeSrgb8(f32 value) noexcept;

struct FramebufferDesc
{
    i32 width = 0;
    i32 height = 0;
    PixelFormat format = PixelFormat::RGBA8Unorm;
    // Bytes between the first pixel of consecutive rows. 0 means tightly packed
    // (width * PixelSizeBytes(format)). A supplied stride must be at least that
    // value and, for RGBA32F, a multiple of alignof(f32).
    usize strideBytes = 0;
};

// Move-only CPU image with allocator-owned storage.
//
// Create validates the descriptor and allocates exactly strideBytes * height
// bytes; every other operation is non-allocating. Pixel coordinates are
// top-left origin (x right, y down) and half-open: [0, width) and [0, height).
// Out-of-range reads return transparent black and out-of-range writes are
// ignored.
class Framebuffer
{
public:
    Framebuffer() noexcept = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    // Width and height must be in [1, 16384]; the format must be valid; the
    // stride must be large enough; and the total byte size must not overflow.
    // Returns InvalidArgument otherwise, or Internal when allocation fails.
    [[nodiscard]] static core::Result<Framebuffer, core::Error>
    Create(const FramebufferDesc& desc, memory::Allocator& allocator) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] i32 Width() const noexcept;
    [[nodiscard]] i32 Height() const noexcept;
    [[nodiscard]] PixelFormat Format() const noexcept;
    [[nodiscard]] usize StrideBytes() const noexcept;

    // Total owned bytes, including stride padding. Zero when invalid.
    [[nodiscard]] usize DataSizeBytes() const noexcept;

    // First byte of the image. Null when invalid. The pointer is non-owning and
    // stays valid until the framebuffer is destroyed, moved, or assigned.
    [[nodiscard]] u8* Data() noexcept;
    [[nodiscard]] const u8* Data() const noexcept;

    // First byte of row y. Returns nullptr when y is outside [0, height).
    [[nodiscard]] u8* Row(i32 y) noexcept;
    [[nodiscard]] const u8* Row(i32 y) const noexcept;

    [[nodiscard]] bool Contains(i32 x, i32 y) const noexcept;

    // Writes color to every pixel. Alpha is written as given.
    void Clear(const math::Color& color) noexcept;

    // Clipped rectangle fill. Negative sizes are treated as empty.
    void ClearRect(i32 x, i32 y, i32 width, i32 height, const math::Color& color) noexcept;

    void SetPixel(i32 x, i32 y, const math::Color& color) noexcept;

    // Reads the stored linear color. Out-of-range coordinates return
    // Color(0, 0, 0, 0).
    [[nodiscard]] math::Color GetPixel(i32 x, i32 y) const noexcept;

    // Copies source so its top-left pixel lands at (destX, destY). Clips against
    // this framebuffer and converts between formats per pixel.
    void Blit(const Framebuffer& source, i32 destX, i32 destY) noexcept;

    // Copies source into destination. The framebuffers must be valid, have equal
    // dimensions, and differ (or be the same object, which is a no-op). Formats
    // may differ. destination keeps its own stride.
    [[nodiscard]] static core::Status<core::Error> Convert(const Framebuffer& source,
                                                           Framebuffer& destination) noexcept;

private:
    void Release() noexcept;
    void MoveFrom(Framebuffer& other) noexcept;

    memory::Allocator* m_allocator = nullptr;
    u8* m_data = nullptr;
    usize m_sizeBytes = 0;
    usize m_strideBytes = 0;
    i32 m_width = 0;
    i32 m_height = 0;
    PixelFormat m_format = PixelFormat::RGBA8Unorm;
};

}  // namespace cyber::software
