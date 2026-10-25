#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/math/color.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::u8;
using cyber::core::usize;

// Texture storage. Both formats hold linear color, like the framebuffer, so a
// texel uploads to the CPU pipeline without a transfer function.
enum class TextureFormat : u8
{
    // Four linear unorm bytes per texel.
    RGBA8Unorm = 0,
    // Four binary32 channels per texel.
    RGBA32F,
};

// Bytes per texel for a format, or 0 for an unknown value.
[[nodiscard]] constexpr usize TexturePixelSizeBytes(TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::RGBA8Unorm:
        return 4;
    case TextureFormat::RGBA32F:
        return 16;
    }
    return 0;
}

[[nodiscard]] constexpr bool IsValidTextureFormat(TextureFormat format) noexcept
{
    return TexturePixelSizeBytes(format) != 0;
}

// Behaviour when a UV leaves [0, 1].
enum class AddressMode : u8
{
    Repeat = 0,
    ClampToEdge,
    MirroredRepeat,
};

// Filtering inside a level. Mip selection uses the same enum.
enum class FilterMode : u8
{
    Nearest = 0,
    Bilinear,
};

// Sampler state. The same sampler is applied to both axes.
struct SamplerDesc
{
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    // Used for levels above zero and for explicit levels.
    FilterMode minFilter = FilterMode::Bilinear;
    // Used for level zero.
    FilterMode magFilter = FilterMode::Bilinear;
    // Nearest fetches one level; Bilinear blends two adjacent levels, which is
    // the trilinear path.
    FilterMode mipFilter = FilterMode::Bilinear;
};

struct TextureDesc
{
    // Width and height must be in [1, 16384].
    i32 width = 0;
    i32 height = 0;
    TextureFormat format = TextureFormat::RGBA8Unorm;
    // Build the box-filtered mip chain at creation and on every upload.
    bool generateMips = true;
};

// Maximum number of mip levels: 16384 down to 1 inclusive is 15 levels.
inline constexpr u32 kMaxTextureMipLevels = 16;

// Move-only CPU texture with an allocator-owned mip chain.
//
// Every level lives in one allocation. Level 0 is the full image and each
// following level halves both dimensions, clamping to 1, until both reach 1.
// Texel coordinates are top-left origin and half-open: [0, mipWidth) and
// [0, mipHeight). Out-of-range reads return transparent black and writes are
// ignored.
class Texture
{
public:
    Texture() noexcept = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Validates the descriptor, allocates the mip chain, and clears it to
    // transparent black. Returns InvalidArgument for an unknown format or a
    // dimension outside [1, 16384], or Internal when allocation fails.
    [[nodiscard]] static core::Result<Texture, core::Error>
    Create(const TextureDesc& desc, memory::Allocator& allocator) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] i32 Width() const noexcept;
    [[nodiscard]] i32 Height() const noexcept;
    [[nodiscard]] TextureFormat Format() const noexcept;
    [[nodiscard]] u32 MipLevels() const noexcept;
    [[nodiscard]] i32 MipWidth(u32 level) const noexcept;
    [[nodiscard]] i32 MipHeight(u32 level) const noexcept;

    // Total owned bytes across every level. Zero when invalid.
    [[nodiscard]] usize DataSizeBytes() const noexcept;

    // Reads one texel. Out-of-range levels or coordinates return transparent
    // black.
    [[nodiscard]] math::Color GetTexel(u32 level, i32 x, i32 y) const noexcept;

    // Writes one texel. Out-of-range levels or coordinates are ignored.
    void SetTexel(u32 level, i32 x, i32 y, const math::Color& color) noexcept;

    // Replaces level 0 from colors.size() texels (row-major, width * height)
    // and regenerates the mip chain when the texture was created with
    // generateMips. A short or long span leaves the texture untouched and
    // returns false.
    [[nodiscard]] bool Upload(memory::Span<const math::Color> colors) noexcept;

    // Box-filters every level above zero from the level before it.
    void GenerateMips() noexcept;

private:
    void Release() noexcept;
    void MoveFrom(Texture& other) noexcept;

    memory::Allocator* m_allocator = nullptr;
    u8* m_data = nullptr;
    usize m_sizeBytes = 0;
    usize m_levelOffsets[kMaxTextureMipLevels] = {};
    i32 m_width = 0;
    i32 m_height = 0;
    u32 m_mipLevels = 0;
    TextureFormat m_format = TextureFormat::RGBA8Unorm;
};

// Samples `texture` with the given sampler at level zero, or the level chosen
// by the sampler's mip filter. NaN and infinite coordinates saturate to the
// nearest edge.
[[nodiscard]] math::Color SampleTexture(const Texture& texture, f32 u, f32 v,
                                        const SamplerDesc& sampler) noexcept;

// Samples `texture` at an explicit fractional mip level. The level is clamped
// to [0, MipLevels - 1]; a fractional level blends the two adjacent levels when
// the sampler's mip filter is Bilinear. A nearest mip filter uses the floor
// level and ignores the fraction.
[[nodiscard]] math::Color SampleTextureLevel(const Texture& texture, f32 u, f32 v, f32 level,
                                             const SamplerDesc& sampler) noexcept;

}  // namespace cyber::software
