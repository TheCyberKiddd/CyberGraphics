#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/software/framebuffer.h>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

enum class GoldenImageStatus : u8
{
    Ok = 0,
    // At least one channel exceeded the tolerance.
    Mismatch,
    // A target was invalid or the dimensions differ.
    InvalidArgument,
};

struct GoldenImageDesc
{
    // Maximum allowed absolute difference in 8-bit sRGB space. The default is
    // 2/255, which absorbs cross-machine math differences while catching a real
    // regression.
    f32 tolerance = 2.0f / 255.0f;
};

struct GoldenImageResult
{
    GoldenImageStatus status = GoldenImageStatus::InvalidArgument;
    // Pixels where any channel exceeded the tolerance.
    u64 differingPixels = 0;
    // Largest absolute per-channel difference in 8-bit units (0..255).
    f32 maxChannelDelta = 0.0f;
};

// Compares two framebuffers in 8-bit sRGB space.
//
// Both targets must be valid and have identical dimensions. A differing
// dimension or an invalid target returns InvalidArgument. Otherwise the
// status is Ok when every channel is within `desc.tolerance` and Mismatch when
// any channel exceeds it.
[[nodiscard]] GoldenImageResult CompareImages(const Framebuffer& actual,
                                              const Framebuffer& expected,
                                              const GoldenImageDesc& desc = {}) noexcept;

// Writes the amplified absolute difference between two framebuffers as a PPM.
// Each channel is the 8-bit difference scaled by `amplification` and clamped to
// 255, which makes a small drift visible. Returns InvalidArgument for a
// dimension mismatch, an invalid target, or a bad path, and reports the write
// failure otherwise.
[[nodiscard]] core::Status<core::Error>
WriteDifferenceImage(const char* utf8Path, const Framebuffer& actual, const Framebuffer& expected,
                     memory::Allocator& allocator, f32 amplification = 8.0f) noexcept;

}  // namespace cyber::software
