#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/span.h>
#include <cyber/memory/types.h>
#include <cyber/software/framebuffer.h>

namespace cyber::software
{

using cyber::core::i32;
using cyber::core::u8;
using cyber::core::usize;

// Bytes needed for a tightly packed width x height RGBA8 present image. Returns
// 0 when either dimension is not positive or the size overflows.
[[nodiscard]] usize PresentByteSize(i32 width, i32 height) noexcept;

// Converts a linear framebuffer into sRGB-encoded RGBA8 bytes with alpha
// quantized linearly. destination receives height rows; a strideBytes value of 0
// selects width * 4. Rows are written top-down and bytes outside the image are
// never touched.
//
// Fails with InvalidArgument when the source is invalid, the stride is smaller
// than width * 4, or destination cannot hold every row.
[[nodiscard]] core::Status<core::Error> PackPresentPixels(const Framebuffer& source,
                                                          memory::Span<u8> destination,
                                                          usize strideBytes = 0) noexcept;

}  // namespace cyber::software
