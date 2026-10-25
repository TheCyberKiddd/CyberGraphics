#include <cyber/memory/heap_allocator.h>
#include <cyber/software/lighting.h>
#include <cyber/test/test.h>

#include <cmath>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::f32;
using cyber::core::u32;
using cyber::math::Color;
using cyber::math::Vec3;

namespace
{

constexpr f32 kInversePi = 0.31830988618f;

Varyings MakeSurface(const Vec3& normal)
{
    Varyings varyings{};
    varyings.normal = normal;
    varyings.worldPosition = Vec3{0.0f, 0.0f, 0.0f};
    varyings.uv = cyber::math::Vec2{0.5f, 0.5f};
    varyings.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    return varyings;
}

LightingData BaseLighting()
{
    LightingData data{};
    data.eyePosition = Vec3{0.0f, 0.0f, 5.0f};
    data.ambientColor = Vec3{0.0f, 0.0f, 0.0f};
    data.material.albedo = Color{1.0f, 1.0f, 1.0f, 1.0f};
    data.material.specular = 0.0f;
    data.material.shininess = 16.0f;
    data.material.ambient = 0.0f;
    return data;
}

}  // namespace

CG_TEST(software_lighting_lambert)
{
    LightingData data = BaseLighting();
    data.directional[0].direction = Vec3{0.0f, -1.0f, 0.0f};
    data.directional[0].color = Vec3{1.0f, 1.0f, 1.0f};
    data.directional[0].intensity = 1.0f;
    data.directionalCount = 1;

    // Normal faces the light: full Lambert term.
    const Color facing = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 1.0f, 0.0f}), &data);
    CG_EXPECT_NEAR(facing.r, kInversePi, 1.0e-4f);
    CG_EXPECT_NEAR(facing.g, kInversePi, 1.0e-4f);

    // Normal perpendicular to the light: no diffuse.
    const Color side = LambertBlinnPhongPixelShader(MakeSurface(Vec3{1.0f, 0.0f, 0.0f}), &data);
    CG_EXPECT_NEAR(side.r, 0.0f, 1.0e-6f);

    // Normal away from the light: no diffuse.
    const Color away = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, -1.0f, 0.0f}), &data);
    CG_EXPECT_NEAR(away.r, 0.0f, 1.0e-6f);

    // Intensity scales the result linearly.
    data.directional[0].intensity = 0.5f;
    const Color half = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 1.0f, 0.0f}), &data);
    CG_EXPECT_NEAR(half.r, kInversePi * 0.5f, 1.0e-4f);
}

CG_TEST(software_lighting_point_attenuation)
{
    LightingData data = BaseLighting();
    data.points[0].position = Vec3{0.0f, 2.0f, 0.0f};
    data.points[0].color = Vec3{1.0f, 1.0f, 1.0f};
    data.points[0].intensity = 1.0f;
    data.points[0].range = 10.0f;
    data.pointCount = 1;

    const Color lit = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 1.0f, 0.0f}), &data);
    const f32 distance = 2.0f;
    const f32 attenuation = 1.0f / (1.0f + 0.09f * distance + 0.032f * distance * distance);
    const f32 rangeFactor = 1.0f - distance / 10.0f;
    const f32 expected = kInversePi * attenuation * rangeFactor;
    CG_EXPECT_NEAR(lit.r, expected, 1.0e-4f);

    // Beyond the range the light contributes nothing.
    data.points[0].range = 1.0f;
    const Color outside = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 1.0f, 0.0f}), &data);
    CG_EXPECT_NEAR(outside.r, 0.0f, 1.0e-6f);
}

CG_TEST(software_lighting_blinn_phong_specular)
{
    LightingData data = BaseLighting();
    data.material.albedo = Color{0.0f, 0.0f, 0.0f, 1.0f};
    data.material.specular = 0.5f;
    data.material.shininess = 16.0f;
    data.directional[0].direction = Vec3{0.0f, 0.0f, -1.0f};
    data.directional[0].color = Vec3{1.0f, 1.0f, 1.0f};
    data.directional[0].intensity = 1.0f;
    data.directionalCount = 1;

    // Light and eye are both along +Z, so the half vector equals the normal and
    // the specular term takes its maximum.
    const Color peak = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 0.0f, 1.0f}), &data);
    CG_EXPECT_NEAR(peak.r, 0.5f, 1.0e-5f);

    // A steeper exponent narrows the highlight but does not change the peak.
    data.material.shininess = 64.0f;
    const Color narrower = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 0.0f, 1.0f}), &data);
    CG_EXPECT_NEAR(narrower.r, 0.5f, 1.0e-5f);
}

CG_TEST(software_lighting_texture_modulation)
{
    HeapAllocator heap;
    TextureDesc textureDesc{};
    textureDesc.width = 1;
    textureDesc.height = 1;
    textureDesc.format = TextureFormat::RGBA32F;
    textureDesc.generateMips = false;
    auto created = Texture::Create(textureDesc, heap);
    CG_REQUIRE(static_cast<bool>(created));
    Texture texture = std::move(created.Value());
    texture.SetTexel(0, 0, 0, Color{0.5f, 0.5f, 0.5f, 1.0f});

    LightingData data = BaseLighting();
    data.directional[0].direction = Vec3{0.0f, -1.0f, 0.0f};
    data.directional[0].intensity = 1.0f;
    data.directionalCount = 1;
    data.sampler.addressU = AddressMode::ClampToEdge;
    data.sampler.addressV = AddressMode::ClampToEdge;
    data.texture = &texture;

    const Color lit = LambertBlinnPhongPixelShader(MakeSurface(Vec3{0.0f, 1.0f, 0.0f}), &data);
    CG_EXPECT_NEAR(lit.r, 0.5f * kInversePi, 1.0e-4f);

    // A null user data returns the vertex color unchanged.
    data.texture = nullptr;
    const Varyings surface = MakeSurface(Vec3{0.0f, 1.0f, 0.0f});
    const Color fallback = LambertBlinnPhongPixelShader(surface, nullptr);
    CG_EXPECT_NEAR(fallback.r, surface.color.r, 1.0e-6f);
}

CG_TEST(software_lighting_vertex_color_tint)
{
    LightingData data = BaseLighting();
    data.directional[0].direction = Vec3{0.0f, -1.0f, 0.0f};
    data.directionalCount = 1;

    Varyings surface = MakeSurface(Vec3{0.0f, 1.0f, 0.0f});
    surface.color = Color{0.5f, 1.0f, 1.0f, 1.0f};
    const Color tinted = LambertBlinnPhongPixelShader(surface, &data);
    CG_EXPECT_NEAR(tinted.r, 0.5f * kInversePi, 1.0e-4f);
    CG_EXPECT_NEAR(tinted.g, kInversePi, 1.0e-4f);
}
