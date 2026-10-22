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
    constexpr f32 kUnormScale = 1.0f / 255.0f;
    for (i32 row = 0; row < height; ++row)
    {
        const u8* pixels = source.Row(row);
        u8* output = destination.Data() + static_cast<usize>(row) * stride;
        if (!floatSource)
        {
            for (i32 column = 0; column < width; ++column)
            {
                output[0] = EncodeSrgb8(static_cast<f32>(pixels[0]) * kUnormScale);
                output[1] = EncodeSrgb8(static_cast<f32>(pixels[1]) * kUnormScale);
                output[2] = EncodeSrgb8(static_cast<f32>(pixels[2]) * kUnormScale);
                output[3] = pixels[3];
                pixels += 4;
                output += 4;
            }
        }
        else
        {
            for (i32 column = 0; column < width; ++column)
            {
                output[0] = EncodeSrgb8(ReadFloat(pixels + 0));
                output[1] = EncodeSrgb8(ReadFloat(pixels + 4));
                output[2] = EncodeSrgb8(ReadFloat(pixels + 8));
                output[3] = QuantizeUnorm8(ReadFloat(pixels + 12));
                pixels += 16;
                output += 4;
            }
        }
    }

    return core::Status<core::Error>::Ok();
}

}  // namespace cyber::software
