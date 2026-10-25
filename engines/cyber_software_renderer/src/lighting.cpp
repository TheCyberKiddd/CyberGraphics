#include <cyber/software/lighting.h>

#include <cmath>

namespace cyber::software
{

namespace
{

// 1 / pi, the Lambertian normalization.
constexpr f32 kInversePi = 0.31830988618f;
constexpr f32 kPointConstantAttenuation = 1.0f;
constexpr f32 kPointLinearAttenuation = 0.09f;
constexpr f32 kPointQuadraticAttenuation = 0.032f;

f32 Max(f32 a, f32 b) noexcept
{
    return a > b ? a : b;
}

}  // namespace

math::Color LambertBlinnPhongPixelShader(const Varyings& varyings, const void* userData) noexcept
{
    const auto* data = static_cast<const LightingData*>(userData);
    if (data == nullptr)
    {
        return varyings.color;
    }

    math::Vec3 normal = cyber::math::Normalize(varyings.normal);
    if (cyber::math::LengthSquared(normal) < 1.0e-12f)
    {
        normal = math::Vec3{0.0f, 0.0f, 1.0f};
    }
    math::Vec3 toEye = cyber::math::Normalize(data->eyePosition - varyings.worldPosition);
    if (cyber::math::LengthSquared(toEye) < 1.0e-12f)
    {
        toEye = math::Vec3{0.0f, 0.0f, 1.0f};
    }

    math::Color albedo = data->material.albedo;
    if (data->texture != nullptr)
    {
        const math::Color texel = SampleTextureLevel(*data->texture, varyings.uv.x, varyings.uv.y,
                                                     data->mipLevel, data->sampler);
        albedo.r *= texel.r;
        albedo.g *= texel.g;
        albedo.b *= texel.b;
        albedo.a *= texel.a;
    }
    // Vertex color tints the surface.
    albedo.r *= varyings.color.r;
    albedo.g *= varyings.color.g;
    albedo.b *= varyings.color.b;
    albedo.a *= varyings.color.a;

    math::Vec3 diffuse{0.0f, 0.0f, 0.0f};
    math::Vec3 specular{0.0f, 0.0f, 0.0f};

    const auto accumulate =
        [&](const math::Vec3& toLight, const math::Vec3& lightColor, f32 intensity) noexcept
    {
        const f32 nDotL = cyber::math::Dot(normal, toLight);
        if (nDotL <= 0.0f)
        {
            return;
        }
        diffuse += lightColor * (nDotL * intensity * kInversePi);
        const math::Vec3 halfVector = cyber::math::Normalize(toLight + toEye);
        const f32 nDotH = Max(cyber::math::Dot(normal, halfVector), 0.0f);
        if (nDotH > 0.0f)
        {
            specular += lightColor * (std::pow(nDotH, data->material.shininess) * intensity *
                                      data->material.specular);
        }
    };

    for (u32 index = 0; index < data->directionalCount && index < kMaxDirectionalLights; ++index)
    {
        const DirectionalLight& light = data->directional[index];
        const math::Vec3 toLight = cyber::math::Normalize(-light.direction);
        if (cyber::math::LengthSquared(toLight) < 1.0e-12f)
        {
            continue;
        }
        accumulate(toLight, light.color, light.intensity);
    }

    for (u32 index = 0; index < data->pointCount && index < kMaxPointLights; ++index)
    {
        const PointLight& light = data->points[index];
        const math::Vec3 offset = light.position - varyings.worldPosition;
        const f32 distanceSquared = cyber::math::LengthSquared(offset);
        const f32 distance = std::sqrt(distanceSquared);
        if (distance <= 0.0f || light.range <= 0.0f || distance >= light.range)
        {
            continue;
        }
        const f32 attenuation = kPointConstantAttenuation /
                                (kPointConstantAttenuation + kPointLinearAttenuation * distance +
                                 kPointQuadraticAttenuation * distanceSquared);
        // Smooth the hard range cut so a surface does not pop at the boundary.
        const f32 rangeFactor = 1.0f - distance / light.range;
        accumulate(offset / distance, light.color, light.intensity * attenuation * rangeFactor);
    }

    const f32 ambient = data->material.ambient;
    math::Color result;
    result.r = albedo.r * (data->ambientColor.x * ambient + diffuse.x) + specular.x;
    result.g = albedo.g * (data->ambientColor.y * ambient + diffuse.y) + specular.y;
    result.b = albedo.b * (data->ambientColor.z * ambient + diffuse.z) + specular.z;
    result.a = albedo.a;
    return result;
}

}  // namespace cyber::software
