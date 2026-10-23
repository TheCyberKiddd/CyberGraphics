#include <cyber/software/present_target.h>

#include <cstring>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::kInvalidIndex;

namespace
{

bool CheckedMultiply(usize lhs, usize rhs, usize& result) noexcept
{
    if (lhs != 0 && rhs > kInvalidIndex / lhs)
    {
        return false;
    }
    result = lhs * rhs;
    return true;
}

f32 ReadFloat(const u8* pointer) noexcept
{
    f32 value = 0.0f;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}

// The sRGB encode curve is evaluated with pow only while building this table.
// Quantizing linear input to 1/16384 steps and storing the encoded byte keeps
// the packed result within one 8-bit code of math::LinearToSrgb, which removes a
// per-channel pow (about 140 ms per 1280x720 frame) from the present path. The
// table lives on the stack so packing stays allocation-free and thread-safe.
constexpr i32 kSrgbTableSteps = 16384;

struct SrgbTables
{
    u8 encoded[kSrgbTableSteps + 1] = {};
    u8 unorm8[256] = {};

    SrgbTables() noexcept
    {
        for (i32 step = 0; step <= kSrgbTableSteps; ++step)
        {
            encoded[step] = EncodeSrgb8(static_cast<f32>(step) / static_cast<f32>(kSrgbTableSteps));
        }
        for (i32 value = 0; value < 256; ++value)
        {
            unorm8[value] = EncodeSrgb8(static_cast<f32>(value) * (1.0f / 255.0f));
        }
    }

    [[nodiscard]] u8 Encode(f32 value) const noexcept
    {
        if (!(value > 0.0f))
        {
            return 0;
        }
        if (value >= 1.0f)
        {
            return 255;
        }
        constexpr f32 kScale = static_cast<f32>(kSrgbTableSteps);
        i32 index = static_cast<i32>(value * kScale);
        if (index > kSrgbTableSteps)
        {
            index = kSrgbTableSteps;
        }
        return encoded[index];
    }
};

}  // namespace

usize PresentByteSize(i32 width, i32 height) noexcept
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    const usize widthBytes = static_cast<usize>(width);
    const usize heightCount = static_cast<usize>(height);
    usize pixels = 0;
    if (!CheckedMultiply(widthBytes, heightCount, pixels) || pixels > kInvalidIndex / 4)
    {
        return 0;
    }
    return pixels * 4;
}

core::Status<core::Error> PackPresentPixels(const Framebuffer& source, memory::Span<u8> destination,
                                            usize strideBytes) noexcept
{
    if (!source.IsValid())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "source framebuffer is invalid"});
    }

    const i32 width = source.Width();
    const i32 height = source.Height();
    const usize tightStride = static_cast<usize>(width) * 4;
    const usize stride = strideBytes == 0 ? tightStride : strideBytes;
    if (stride < tightStride)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "present stride is too small"});
    }

    usize required = 0;
    if (!CheckedMultiply(stride, static_cast<usize>(height) - 1, required) ||
        required > kInvalidIndex - tightStride)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "present image size overflows"});
    }
    required += tightStride;

    if (destination.Data() == nullptr || destination.Size() < required)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "present destination is too small"});
    }

    const bool floatSource = source.Format() == PixelFormat::RGBA32F;
    const SrgbTables tables{};
    for (i32 row = 0; row < height; ++row)
    {
        const u8* pixels = source.Row(row);
        u8* output = destination.Data() + static_cast<usize>(row) * stride;
        if (!floatSource)
        {
            for (i32 column = 0; column < width; ++column)
            {
                output[0] = tables.unorm8[pixels[0]];
                output[1] = tables.unorm8[pixels[1]];
                output[2] = tables.unorm8[pixels[2]];
                output[3] = pixels[3];
                pixels += 4;
                output += 4;
            }
        }
        else
        {
            for (i32 column = 0; column < width; ++column)
            {
                output[0] = tables.Encode(ReadFloat(pixels + 0));
                output[1] = tables.Encode(ReadFloat(pixels + 4));
                output[2] = tables.Encode(ReadFloat(pixels + 8));
                output[3] = QuantizeUnorm8(ReadFloat(pixels + 12));
                pixels += 16;
                output += 4;
            }
        }
    }

    return core::Status<core::Error>::Ok();
}

}  // namespace cyber::software
