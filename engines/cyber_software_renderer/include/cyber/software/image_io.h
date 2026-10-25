#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/types.h>
#include <cyber/software/framebuffer.h>

namespace cyber::software
{

using cyber::core::u8;

enum class ImageFormat : u8
{
    // Netpbm P6: ASCII header, then RGB bytes.
    Ppm = 0,
    // Uncompressed 24-bit true-color TGA with a top-down origin.
    Tga,
};

struct ImageWriteDesc
{
    ImageFormat format = ImageFormat::Ppm;
    // True applies the sRGB transfer function; false stores linear unorm bytes
    // for debugging. Alpha is not written by either format.
    bool encodeSrgb = true;
};

// Encodes the framebuffer into a new allocator-backed byte array. The result is
// deterministic: identical frames produce identical bytes.
[[nodiscard]] core::Result<memory::DynamicArray<memory::Byte>, core::Error>
EncodeImage(const Framebuffer& source, const ImageWriteDesc& desc,
            memory::Allocator& allocator) noexcept;

// Encodes and writes the file through cyber_platform. The path is UTF-8. The
// temporary encoded buffer is allocated from allocator. Fails with
// InvalidArgument for an invalid framebuffer or path and reports the platform
// write failure otherwise.
[[nodiscard]] core::Status<core::Error>
WriteImage(const char* utf8Path, const Framebuffer& source, memory::Allocator& allocator,
           const ImageWriteDesc& desc = ImageWriteDesc{}) noexcept;

// Decodes an image held in memory into a new linear RGBA32F framebuffer. PPM
// (P6, P5, and P3) and TGA (type 2 or 3, 8/24/32 bits) are recognized from the
// leading bytes. Input is treated as hostile: the decoder never reads past
// `bytes` and returns InvalidArgument or Unsupported for malformed data.
[[nodiscard]] core::Result<Framebuffer, core::Error>
ReadImageFromMemory(memory::Span<const memory::Byte> bytes, memory::Allocator& allocator) noexcept;

// Reads a file through cyber_platform and decodes it with
// `ReadImageFromMemory`. Fails with NotFound, InvalidArgument, or Unsupported.
[[nodiscard]] core::Result<Framebuffer, core::Error>
LoadImage(const char* utf8Path, memory::Allocator& allocator) noexcept;

}  // namespace cyber::software
