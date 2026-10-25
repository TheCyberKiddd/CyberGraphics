#include <cyber/math/mat4.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/software/camera.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/shader.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstring>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u64;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec2;
using cyber::math::Vec3;

namespace
{

struct SceneData
{
    TransformShaderData transform;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

void SceneVertex(const VertexInput& input, const void* userData, VertexStageOutput& output) noexcept
{
    const auto* scene = static_cast<const SceneData*>(userData);
    TransformVertexShader(input, &scene->transform, output);
}

Color ConstantPixel(const Varyings&, const void* userData) noexcept
{
    return static_cast<const SceneData*>(userData)->color;
}

Color UvPixel(const Varyings& varyings, const void*) noexcept
{
    return Color{varyings.uv.x, varyings.uv.y, 0.0f, 1.0f};
}

bool CreateTargets(HeapAllocator& heap, i32 size, Framebuffer& color, DepthBuffer& depth)
{
    FramebufferDesc colorDesc{};
    colorDesc.width = size;
    colorDesc.height = size;
    colorDesc.format = PixelFormat::RGBA32F;
    auto createdColor = Framebuffer::Create(colorDesc, heap);
    if (!createdColor)
    {
        return false;
    }
    color = std::move(createdColor.Value());

    DepthBufferDesc depthDesc{};
    depthDesc.width = size;
    depthDesc.height = size;
    auto createdDepth = DepthBuffer::Create(depthDesc, heap);
    if (!createdDepth)
    {
        return false;
    }
    depth = std::move(createdDepth.Value());
    return true;
}

void MakeQuad(f32 z, VertexInput (&vertices)[6])
{
    const Vec3 positions[4] = {
        Vec3{-1.0f, -1.0f, z},
        Vec3{1.0f, -1.0f, z},
        Vec3{1.0f, 1.0f, z},
        Vec3{-1.0f, 1.0f, z},
    };
    const Vec2 uvs[4] = {Vec2{0.0f, 0.0f}, Vec2{1.0f, 0.0f}, Vec2{1.0f, 1.0f}, Vec2{0.0f, 1.0f}};
    const i32 indices[6] = {0, 1, 2, 0, 2, 3};
    for (i32 index = 0; index < 6; ++index)
    {
        const i32 corner = indices[index];
        VertexInput vertex{};
        vertex.position = positions[corner];
        vertex.uv = uvs[corner];
        vertex.normal = Vec3{0.0f, 0.0f, 1.0f};
        vertex.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
        vertices[index] = vertex;
    }
}

}  // namespace

CG_TEST(software_shader_constant_color)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 8, color, depth));
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    SceneData scene;
    scene.color = Color{0.25f, 0.5f, 0.75f, 1.0f};

    VertexInput quad[6];
    MakeQuad(0.5f, quad);

    ShaderDesc desc{};
    desc.pipeline.viewport = Viewport{0.0f, 0.0f, 8.0f, 8.0f};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = &ConstantPixel;
    desc.userData = &scene;

    PipelineStats stats{};
    CG_REQUIRE(static_cast<bool>(DrawShadedTriangles(desc, Span<const VertexInput>(quad), color,
                                                     depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.triangles, u64{2});
    CG_EXPECT_EQ(stats.trianglesSubmitted, u64{2});
    CG_EXPECT_EQ(stats.pixels, u64{64});
    CG_EXPECT_EQ(stats.pixelsRejected, u64{0});

    for (i32 y = 0; y < 8; ++y)
    {
        for (i32 x = 0; x < 8; ++x)
        {
            const Color pixel = color.GetPixel(x, y);
            CG_EXPECT_NEAR(pixel.r, 0.25f, 1.0e-5f);
            CG_EXPECT_NEAR(pixel.g, 0.5f, 1.0e-5f);
            CG_EXPECT_NEAR(pixel.b, 0.75f, 1.0e-5f);
        }
    }
    CG_EXPECT_NEAR(depth.Get(4, 4), 0.5f, 1.0e-5f);
}

CG_TEST(software_shader_varying_interpolation)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 8, color, depth));
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    SceneData scene;
    VertexInput quad[6];
    MakeQuad(0.5f, quad);

    ShaderDesc desc{};
    desc.pipeline.viewport = Viewport{0.0f, 0.0f, 8.0f, 8.0f};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = &UvPixel;
    desc.userData = &scene;

    CG_REQUIRE(static_cast<bool>(
        DrawShadedTriangles(desc, Span<const VertexInput>(quad), color, depth, nullptr, heap)));

    // UVs increase with x and decrease with y because the viewport flips y.
    const Color topLeft = color.GetPixel(0, 0);
    const Color topRight = color.GetPixel(7, 0);
    const Color bottomLeft = color.GetPixel(0, 7);
    CG_EXPECT_TRUE(topRight.r > topLeft.r);
    CG_EXPECT_TRUE(bottomLeft.g < topLeft.g);
    CG_EXPECT_NEAR(topLeft.r, 0.5f / 8.0f, 2.0e-3f);
}

CG_TEST(software_shader_clipping_preserves_varyings)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 32, color, depth));
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    Camera camera;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    camera.fovYRadians = 60.0f * cyber::math::kDegreesToRadians;

    SceneData scene;
    scene.transform.modelViewProjection = CameraProjection(camera, 1.0f) * CameraView(camera);

    const VertexInput crossing[3] = {
        VertexInput{Vec3{0.0f, 0.5f, 0.5f}, Vec3{0.0f, 0.0f, 1.0f}, Vec2{0.0f, 0.0f},
                    Color{1.0f, 1.0f, 1.0f, 1.0f}},
        VertexInput{Vec3{-2.0f, 0.0f, -1.0f}, Vec3{0.0f, 0.0f, 1.0f}, Vec2{1.0f, 0.0f},
                    Color{1.0f, 1.0f, 1.0f, 1.0f}},
        VertexInput{Vec3{2.0f, 0.0f, -1.0f}, Vec3{0.0f, 0.0f, 1.0f}, Vec2{0.0f, 1.0f},
                    Color{1.0f, 1.0f, 1.0f, 1.0f}},
    };

    ShaderDesc desc{};
    desc.pipeline.viewport = Viewport{0.0f, 0.0f, 32.0f, 32.0f};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = &ConstantPixel;
    desc.userData = &scene;

    PipelineStats stats{};
    CG_REQUIRE(static_cast<bool>(DrawShadedTriangles(desc, Span<const VertexInput>(crossing), color,
                                                     depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesClipped, u64{1});
    CG_EXPECT_TRUE(stats.pixels > u64{0});
    CG_EXPECT_TRUE(stats.trianglesInvalid == u64{0});
}

CG_TEST(software_shader_rejects_invalid_arguments)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 8, color, depth));

    SceneData scene;
    VertexInput quad[6];
    MakeQuad(0.5f, quad);

    ShaderDesc desc{};
    desc.pipeline.viewport = Viewport{0.0f, 0.0f, 8.0f, 8.0f};
    desc.vertexShader = &SceneVertex;
    desc.pixelShader = &ConstantPixel;
    desc.userData = &scene;

    // A missing stage is rejected.
    ShaderDesc noVertex = desc;
    noVertex.vertexShader = nullptr;
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawShadedTriangles(noVertex, Span<const VertexInput>(quad), color, depth, nullptr, heap)));

    ShaderDesc noPixel = desc;
    noPixel.pixelShader = nullptr;
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawShadedTriangles(noPixel, Span<const VertexInput>(quad), color, depth, nullptr, heap)));

    // Vertex count must be a multiple of three.
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawShadedTriangles(desc, Span<const VertexInput>(quad, 4), color, depth, nullptr, heap)));

    // An invalid target is rejected.
    Framebuffer invalid;
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawShadedTriangles(desc, Span<const VertexInput>(quad), invalid, depth, nullptr, heap)));
}
