#include <cyber/software/golden_image.h>
#include <cyber/software/image_io.h>

#include <cmath>
#include <utility>

namespace cyber::software
{

namespace
{

u8 EncodeChannel(const Framebuffer& target, i32 x, i32 y, u32 channel) noexcept
{
    const math::Color color = target.GetPixel(x, y);
    const f32 value = channel == 0   ? color.r
                      : channel == 1 ? color.g
                      : channel == 2 ? color.b
                                     : color.a;
    return EncodeSrgb8(value);
}

}  // namespace

GoldenImageResult CompareImages(const Framebuffer& actual, const Framebuffer& expected,
                                const GoldenImageDesc& desc) noexcept
{
    GoldenImageResult result{};
    if (!actual.IsValid() || !expected.IsValid() || actual.Width() != expected.Width() ||
        actual.Height() != expected.Height())
    {
        result.status = GoldenImageStatus::InvalidArgument;
        return result;
    }

    const f32 tolerance = desc.tolerance > 0.0f ? desc.tolerance * 255.0f : 0.0f;
    u64 differing = 0;
    f32 maximum = 0.0f;

    for (i32 y = 0; y < actual.Height(); ++y)
    {
        for (i32 x = 0; x < actual.Width(); ++x)
        {
            bool differs = false;
            for (u32 channel = 0; channel < 4; ++channel)
            {
                const f32 delta =
                    std::fabs(static_cast<f32>(EncodeChannel(actual, x, y, channel)) -
                              static_cast<f32>(EncodeChannel(expected, x, y, channel)));
                if (delta > maximum)
                {
                    maximum = delta;
                }
                if (delta > tolerance)
                {
                    differs = true;
                }
            }
            if (differs)
            {
                ++differing;
            }
        }
    }

    result.differingPixels = differing;
    result.maxChannelDelta = maximum;
    result.status = differing == 0 ? GoldenImageStatus::Ok : GoldenImageStatus::Mismatch;
    return result;
}

core::Status<core::Error> WriteDifferenceImage(const char* utf8Path, const Framebuffer& actual,
                                               const Framebuffer& expected,
                                               memory::Allocator& allocator,
                                               f32 amplification) noexcept
{
    if (utf8Path == nullptr || utf8Path[0] == '\0')
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "image path is empty"});
    }
    if (!actual.IsValid() || !expected.IsValid() || actual.Width() != expected.Width() ||
        actual.Height() != expected.Height())
    {
        return core::Status<core::Error>::Err(core::Error{
            core::ErrorCode::InvalidArgument, "images are invalid or have different dimensions"});
    }
    if (!(amplification > 0.0f) || !std::isfinite(amplification))
    {
        amplification = 1.0f;
    }

    FramebufferDesc desc{};
    desc.width = actual.Width();
    desc.height = actual.Height();
    desc.format = PixelFormat::RGBA8Unorm;
    auto created = Framebuffer::Create(desc, allocator);
    if (!created)
    {
        return core::Status<core::Error>::Err(created.Error());
    }
    Framebuffer difference = std::move(created.Value());

    for (i32 y = 0; y < actual.Height(); ++y)
    {
        for (i32 x = 0; x < actual.Width(); ++x)
        {
            const math::Color pixel = actual.GetPixel(x, y);
            const math::Color reference = expected.GetPixel(x, y);
            const f32 deltaR = std::fabs(static_cast<f32>(EncodeSrgb8(pixel.r)) -
                                         static_cast<f32>(EncodeSrgb8(reference.r))) *
                               amplification;
            const f32 deltaG = std::fabs(static_cast<f32>(EncodeSrgb8(pixel.g)) -
                                         static_cast<f32>(EncodeSrgb8(reference.g))) *
                               amplification;
            const f32 deltaB = std::fabs(static_cast<f32>(EncodeSrgb8(pixel.b)) -
                                         static_cast<f32>(EncodeSrgb8(reference.b))) *
                               amplification;
            const auto clampByte = [](f32 value) noexcept
            {
                const f32 clamped = value > 255.0f ? 255.0f : (value < 0.0f ? 0.0f : value);
                return clamped / 255.0f;
            };
            difference.SetPixel(
                x, y, math::Color{clampByte(deltaR), clampByte(deltaG), clampByte(deltaB), 1.0f});
        }
    }

    ImageWriteDesc writeDesc{};
    writeDesc.format = ImageFormat::Ppm;
    // The framebuffer already holds the byte values we want; do not re-encode.
    writeDesc.encodeSrgb = false;
    return WriteImage(utf8Path, difference, allocator, writeDesc);
}

}  // namespace cyber::software
