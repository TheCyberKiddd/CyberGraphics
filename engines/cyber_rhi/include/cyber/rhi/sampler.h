#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u8;

// Magnification, minification, or mip-level selection filter.
enum class FilterMode : u8
{
    // The nearest texel or mip level.
    Nearest = 0,
    // A weighted blend of the neighbouring texels or mip levels.
    Linear,
    // Count of enumerators, for table tests. Not a filter.
    Count
};

// Human-readable filter, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* FilterModeName(FilterMode mode) noexcept
{
    switch (mode)
    {
    case FilterMode::Nearest:
        return "nearest";
    case FilterMode::Linear:
        return "linear";
    case FilterMode::Count:
        break;
    }
    return "unknown";
}

// What a sample outside the 0..1 range reads.
enum class AddressMode : u8
{
    // Repeats the texture; the integer part of the coordinate wraps.
    Repeat = 0,
    // Repeats the texture, mirroring on every other tile.
    MirroredRepeat,
    // Clamps to the edge texel.
    ClampToEdge,
    // Reads `borderColor` outside the texture.
    ClampToBorder,
    // Count of enumerators, for table tests. Not an address mode.
    Count
};

// Human-readable address mode, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* AddressModeName(AddressMode mode) noexcept
{
    switch (mode)
    {
    case AddressMode::Repeat:
        return "repeat";
    case AddressMode::MirroredRepeat:
        return "mirrored_repeat";
    case AddressMode::ClampToEdge:
        return "clamp_to_edge";
    case AddressMode::ClampToBorder:
        return "clamp_to_border";
    case AddressMode::Count:
        break;
    }
    return "unknown";
}

// Value a `ClampToBorder` sampler returns outside the texture.
enum class BorderColor : u8
{
    // (0, 0, 0, 0). The usual choice for a color texture.
    TransparentBlack = 0,
    // (0, 0, 0, 1).
    OpaqueBlack,
    // (1, 1, 1, 1).
    OpaqueWhite,
    // Count of enumerators, for table tests. Not a border color.
    Count
};

// Human-readable border color, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* BorderColorName(BorderColor color) noexcept
{
    switch (color)
    {
    case BorderColor::TransparentBlack:
        return "transparent_black";
    case BorderColor::OpaqueBlack:
        return "opaque_black";
    case BorderColor::OpaqueWhite:
        return "opaque_white";
    case BorderColor::Count:
        break;
    }
    return "unknown";
}

// Comparison a depth-comparison sampler performs against the reference value.
// Ordered as `Less` means "the sampled depth is less than the reference", which
// with reverse-Z is the usual shadow look-up.
enum class CompareFunction : u8
{
    Never = 0,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
    // Count of enumerators, for table tests. Not a comparison.
    Count
};

// Human-readable comparison, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* CompareFunctionName(CompareFunction function) noexcept
{
    switch (function)
    {
    case CompareFunction::Never:
        return "never";
    case CompareFunction::Less:
        return "less";
    case CompareFunction::Equal:
        return "equal";
    case CompareFunction::LessEqual:
        return "less_equal";
    case CompareFunction::Greater:
        return "greater";
    case CompareFunction::NotEqual:
        return "not_equal";
    case CompareFunction::GreaterEqual:
        return "greater_equal";
    case CompareFunction::Always:
        return "always";
    case CompareFunction::Count:
        break;
    }
    return "unknown";
}

// Largest magnitude a `mipLodBias` may have. D3D12's range is [-16, 15.99]; the
// symmetric limit here is simpler to state and no renderer needs the extra
// 0.01.
inline constexpr float kMaxMipLodBias = 16.0f;

// Largest `maxLod` a descriptor may request when the texture has fewer levels.
// A sampler clamps rather than truncating: a request beyond this is a mistake
// in the caller's descriptor, not a texture property.
inline constexpr float kMaxSamplerLod = 1000.0f;

// Description of a sampler to create. Unlike a buffer or a texture, a zeroed
// descriptor is valid: trilinear, repeat on every axis, no anisotropy, and no
// depth comparison, which is what a caller that wants "just sample this
// texture" expects.
struct SamplerDesc
{
    // Filter applied when a texel covers less than one screen pixel. Also used
    // for the comparison, when enabled.
    FilterMode minFilter = FilterMode::Linear;
    // Filter applied when a texel covers more than one screen pixel.
    FilterMode magFilter = FilterMode::Linear;
    // Filter used between mip levels. `Linear` makes a full mip chain trilinear.
    FilterMode mipmapFilter = FilterMode::Linear;
    // Address mode for each axis. A 1D or 2D texture ignores the axes it does
    // not have.
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    // Value read outside the texture when an axis is `ClampToBorder`.
    BorderColor borderColor = BorderColor::TransparentBlack;
    // LOD bias added before mip selection. Clamped to +/- `kMaxMipLodBias`.
    float mipLodBias = 0.0f;
    // Samples taken along the major axis when `minFilter` and `magFilter` are
    // both linear. 1 means isotropic. Above 1 requires
    // `Feature::AnisotropicFiltering` and all three filters linear.
    u32 maxAnisotropy = 1;
    // Lowest and highest mip level the sampler may select. `minLod` must not
    // exceed `maxLod`, both must be finite and non-negative, and `maxLod` is at
    // most `kMaxSamplerLod`.
    float minLod = 0.0f;
    float maxLod = kMaxSamplerLod;
    // True when the sampler compares each sample against a reference value, as
    // a shadow map look-up does. The comparison uses `compareFunction`.
    bool compareEnabled = false;
    // Comparison performed when `compareEnabled` is true. Ignored otherwise.
    CompareFunction compareFunction = CompareFunction::Always;
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

}  // namespace cyber::rhi
