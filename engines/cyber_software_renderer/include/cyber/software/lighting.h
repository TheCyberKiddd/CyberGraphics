#pragma once

#include <cyber/core/types.h>
#include <cyber/math/color.h>
#include <cyber/math/vec3.h>
#include <cyber/software/shading.h>
#include <cyber/software/texture.h>

namespace cyber::software
{

using cyber::core::f32;
using cyber::core::u32;

// A light infinitely far away. `direction` is the direction the light travels,
// so a surface facing the light has `dot(N, -direction) > 0`.
struct DirectionalLight
{
    math::Vec3 direction{-0.5f, -1.0f, -0.3f};
    math::Vec3 color{1.0f, 1.0f, 1.0f};
    f32 intensity = 1.0f;
};

// A point light with a squared-distance attenuation and a hard range cut.
struct PointLight
{
    math::Vec3 position;
    math::Vec3 color{1.0f, 1.0f, 1.0f};
    f32 intensity = 1.0f;
    // Distance at which the light fades to zero. Must be positive.
    f32 range = 10.0f;
};

// Linear surface response. `specular` is the Blinn-Phong strength and
// `shininess` its exponent; both are untuned physical approximations.
struct SurfaceMaterial
{
    math::Color albedo{0.8f, 0.8f, 0.8f, 1.0f};
    f32 specular = 0.25f;
    f32 shininess = 32.0f;
    f32 ambient = 0.08f;
};

inline constexpr u32 kMaxDirectionalLights = 2;
inline constexpr u32 kMaxPointLights = 4;

// User data for `LambertBlinnPhongPixelShader`.
struct LightingData
{
    math::Vec3 eyePosition;
    math::Vec3 ambientColor{0.10f, 0.11f, 0.14f};
    // Optional albedo texture. Null leaves the material albedo unmodulated.
    const Texture* texture = nullptr;
    SamplerDesc sampler{};
    // Explicit mip level used when sampling the texture.
    f32 mipLevel = 0.0f;
    SurfaceMaterial material{};

    DirectionalLight directional[kMaxDirectionalLights]{};
    u32 directionalCount = 0;

    PointLight points[kMaxPointLights]{};
    u32 pointCount = 0;
};

// Lambert diffuse plus Blinn-Phong specular, evaluated in linear space.
//
// `varyings.normal` is normalized; a zero normal falls back to +Z so a broken
// vertex cannot produce NaN. The albedo is multiplied by the vertex color and,
// when `texture` is set, by the sampled texel. Everything is linear; the result
// is not clamped, and present-time quantization handles the range.
//
// Returns the interpolated vertex color when `userData` is null.
[[nodiscard]] math::Color LambertBlinnPhongPixelShader(const Varyings& varyings,
                                                       const void* userData) noexcept;

}  // namespace cyber::software
