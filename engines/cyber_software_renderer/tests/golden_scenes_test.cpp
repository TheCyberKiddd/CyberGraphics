#include <cyber/math/mat4.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/software/camera.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/golden_image.h>
#include <cyber/software/image_io.h>
#include <cyber/software/lighting.h>
#include <cyber/software/mesh.h>
#include <cyber/software/shader.h>
#include <cyber/software/texture.h>
#include <cyber/test/test.h>

#include <cstring>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::usize;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec2;
using cyber::math::Vec3;

namespace
{

constexpr i32 kWidth = 640;
constexpr i32 kHeight = 360;

struct SceneUniforms
{
    TransformShaderData transform;
    LightingData lighting;
};

void SceneVertex(const VertexInput& input, const void* userData, VertexStageOutput& output) noexcept
{
    TransformVertexShader(input, &static_cast<const SceneUniforms*>(userData)->transform, output);
}

Color ScenePixel(const Varyings& varyings, const void* userData) noexcept
{
    return LambertBlinnPhongPixelShader(varyings,
                                        &static_cast<const SceneUniforms*>(userData)->lighting);
}

Color UvPixel(const Varyings& varyings, const void*) noexcept
{
    return Color{varyings.uv.x, varyings.uv.y, 0.5f, 1.0f};
}

bool CreateTargets(HeapAllocator& heap, Framebuffer& color, DepthBuffer& depth)
{
    FramebufferDesc colorDesc{};
    colorDesc.width = kWidth;
    colorDesc.height = kHeight;
    colorDesc.format = PixelFormat::RGBA32F;
    auto createdColor = Framebuffer::Create(colorDesc, heap);
    if (!createdColor)
    {
        return false;
    }
    color = std::move(createdColor.Value());

    DepthBufferDesc depthDesc{};
    depthDesc.width = kWidth;
    depthDesc.height = kHeight;
    auto createdDepth = DepthBuffer::Create(depthDesc, heap);
    if (!createdDepth)
    {
        return false;
    }
    depth = std::move(createdDepth.Value());
    return true;
}

void BuildCube(Mesh& mesh)
{
    const Vec3 corners[8] = {
        Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, -1.0f},
        Vec3{-1.0f, 1.0f, -1.0f},  Vec3{-1.0f, -1.0f, 1.0f}, Vec3{1.0f, -1.0f, 1.0f},
        Vec3{1.0f, 1.0f, 1.0f},    Vec3{-1.0f, 1.0f, 1.0f},
    };
    struct Face
    {
        Vec3 normal;
        u32 corners[4];
    };
    const Face faces[6] = {
        Face{Vec3{0.0f, 0.0f, 1.0f}, {4, 5, 6, 7}},  Face{Vec3{0.0f, 0.0f, -1.0f}, {1, 0, 3, 2}},
        Face{Vec3{-1.0f, 0.0f, 0.0f}, {0, 4, 7, 3}}, Face{Vec3{1.0f, 0.0f, 0.0f}, {5, 1, 2, 6}},
        Face{Vec3{0.0f, 1.0f, 0.0f}, {7, 6, 2, 3}},  Face{Vec3{0.0f, -1.0f, 0.0f}, {0, 1, 5, 4}},
    };
    const Vec2 uvs[4] = {Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{1.0f, 1.0f}, Vec2{0.0f, 1.0f}};

    for (const Face& face : faces)
    {
        const u32 order[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (const auto& triangle : order)
        {
            for (u32 corner = 0; corner < 3; ++corner)
            {
                const u32 slot = triangle[corner];
                VertexInput vertex{};
                vertex.position = corners[face.corners[slot]];
                vertex.normal = face.normal;
                vertex.uv = uvs[slot];
                vertex.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
                (void)mesh.vertices.TryPushBack(vertex);
            }
        }
    }
}

void BuildQuad(Mesh& mesh, f32 z)
{
    const Vec3 positions[4] = {
        Vec3{-1.0f, -1.0f, z},
        Vec3{1.0f, -1.0f, z},
        Vec3{1.0f, 1.0f, z},
        Vec3{-1.0f, 1.0f, z},
    };
    const Vec2 uvs[4] = {Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{1.0f, 1.0f}, Vec2{0.0f, 1.0f}};
    const i32 indices[6] = {0, 1, 2, 0, 2, 3};
    for (i32 slot : indices)
    {
        VertexInput vertex{};
        vertex.position = positions[slot];
        vertex.normal = Vec3{0.0f, 0.0f, 1.0f};
        vertex.uv = uvs[slot];
        vertex.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
        (void)mesh.vertices.TryPushBack(vertex);
    }
}

bool MakeCheckerTexture(HeapAllocator& heap, Texture& texture)
{
    TextureDesc desc{};
    desc.width = 8;
    desc.height = 8;
    desc.format = TextureFormat::RGBA32F;
    desc.generateMips = true;
    auto created = Texture::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    texture = std::move(created.Value());

    for (i32 y = 0; y < 8; ++y)
    {
        for (i32 x = 0; x < 8; ++x)
        {
            const bool light = ((x / 2) + (y / 2)) % 2 == 0;
            (void)texture.SetTexel(0, x, y,
                                   light ? Color{0.85f, 0.75f, 0.55f, 1.0f}
                                         : Color{0.15f, 0.18f, 0.22f, 1.0f});
        }
    }
    texture.GenerateMips();
    return true;
}

usize CopyText(char* destination, const char* source)
{
    usize offset = 0;
    while (source[offset] != '\0')
    {
        destination[offset] = source[offset];
        ++offset;
    }
    return offset;
}

void BuildPath(char* out, const char* directory, const char* name, const char* extension)
{
    usize offset = CopyText(out, directory);
    offset += CopyText(out + offset, "/");
    offset += CopyText(out + offset, name);
    offset += CopyText(out + offset, extension);
    out[offset] = '\0';
}

bool CheckGolden(const char* name, const Framebuffer& actual, HeapAllocator& heap)
{
    (void)cyber::platform::CreateDirectories("golden_actual");

    char referencePath[512];
    BuildPath(referencePath, CYBER_GOLDEN_DIR, name, ".ppm");
    char actualPath[512];
    BuildPath(actualPath, "golden_actual", name, ".ppm");
    char differencePath[512];
    BuildPath(differencePath, "golden_actual", name, ".diff.ppm");

    auto reference = LoadImage(referencePath, heap);
    if (!reference)
    {
        (void)WriteImage(actualPath, actual, heap, ImageWriteDesc{ImageFormat::Ppm, true});
        CG_FAIL(referencePath);
        return false;
    }

    const GoldenImageResult result = CompareImages(actual, reference.Value());
    if (result.status != GoldenImageStatus::Ok)
    {
        (void)WriteImage(actualPath, actual, heap, ImageWriteDesc{ImageFormat::Ppm, true});
        (void)WriteDifferenceImage(differencePath, actual, reference.Value(), heap);
        CG_FAIL(actualPath);
        return false;
    }
    return true;
}

bool RenderMesh(HeapAllocator& heap, const Mesh& mesh, SceneUniforms& uniforms,
                PixelShaderFn pixelShader, Framebuffer& color, DepthBuffer& depth)
{
    ShaderDesc desc{};
    desc.pipeline.viewport =
        Viewport{0.0f, 0.0f, static_cast<f32>(kWidth), static_cast<f32>(kHeight)};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = pixelShader;
    desc.userData = &uniforms;
    return static_cast<bool>(DrawShadedTriangles(
        desc, Span<const VertexInput>(mesh.vertices.Data(), mesh.vertices.Size()), color, depth,
        nullptr, heap));
}

void ConfigureLighting(LightingData& lighting, const Vec3& eye, const Texture* texture)
{
    lighting.eyePosition = eye;
    lighting.ambientColor = Vec3{0.08f, 0.09f, 0.12f};
    lighting.texture = texture;
    lighting.sampler.addressU = AddressMode::Repeat;
    lighting.sampler.addressV = AddressMode::Repeat;
    lighting.sampler.minFilter = FilterMode::Nearest;
    lighting.sampler.magFilter = FilterMode::Nearest;
    lighting.mipLevel = 0.0f;
    lighting.material.albedo = Color{1.0f, 1.0f, 1.0f, 1.0f};
    lighting.material.specular = 0.0f;
    lighting.material.shininess = 32.0f;
    lighting.material.ambient = 0.25f;
    lighting.directional[0].direction = cyber::math::Normalize(Vec3{-0.4f, -1.0f, -0.6f});
    lighting.directional[0].color = Vec3{1.0f, 0.98f, 0.95f};
    lighting.directional[0].intensity = 1.0f;
    lighting.directionalCount = 1;
}

}  // namespace

CG_TEST(software_golden_scene_checker_cube)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, color, depth));
    color.Clear(Color{0.02f, 0.03f, 0.05f, 1.0f});
    depth.Clear(0.0f);

    Mesh mesh(heap);
    BuildCube(mesh);
    Texture checker;
    CG_REQUIRE(MakeCheckerTexture(heap, checker));

    OrbitCamera orbit;
    orbit.distance = 4.5f;
    orbit.yawRadians = 0.6f;
    orbit.pitchRadians = 0.4f;
    const Mat4 model = Mat4::RotationY(0.4f);
    const Vec3 eye = OrbitCameraPosition(orbit);

    SceneUniforms uniforms{};
    uniforms.transform.modelViewProjection =
        OrbitCameraProjection(orbit, static_cast<f32>(kWidth) / static_cast<f32>(kHeight)) *
        OrbitCameraView(orbit) * model;
    uniforms.transform.modelToWorld = model;
    uniforms.transform.normalToWorld = model;
    uniforms.transform.eyePosition = eye;
    ConfigureLighting(uniforms.lighting, eye, &checker);

    CG_REQUIRE(RenderMesh(heap, mesh, uniforms, &ScenePixel, color, depth));
    CG_EXPECT_TRUE(CheckGolden("checker_cube", color, heap));
}

CG_TEST(software_golden_scene_lambert_cube)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, color, depth));
    color.Clear(Color{0.03f, 0.03f, 0.05f, 1.0f});
    depth.Clear(0.0f);

    Mesh mesh(heap);
    BuildCube(mesh);

    OrbitCamera orbit;
    orbit.distance = 4.0f;
    orbit.yawRadians = 0.5f;
    orbit.pitchRadians = 0.35f;
    const Mat4 model = Mat4::RotationY(0.25f);
    const Vec3 eye = OrbitCameraPosition(orbit);

    SceneUniforms uniforms{};
    uniforms.transform.modelViewProjection =
        OrbitCameraProjection(orbit, static_cast<f32>(kWidth) / static_cast<f32>(kHeight)) *
        OrbitCameraView(orbit) * model;
    uniforms.transform.modelToWorld = model;
    uniforms.transform.normalToWorld = model;
    uniforms.transform.eyePosition = eye;
    ConfigureLighting(uniforms.lighting, eye, nullptr);
    uniforms.lighting.material.albedo = Color{0.75f, 0.55f, 0.35f, 1.0f};

    CG_REQUIRE(RenderMesh(heap, mesh, uniforms, &ScenePixel, color, depth));
    CG_EXPECT_TRUE(CheckGolden("lambert_cube", color, heap));
}

CG_TEST(software_golden_scene_blinn_phong_cube)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, color, depth));
    color.Clear(Color{0.03f, 0.03f, 0.05f, 1.0f});
    depth.Clear(0.0f);

    Mesh mesh(heap);
    BuildCube(mesh);

    OrbitCamera orbit;
    orbit.distance = 4.0f;
    orbit.yawRadians = 0.5f;
    orbit.pitchRadians = 0.35f;
    const Mat4 model = Mat4::RotationY(0.25f);
    const Vec3 eye = OrbitCameraPosition(orbit);

    SceneUniforms uniforms{};
    uniforms.transform.modelViewProjection =
        OrbitCameraProjection(orbit, static_cast<f32>(kWidth) / static_cast<f32>(kHeight)) *
        OrbitCameraView(orbit) * model;
    uniforms.transform.modelToWorld = model;
    uniforms.transform.normalToWorld = model;
    uniforms.transform.eyePosition = eye;
    ConfigureLighting(uniforms.lighting, eye, nullptr);
    uniforms.lighting.material.albedo = Color{0.35f, 0.45f, 0.65f, 1.0f};
    uniforms.lighting.material.specular = 0.9f;
    uniforms.lighting.material.shininess = 48.0f;
    uniforms.lighting.material.ambient = 0.15f;

    CG_REQUIRE(RenderMesh(heap, mesh, uniforms, &ScenePixel, color, depth));
    CG_EXPECT_TRUE(CheckGolden("blinn_cube", color, heap));
}

CG_TEST(software_golden_scene_uv_gradient)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, color, depth));
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    Mesh mesh(heap);
    BuildQuad(mesh, 0.5f);

    SceneUniforms uniforms{};
    uniforms.transform.modelViewProjection = Mat4::Identity();
    uniforms.transform.modelToWorld = Mat4::Identity();
    uniforms.transform.normalToWorld = Mat4::Identity();

    ShaderDesc desc{};
    desc.pipeline.viewport =
        Viewport{0.0f, 0.0f, static_cast<f32>(kWidth), static_cast<f32>(kHeight)};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = &UvPixel;
    desc.userData = &uniforms;
    CG_REQUIRE(static_cast<bool>(DrawShadedTriangles(
        desc, Span<const VertexInput>(mesh.vertices.Data(), mesh.vertices.Size()), color, depth,
        nullptr, heap)));

    CG_EXPECT_TRUE(CheckGolden("uv_gradient", color, heap));
}

CG_TEST(software_golden_scene_depth_order)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, color, depth));
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    Mesh farMesh(heap);
    BuildQuad(farMesh, 0.4f);
    Mesh nearMesh(heap);
    BuildQuad(nearMesh, 0.9f);

    SceneUniforms farUniforms{};
    farUniforms.transform.modelViewProjection = Mat4::Identity();
    farUniforms.transform.modelToWorld = Mat4::Identity();
    farUniforms.transform.normalToWorld = Mat4::Identity();
    ConfigureLighting(farUniforms.lighting, Vec3{0.0f, 0.0f, 0.0f}, nullptr);
    farUniforms.lighting.material.albedo = Color{0.1f, 0.3f, 0.8f, 1.0f};
    farUniforms.lighting.material.ambient = 1.0f;

    SceneUniforms nearUniforms = farUniforms;
    nearUniforms.lighting.material.albedo = Color{0.9f, 0.3f, 0.1f, 1.0f};

    // Draw the far quad first; the nearer quad must win the depth test.
    CG_REQUIRE(RenderMesh(heap, farMesh, farUniforms, &ScenePixel, color, depth));
    CG_REQUIRE(RenderMesh(heap, nearMesh, nearUniforms, &ScenePixel, color, depth));

    CG_EXPECT_TRUE(CheckGolden("depth_order", color, heap));
}

CG_TEST(software_golden_scene_determinism)
{
    HeapAllocator heap;
    Framebuffer first;
    DepthBuffer firstDepth;
    Framebuffer second;
    DepthBuffer secondDepth;
    CG_REQUIRE(CreateTargets(heap, first, firstDepth));
    CG_REQUIRE(CreateTargets(heap, second, secondDepth));

    Mesh mesh(heap);
    BuildCube(mesh);

    OrbitCamera orbit;
    orbit.distance = 4.0f;
    orbit.yawRadians = 0.5f;
    orbit.pitchRadians = 0.35f;
    const Mat4 model = Mat4::RotationY(0.25f);
    const Vec3 eye = OrbitCameraPosition(orbit);

    SceneUniforms uniforms{};
    uniforms.transform.modelViewProjection =
        OrbitCameraProjection(orbit, static_cast<f32>(kWidth) / static_cast<f32>(kHeight)) *
        OrbitCameraView(orbit) * model;
    uniforms.transform.modelToWorld = model;
    uniforms.transform.normalToWorld = model;
    uniforms.transform.eyePosition = eye;
    ConfigureLighting(uniforms.lighting, eye, nullptr);

    first.Clear(Color{0.03f, 0.03f, 0.05f, 1.0f});
    firstDepth.Clear(0.0f);
    second.Clear(Color{0.03f, 0.03f, 0.05f, 1.0f});
    secondDepth.Clear(0.0f);

    CG_REQUIRE(RenderMesh(heap, mesh, uniforms, &ScenePixel, first, firstDepth));
    CG_REQUIRE(RenderMesh(heap, mesh, uniforms, &ScenePixel, second, secondDepth));

    CG_EXPECT_EQ(std::memcmp(first.Data(), second.Data(), first.DataSizeBytes()), 0);
}
