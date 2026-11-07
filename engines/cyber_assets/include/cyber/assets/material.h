#pragma once

#include <cyber/assets/asset.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/string.h>

namespace cyber::assets
{

using cyber::core::f32;
using cyber::memory::Allocator;
using cyber::memory::String;

/// Alpha handling for a material.
enum class AlphaMode : u8
{
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

/// Texture reference inside a material. A zero `image` means the slot is unused.
/// Asset IDs are derived from the image's virtual path, so a material can be
/// cooked before the image is.
struct TextureReference
{
    AssetId image = kInvalidAssetId;
    u32 texCoord = 0;
    u32 sampler = 0;
};

/// Metallic-roughness material in the glTF 2.0 sense. This is import data, not a
/// shader; the renderer maps it onto its own parameter blocks.
struct MaterialAsset
{
    explicit MaterialAsset(Allocator& allocator) noexcept : name(allocator) {}

    MaterialAsset(const MaterialAsset&) = delete;
    MaterialAsset& operator=(const MaterialAsset&) = delete;
    MaterialAsset(MaterialAsset&&) noexcept = default;
    MaterialAsset& operator=(MaterialAsset&&) noexcept = default;
    ~MaterialAsset() = default;

    String name;
    cyber::math::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    f32 metallicFactor = 1.0f;
    f32 roughnessFactor = 1.0f;
    cyber::math::Vec3 emissiveFactor{0.0f, 0.0f, 0.0f};
    f32 normalScale = 1.0f;
    f32 occlusionStrength = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    f32 alphaCutoff = 0.5f;
    bool doubleSided = false;

    TextureReference baseColorTexture{};
    TextureReference metallicRoughnessTexture{};
    TextureReference normalTexture{};
    TextureReference occlusionTexture{};
    TextureReference emissiveTexture{};

    /// Clamps and validates the scalar ranges. Returns `InvalidArgument` for a
    /// negative scale or an out-of-range factor.
    [[nodiscard]] ImportStatus Validate() const noexcept;
};

}  // namespace cyber::assets
