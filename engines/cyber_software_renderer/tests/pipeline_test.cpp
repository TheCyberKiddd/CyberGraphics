#include <cyber/jobs/job_system.h>
#include <cyber/math/constants.h>
#include <cyber/math/mat4.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/software/camera.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/pipeline.h>
#include <cyber/software/raster.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstring>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec3;
using cyber::math::Vec4;

namespace
{

constexpr i32 kSize = 8;

bool CreateTargets(HeapAllocator& heap, i32 width, i32 height, Framebuffer& color,
                   DepthBuffer& depth)
{
    FramebufferDesc colorDesc{};
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = PixelFormat::RGBA32F;
    auto createdColor = Framebuffer::Create(colorDesc, heap);
    if (!createdColor)
    {
        return false;
    }
    color = std::move(createdColor.Value());

    DepthBufferDesc depthDesc{};
    depthDesc.width = width;
    depthDesc.height = height;
    auto createdDepth = DepthBuffer::Create(depthDesc, heap);
    if (!createdDepth)
    {
        return false;
    }
    depth = std::move(createdDepth.Value());
    return true;
}

PipelineDesc MakeDesc(i32 width, i32 height)
{
    PipelineDesc desc{};
    desc.viewport.x = 0.0f;
    desc.viewport.y = 0.0f;
    desc.viewport.width = static_cast<f32>(width);
    desc.viewport.height = static_cast<f32>(height);
    return desc;
}

// Full-screen quad in NDC at a constant depth, wound counter-clockwise in the
// y-up clip space (front-facing for right-handed geometry).
void MakeQuad(f32 depth, const Color& color, Vertex (&vertices)[6])
{
    const Vec3 positions[4] = {
        Vec3{-1.0f, -1.0f, depth},
        Vec3{1.0f, -1.0f, depth},
        Vec3{1.0f, 1.0f, depth},
        Vec3{-1.0f, 1.0f, depth},
    };
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    for (i32 index = 0; index < 6; ++index)
    {
        vertices[index] = Vertex{positions[indices[index]], color};
    }
}

u64 CountColor(const Framebuffer& target, const Color& color)
{
    u64 count = 0;
    for (i32 y = 0; y < target.Height(); ++y)
    {
        for (i32 x = 0; x < target.Width(); ++x)
        {
            const Color pixel = target.GetPixel(x, y);
            if (pixel.r == color.r && pixel.g == color.g && pixel.b == color.b &&
                pixel.a == color.a)
            {
                ++count;
            }
        }
    }
    return count;
}

bool AllFinite(const Framebuffer& target)
{
    for (i32 y = 0; y < target.Height(); ++y)
    {
        for (i32 x = 0; x < target.Width(); ++x)
        {
            const Color pixel = target.GetPixel(x, y);
            if (!std::isfinite(pixel.r) || !std::isfinite(pixel.g) || !std::isfinite(pixel.b) ||
                !std::isfinite(pixel.a))
            {
                return false;
            }
        }
    }
    return true;
}

// Unit cube centered at the origin, 12 triangles wound counter-clockwise when
// seen from outside (right-handed, +Y up, +Z toward the viewer).
constexpr usize kCubeTriangleCount = 12;
constexpr usize kCubeVertexCount = kCubeTriangleCount * 3;

void MakeCube(const Color& color, Vertex (&vertices)[kCubeVertexCount])
{
    const Vec3 corners[8] = {
        Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, -1.0f},
        Vec3{-1.0f, 1.0f, -1.0f},  Vec3{-1.0f, -1.0f, 1.0f}, Vec3{1.0f, -1.0f, 1.0f},
        Vec3{1.0f, 1.0f, 1.0f},    Vec3{-1.0f, 1.0f, 1.0f},
    };
    const int faces[36] = {
        4, 5, 6, 4, 6, 7,  // +Z
        1, 0, 3, 1, 3, 2,  // -Z
        0, 4, 7, 0, 7, 3,  // -X
        5, 1, 2, 5, 2, 6,  // +X
        7, 6, 2, 7, 2, 3,  // +Y
        0, 1, 5, 0, 5, 4,  // -Y
    };
    for (usize index = 0; index < kCubeVertexCount; ++index)
    {
        vertices[index] = Vertex{corners[faces[index]], color};
    }
}

ClipVertex MakeClip(f32 x, f32 y, f32 z, f32 w, const Color& color)
{
    ClipVertex vertex;
    vertex.position = Vec4{x, y, z, w};
    vertex.color = color;
    return vertex;
}

}  // namespace

CG_TEST(software_pipeline_viewport_and_depth)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, kSize, kSize, color, depth));

    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    Vertex vertices[6];
    MakeQuad(0.5f, red, vertices);

    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    PipelineStats stats{};
    const auto status = DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(vertices), color,
                                      depth, nullptr, heap, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.triangles, u64{2});
    CG_EXPECT_EQ(stats.trianglesClipped, u64{0});
    CG_EXPECT_EQ(stats.trianglesCulled, u64{0});
    CG_EXPECT_EQ(stats.trianglesSubmitted, u64{2});
    CG_EXPECT_EQ(stats.pixels, u64{kSize * kSize});
    CG_EXPECT_EQ(stats.pixelsRejected, u64{0});
    CG_EXPECT_EQ(CountColor(color, red), u64{kSize * kSize});
    for (i32 y = 0; y < kSize; ++y)
    {
        for (i32 x = 0; x < kSize; ++x)
        {
            CG_EXPECT_EQ(depth.Get(x, y), 0.5f);
        }
    }

    // The viewport rectangle offsets and scales the image, and nothing outside
    // it is touched.
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    PipelineDesc offset = MakeDesc(kSize, kSize);
    offset.viewport.x = 2.0f;
    offset.viewport.y = 1.0f;
    offset.viewport.width = 4.0f;
    offset.viewport.height = 4.0f;
    stats = PipelineStats{};
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(offset, Span<const Vertex>(vertices), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.pixels, u64{16});
    CG_EXPECT_EQ(CountColor(color, red), u64{16});
    CG_EXPECT_TRUE(color.GetPixel(3, 2).r == 1.0f);
    // Reading a member of a returned temporary through CG_EXPECT_EQ's
    // reference would dangle; store the pixel first.
    const Color outsideLeft = color.GetPixel(1, 2);
    const Color outsideRight = color.GetPixel(6, 2);
    const Color outsideTop = color.GetPixel(3, 0);
    const Color outsideBottom = color.GetPixel(3, 5);
    CG_EXPECT_EQ(outsideLeft.r, 0.0f);
    CG_EXPECT_EQ(outsideRight.r, 0.0f);
    CG_EXPECT_EQ(outsideTop.r, 0.0f);
    CG_EXPECT_EQ(outsideBottom.r, 0.0f);
    CG_EXPECT_TRUE(depth.Get(3, 2) == 0.5f);
    CG_EXPECT_EQ(depth.Get(0, 0), 0.0f);

    // The viewport y flip puts positive NDC y at the top of the screen.
    const Vertex topHalf[3] = {
        Vertex{Vec3{0.0f, 0.0f, 0.5f}, red},
        Vertex{Vec3{1.0f, 0.0f, 0.5f}, red},
        Vertex{Vec3{0.0f, 1.0f, 0.5f}, red},
    };
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    stats = PipelineStats{};
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(topHalf),
                                               color, depth, nullptr, heap, &stats)));
    CG_EXPECT_TRUE(stats.pixels > u64{0});
    CG_EXPECT_TRUE(color.GetPixel(4, 1).r > 0.0f);
    const Color belowTriangle = color.GetPixel(4, 6);
    const Color besideTriangle = color.GetPixel(1, 1);
    CG_EXPECT_EQ(belowTriangle.r, 0.0f);
    CG_EXPECT_EQ(besideTriangle.r, 0.0f);
}

CG_TEST(software_pipeline_culling)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, kSize, kSize, color, depth));

    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    Vertex front[6];
    MakeQuad(0.5f, red, front);
    Vertex back[6];
    MakeQuad(0.5f, Color{0.0f, 0.0f, 1.0f, 1.0f}, back);

    // Reverse the winding of the second quad: it becomes clockwise on screen.
    for (i32 triangle = 0; triangle < 2; ++triangle)
    {
        std::swap(back[triangle * 3 + 1], back[triangle * 3 + 2]);
    }

    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    PipelineStats stats{};
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(front),
                                               color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesSubmitted, u64{2});
    CG_EXPECT_EQ(stats.trianglesCulled, u64{0});

    // Back-face culling rejects the reversed quad regardless of draw order.
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(back),
                                               color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesSubmitted, u64{0});
    CG_EXPECT_EQ(stats.trianglesCulled, u64{2});
    CG_EXPECT_EQ(stats.pixels, u64{0});

    // Cull mode None draws it; front-face culling rejects the first winding.
    PipelineDesc none = MakeDesc(kSize, kSize);
    none.cullMode = CullMode::None;
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(none, Span<const Vertex>(back), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesSubmitted, u64{2});

    PipelineDesc cullFront = MakeDesc(kSize, kSize);
    cullFront.cullMode = CullMode::Front;
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(cullFront, Span<const Vertex>(front), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesCulled, u64{2});
    CG_EXPECT_EQ(stats.pixels, u64{0});

    // Declaring clockwise as the front face flips the classification.
    PipelineDesc clockwise = MakeDesc(kSize, kSize);
    clockwise.frontFace = FrontFace::Clockwise;
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(clockwise, Span<const Vertex>(back), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesSubmitted, u64{2});
    CG_EXPECT_TRUE(CountColor(color, Color{0.0f, 0.0f, 1.0f, 1.0f}) == u64{kSize * kSize});
}

CG_TEST(software_pipeline_depth_order)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, kSize, kSize, color, depth));

    const Color near{1.0f, 0.0f, 0.0f, 1.0f};
    const Color far{0.0f, 1.0f, 0.0f, 1.0f};
    Vertex nearQuad[6];
    Vertex farQuad[6];
    MakeQuad(0.75f, near, nearQuad);
    MakeQuad(0.25f, far, farQuad);

    // Far first, then near.
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    PipelineStats stats{};
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(farQuad),
                                               color, depth, nullptr, heap, &stats)));
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(nearQuad),
                                               color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.pixels, u64{kSize * kSize});
    CG_EXPECT_EQ(stats.pixelsRejected, u64{0});
    CG_EXPECT_EQ(CountColor(color, near), u64{kSize * kSize});
    CG_EXPECT_EQ(depth.Get(4, 4), 0.75f);

    // Near first, then far: the far quad loses every covered sample.
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(nearQuad),
                                               color, depth, nullptr, heap, &stats)));
    stats = PipelineStats{};
    CG_REQUIRE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(farQuad),
                                               color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.pixels, u64{0});
    CG_EXPECT_EQ(stats.pixelsRejected, u64{kSize * kSize});
    CG_EXPECT_EQ(CountColor(color, near), u64{kSize * kSize});

    // Equal depth resolves by submission order and is deterministic.
    Vertex sameDepth[6];
    MakeQuad(0.75f, far, sameDepth);
    CG_REQUIRE(
        static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(sameDepth),
                                        color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(CountColor(color, far), u64{kSize * kSize});
}

CG_TEST(software_pipeline_near_plane_clipping)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 32, 32, color, depth));

    Camera camera;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    camera.fovYRadians = 60.0f * cyber::math::kDegreesToRadians;

    PipelineDesc desc = MakeDesc(32, 32);
    desc.view = CameraView(camera);
    desc.projection = CameraProjection(camera, 1.0f);

    // One vertex is behind the camera, the other two are in front; the
    // triangle straddles the near plane.
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    // The band crosses the middle of the screen, so the clipped result is
    // observable rather than a sliver outside the viewport.
    const Vertex crossing[3] = {
        Vertex{Vec3{0.0f, 0.5f, 0.5f}, red},
        Vertex{Vec3{-2.0f, 0.0f, -1.0f}, red},
        Vertex{Vec3{2.0f, 0.0f, -1.0f}, red},
    };
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    PipelineStats stats{};
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(desc, Span<const Vertex>(crossing), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.triangles, u64{1});
    CG_EXPECT_EQ(stats.trianglesClipped, u64{1});
    CG_EXPECT_EQ(stats.trianglesInvalid, u64{0});
    CG_EXPECT_EQ(stats.trianglesOffscreen, u64{0});
    CG_EXPECT_TRUE(stats.trianglesSubmitted == u64{1} || stats.trianglesSubmitted == u64{2});
    CG_EXPECT_TRUE(stats.pixels > u64{0});
    CG_EXPECT_TRUE(AllFinite(color));

    // A triangle entirely behind the camera is culled instead of wrapping
    // around the screen.
    const Vertex behind[3] = {
        Vertex{Vec3{-1.0f, -1.0f, 1.0f}, red},
        Vertex{Vec3{1.0f, -1.0f, 1.0f}, red},
        Vertex{Vec3{0.0f, 1.0f, 2.0f}, red},
    };
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    stats = PipelineStats{};
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(desc, Span<const Vertex>(behind), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesCulled, u64{1});
    CG_EXPECT_EQ(stats.pixels, u64{0});
    CG_EXPECT_EQ(CountColor(color, Color{1.0f, 0.0f, 0.0f, 1.0f}), u64{0});

    // A vertex that is in front of the near plane but far off axis leaves the
    // rasterizer's guard band and is reported, not rasterized.
    const Vertex guard[3] = {
        Vertex{Vec3{400.0f, 0.0f, -0.12f}, red},
        Vertex{Vec3{0.0f, -1.0f, -2.0f}, red},
        Vertex{Vec3{0.0f, 1.0f, -2.0f}, red},
    };
    stats = PipelineStats{};
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(desc, Span<const Vertex>(guard), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.trianglesOffscreen, u64{1});
    CG_EXPECT_EQ(stats.pixels, u64{0});
}

CG_TEST(software_pipeline_perspective_correct_color)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, kSize, kSize, color, depth));
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);

    DepthRasterTriangle triangle{};
    triangle.v0.position = cyber::math::Vec2{0.5f, 0.5f};
    triangle.v0.color = Color{1.0f, 0.0f, 0.0f, 1.0f};
    triangle.v0.depth = 0.9f;
    triangle.v0.inverseW = 1.0f;

    triangle.v1.position = cyber::math::Vec2{7.5f, 0.5f};
    triangle.v1.color = Color{0.0f, 1.0f, 0.0f, 1.0f};
    triangle.v1.depth = 0.5f;
    triangle.v1.inverseW = 0.25f;

    triangle.v2.position = cyber::math::Vec2{0.5f, 7.5f};
    triangle.v2.color = Color{0.0f, 0.0f, 1.0f, 1.0f};
    triangle.v2.depth = 0.1f;
    triangle.v2.inverseW = 1.0f;

    RasterStats stats{};
    const auto status = RasterizeDepthTestedTriangles(
        color, depth, Span<const DepthRasterTriangle>(&triangle, 1), nullptr, heap, {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.pixelsRejected, u64{0});

    // Pixel (2, 2) has barycentric weights 3/7, 2/7, 2/7.
    const Color pixel = color.GetPixel(2, 2);
    CG_EXPECT_NEAR(pixel.r, 3.0f / 5.5f, 2.0e-5f);
    CG_EXPECT_NEAR(pixel.g, 0.5f / 5.5f, 2.0e-5f);
    CG_EXPECT_NEAR(pixel.b, 2.0f / 5.5f, 2.0e-5f);
    CG_EXPECT_NEAR(pixel.a, 1.0f, 2.0e-5f);
    CG_EXPECT_NEAR(depth.Get(2, 2), 3.9f / 7.0f, 2.0e-5f);

    // The affine result would have been 3/7, 2/7, 2/7; perspective correction
    // must differ from it.
    CG_EXPECT_TRUE(std::fabs(pixel.r - 3.0f / 7.0f) > 1.0e-3f);
    CG_EXPECT_TRUE(std::fabs(pixel.g - 2.0f / 7.0f) > 1.0e-3f);

    // Vertex samples stay exact: (0.5, 0.5) is the sample of pixel (0, 0).
    const Color corner = color.GetPixel(0, 0);
    CG_EXPECT_NEAR(corner.r, 1.0f, 1.0e-5f);
    CG_EXPECT_NEAR(corner.g, 0.0f, 1.0e-5f);
    CG_EXPECT_NEAR(corner.b, 0.0f, 1.0e-5f);
}

CG_TEST(software_pipeline_clipper_output)
{
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    const Color green{0.0f, 1.0f, 0.0f, 1.0f};
    const Color blue{0.0f, 0.0f, 1.0f, 1.0f};

    // All inside: unchanged, same order.
    {
        const ClipPolygon polygon = ClipTriangleNearPlane(MakeClip(-1.0f, -1.0f, 0.0f, 1.0f, red),
                                                          MakeClip(1.0f, -1.0f, 0.5f, 1.0f, green),
                                                          MakeClip(0.0f, 1.0f, 1.0f, 1.0f, blue));
        CG_EXPECT_EQ(polygon.count, u32{3});
        CG_EXPECT_TRUE(
            cyber::math::ApproxEqual(polygon.vertices[0].position, Vec4{-1.0f, -1.0f, 0.0f, 1.0f}));
        CG_EXPECT_TRUE(
            cyber::math::ApproxEqual(polygon.vertices[1].position, Vec4{1.0f, -1.0f, 0.5f, 1.0f}));
        CG_EXPECT_TRUE(
            cyber::math::ApproxEqual(polygon.vertices[2].position, Vec4{0.0f, 1.0f, 1.0f, 1.0f}));
        CG_EXPECT_EQ(polygon.vertices[1].color.g, 1.0f);
    }

    // One vertex outside produces a quad with two intersections on z = w.
    {
        // v0 outside: z - w = 2, v1 and v2 inside.
        const ClipPolygon polygon = ClipTriangleNearPlane(MakeClip(0.0f, 2.0f, 4.0f, 2.0f, red),
                                                          MakeClip(-2.0f, 0.0f, 0.0f, 2.0f, green),
                                                          MakeClip(2.0f, 0.0f, 0.0f, 2.0f, blue));
        CG_EXPECT_EQ(polygon.count, u32{4});
        for (u32 index = 0; index < polygon.count; ++index)
        {
            const Vec4& position = polygon.vertices[index].position;
            CG_EXPECT_TRUE(position.z <= position.w + 1.0e-6f);
        }
        // Edge v0 -> v1 intersects at the midpoint: t = 2 / (2 - (-2)).
        CG_EXPECT_NEAR(polygon.vertices[0].position.x, -1.0f, 1.0e-5f);
        CG_EXPECT_NEAR(polygon.vertices[0].position.y, 1.0f, 1.0e-5f);
        CG_EXPECT_NEAR(polygon.vertices[0].position.z / polygon.vertices[0].position.w, 1.0f,
                       1.0e-5f);
        CG_EXPECT_NEAR(polygon.vertices[0].color.r, 0.5f, 1.0e-5f);
        CG_EXPECT_NEAR(polygon.vertices[0].color.g, 0.5f, 1.0e-5f);
        // Edge v2 -> v0 intersects at the midpoint as well.
        CG_EXPECT_NEAR(polygon.vertices[3].position.x, 1.0f, 1.0e-5f);
        CG_EXPECT_NEAR(polygon.vertices[3].color.b, 0.5f, 1.0e-5f);
    }

    // Two vertices outside produce one triangle.
    {
        const ClipPolygon polygon = ClipTriangleNearPlane(MakeClip(0.0f, 2.0f, 4.0f, 2.0f, red),
                                                          MakeClip(0.0f, 3.0f, 4.0f, 2.0f, green),
                                                          MakeClip(0.0f, 0.0f, 0.0f, 2.0f, blue));
        CG_EXPECT_EQ(polygon.count, u32{3});
        CG_EXPECT_NEAR(polygon.vertices[0].position.z / polygon.vertices[0].position.w, 1.0f,
                       1.0e-5f);
        CG_EXPECT_NEAR(polygon.vertices[2].position.z / polygon.vertices[2].position.w, 1.0f,
                       1.0e-5f);
        CG_EXPECT_TRUE(polygon.vertices[1].color.b == 1.0f);
    }

    // Everything outside produces nothing.
    {
        const ClipPolygon polygon = ClipTriangleNearPlane(MakeClip(0.0f, 0.0f, 4.0f, 2.0f, red),
                                                          MakeClip(1.0f, 0.0f, 4.0f, 2.0f, green),
                                                          MakeClip(0.0f, 1.0f, 4.0f, 2.0f, blue));
        CG_EXPECT_EQ(polygon.count, u32{0});
    }

    // A vertex exactly on the plane counts as inside and is not duplicated.
    {
        const ClipPolygon polygon = ClipTriangleNearPlane(MakeClip(0.0f, 0.0f, 2.0f, 2.0f, red),
                                                          MakeClip(-1.0f, 0.0f, 0.0f, 2.0f, green),
                                                          MakeClip(1.0f, 0.0f, 0.0f, 2.0f, blue));
        CG_EXPECT_EQ(polygon.count, u32{3});
        CG_EXPECT_TRUE(
            cyber::math::ApproxEqual(polygon.vertices[0].position, Vec4{0.0f, 0.0f, 2.0f, 2.0f}));
    }
}

CG_TEST(software_pipeline_cube_determinism)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 96, 96, color, depth));

    Vertex cube[kCubeVertexCount];
    MakeCube(Color{0.8f, 0.6f, 0.3f, 1.0f}, cube);

    OrbitCamera orbit;
    orbit.target = Vec3{0.0f, 0.0f, 0.0f};
    orbit.distance = 5.0f;
    orbit.yawRadians = 1.0f;
    orbit.pitchRadians = 0.45f;

    PipelineDesc desc = MakeDesc(96, 96);
    desc.model = Mat4::RotationY(0.35f);
    desc.view = OrbitCameraView(orbit);
    desc.projection = OrbitCameraProjection(orbit, 1.0f);

    Framebuffer serialColor;
    DepthBuffer serialDepth;
    CG_REQUIRE(CreateTargets(heap, 96, 96, serialColor, serialDepth));
    serialColor.Clear(Color{0.02f, 0.03f, 0.05f, 1.0f});
    serialDepth.Clear(0.0f);
    PipelineStats serialStats{};
    CG_REQUIRE(static_cast<bool>(DrawTriangles(desc, Span<const Vertex>(cube), serialColor,
                                               serialDepth, nullptr, heap, &serialStats)));

    CG_EXPECT_EQ(serialStats.triangles, u64{12});
    CG_EXPECT_EQ(serialStats.trianglesClipped, u64{0});
    CG_EXPECT_EQ(serialStats.trianglesOffscreen, u64{0});
    CG_EXPECT_EQ(serialStats.trianglesInvalid, u64{0});
    CG_EXPECT_EQ(serialStats.trianglesSubmitted, u64{6});
    CG_EXPECT_EQ(serialStats.trianglesCulled, u64{6});
    CG_EXPECT_TRUE(serialStats.pixels > u64{0});
    CG_EXPECT_TRUE(AllFinite(serialColor));

    // The front (+Z) face wins in the middle of the silhouette.
    const Color center = serialColor.GetPixel(48, 48);
    CG_EXPECT_TRUE(center.r > 0.0f && center.g > 0.0f);
    CG_EXPECT_TRUE(serialDepth.Get(48, 48) > 0.0f);
    CG_EXPECT_TRUE(serialDepth.Get(0, 0) == 0.0f);

    // Tile size and worker count never change the image or the depth buffer.
    const i32 tileSizes[3] = {16, 64, 128};
    for (i32 size : tileSizes)
    {
        Framebuffer parallelColor;
        DepthBuffer parallelDepth;
        CG_REQUIRE(CreateTargets(heap, 96, 96, parallelColor, parallelDepth));
        parallelColor.Clear(Color{0.02f, 0.03f, 0.05f, 1.0f});
        parallelDepth.Clear(0.0f);

        PipelineDesc other = desc;
        other.tileSize = size;
        PipelineStats stats{};
        CG_REQUIRE(static_cast<bool>(DrawTriangles(other, Span<const Vertex>(cube), parallelColor,
                                                   parallelDepth, nullptr, heap, &stats)));
        CG_EXPECT_EQ(
            std::memcmp(serialColor.Data(), parallelColor.Data(), serialColor.DataSizeBytes()), 0);
        CG_EXPECT_EQ(
            std::memcmp(serialDepth.Data(), parallelDepth.Data(), serialDepth.DataSizeBytes()), 0);
        CG_EXPECT_EQ(stats.pixels, serialStats.pixels);
    }
}

CG_TEST(software_pipeline_camera_inside_cube)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, 64, 64, color, depth));

    Vertex cube[kCubeVertexCount];
    MakeCube(Color{0.2f, 0.5f, 0.9f, 1.0f}, cube);

    Camera camera;
    camera.position = Vec3{0.0f, 0.0f, 0.0f};
    camera.yawRadians = 0.35f;
    camera.pitchRadians = 0.2f;
    camera.nearPlane = 0.05f;
    camera.farPlane = 100.0f;

    PipelineDesc desc = MakeDesc(64, 64);
    desc.view = CameraView(camera);
    desc.projection = CameraProjection(camera, 1.0f);
    // From inside a closed cube every face is back-facing, so the interior is
    // only visible with culling disabled.
    desc.cullMode = CullMode::None;

    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    PipelineStats stats{};
    CG_REQUIRE(static_cast<bool>(
        DrawTriangles(desc, Span<const Vertex>(cube), color, depth, nullptr, heap, &stats)));
    CG_EXPECT_TRUE(stats.trianglesClipped > u64{0});
    CG_EXPECT_TRUE(stats.pixels > u64{0});
    CG_EXPECT_TRUE(stats.trianglesInvalid == u64{0});
    CG_EXPECT_TRUE(AllFinite(color));

    // Every written depth stays inside the valid reverse-Z range.
    for (i32 y = 0; y < 64; ++y)
    {
        for (i32 x = 0; x < 64; ++x)
        {
            const f32 value = depth.Get(x, y);
            CG_EXPECT_TRUE(value >= 0.0f && value <= 1.0f + 1.0e-5f);
        }
    }
}

CG_TEST(software_pipeline_rejects_invalid_arguments)
{
    HeapAllocator heap;
    Framebuffer color;
    DepthBuffer depth;
    CG_REQUIRE(CreateTargets(heap, kSize, kSize, color, depth));

    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    Vertex vertices[6];
    MakeQuad(0.5f, red, vertices);
    const Span<const Vertex> quad(vertices);

    // Vertex count must be a multiple of three.
    PipelineStats stats{};
    CG_EXPECT_FALSE(
        static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(vertices, 4),
                                        color, depth, nullptr, heap, &stats)));

    // Null span with a non-zero count.
    CG_EXPECT_FALSE(
        static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(nullptr, 3),
                                        color, depth, nullptr, heap, &stats)));

    // Invalid viewport.
    PipelineDesc zeroViewport = MakeDesc(kSize, kSize);
    zeroViewport.viewport.width = 0.0f;
    CG_EXPECT_FALSE(
        static_cast<bool>(DrawTriangles(zeroViewport, quad, color, depth, nullptr, heap, &stats)));

    PipelineDesc nanViewport = MakeDesc(kSize, kSize);
    nanViewport.viewport.x = std::nanf("");
    CG_EXPECT_FALSE(
        static_cast<bool>(DrawTriangles(nanViewport, quad, color, depth, nullptr, heap, &stats)));

    // Mismatched depth dimensions.
    DepthBuffer small;
    DepthBufferDesc smallDesc{};
    smallDesc.width = 4;
    smallDesc.height = 4;
    auto createdSmall = DepthBuffer::Create(smallDesc, heap);
    CG_REQUIRE(static_cast<bool>(createdSmall));
    small = std::move(createdSmall.Value());
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawTriangles(MakeDesc(kSize, kSize), quad, color, small, nullptr, heap, &stats)));

    // Invalid targets.
    Framebuffer invalidColor;
    DepthBuffer invalidDepth;
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawTriangles(MakeDesc(kSize, kSize), quad, invalidColor, depth, nullptr, heap, &stats)));
    CG_EXPECT_FALSE(static_cast<bool>(
        DrawTriangles(MakeDesc(kSize, kSize), quad, color, invalidDepth, nullptr, heap, &stats)));

    // An empty span is a valid no-op.
    CG_EXPECT_TRUE(static_cast<bool>(DrawTriangles(MakeDesc(kSize, kSize), Span<const Vertex>(),
                                                   color, depth, nullptr, heap, &stats)));
    CG_EXPECT_EQ(stats.triangles, u64{0});

    // Invalid depth-triangle attributes are skipped rather than drawn.
    DepthRasterTriangle broken{};
    broken.v0.position = cyber::math::Vec2{0.5f, 0.5f};
    broken.v1.position = cyber::math::Vec2{7.5f, 0.5f};
    broken.v2.position = cyber::math::Vec2{0.5f, 7.5f};
    broken.v0.inverseW = 0.0f;
    RasterStats rasterStats{};
    color.Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    depth.Clear(0.0f);
    const auto status = RasterizeDepthTestedTriangles(
        color, depth, Span<const DepthRasterTriangle>(&broken, 1), nullptr, heap, {}, &rasterStats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(rasterStats.trianglesSkipped, u64{1});
    CG_EXPECT_EQ(rasterStats.pixels, u64{0});

    // Depth target validation on the raster entry point.
    CG_EXPECT_FALSE(static_cast<bool>(RasterizeDepthTestedTriangles(
        invalidColor, depth, Span<const DepthRasterTriangle>(&broken, 1), nullptr, heap)));
    CG_EXPECT_FALSE(static_cast<bool>(RasterizeDepthTestedTriangles(
        color, invalidDepth, Span<const DepthRasterTriangle>(&broken, 1), nullptr, heap)));
    CG_EXPECT_FALSE(static_cast<bool>(RasterizeDepthTestedTriangles(
        color, small, Span<const DepthRasterTriangle>(&broken, 1), nullptr, heap)));
}

CG_TEST(software_pipeline_depth_raster_matches_serial_with_jobs)
{
    HeapAllocator heap;
    Framebuffer serialColor;
    DepthBuffer serialDepth;
    Framebuffer jobColor;
    DepthBuffer jobDepth;
    CG_REQUIRE(CreateTargets(heap, 64, 64, serialColor, serialDepth));
    CG_REQUIRE(CreateTargets(heap, 64, 64, jobColor, jobDepth));

    // A grid of overlapping triangles at varying depths exercises the depth
    // test, tile binning, and perspective interpolation together.
    constexpr i32 kTriangles = 48;
    DepthRasterTriangle triangles[kTriangles];
    for (i32 index = 0; index < kTriangles; ++index)
    {
        const f32 x = static_cast<f32>((index * 37) % 48);
        const f32 y = static_cast<f32>((index * 23) % 48);
        const f32 depth = static_cast<f32>(index % 7) / 7.0f;
        const f32 inverseW = 0.25f + static_cast<f32>(index % 5) * 0.25f;
        DepthRasterTriangle& triangle = triangles[index];
        triangle.v0 = DepthRasterVertex{{x, y}, Color{1.0f, 0.0f, 0.0f, 1.0f}, depth, inverseW};
        triangle.v1 = DepthRasterVertex{
            {x + 24.0f, y + 4.0f}, Color{0.0f, 1.0f, 0.0f, 1.0f}, depth, inverseW};
        triangle.v2 = DepthRasterVertex{
            {x + 2.0f, y + 22.0f}, Color{0.0f, 0.0f, 1.0f, 1.0f}, depth, inverseW};
    }

    for (Framebuffer* target : {&serialColor, &jobColor})
    {
        target->Clear(Color{0.0f, 0.0f, 0.0f, 1.0f});
    }
    serialDepth.Clear(0.0f);
    jobDepth.Clear(0.0f);

    cyber::jobs::JobSystem jobSystem;
    cyber::jobs::JobSystem::Desc systemDesc{};
    auto createdJobs = cyber::jobs::JobSystem::Create(systemDesc, heap);
    CG_REQUIRE(static_cast<bool>(createdJobs));
    jobSystem = std::move(createdJobs.Value());

    RasterizerDesc serialDesc{};
    serialDesc.useJobs = false;
    RasterizerDesc jobDesc{};
    jobDesc.tileSize = 16;

    CG_REQUIRE(static_cast<bool>(RasterizeDepthTestedTriangles(
        serialColor, serialDepth, Span<const DepthRasterTriangle>(triangles), nullptr, heap,
        serialDesc, nullptr)));
    CG_REQUIRE(static_cast<bool>(RasterizeDepthTestedTriangles(
        jobColor, jobDepth, Span<const DepthRasterTriangle>(triangles), &jobSystem, heap, jobDesc,
        nullptr)));

    // Tile size and worker count never change the color or the depth bytes.
    CG_EXPECT_EQ(std::memcmp(serialColor.Data(), jobColor.Data(), serialColor.DataSizeBytes()), 0);
    CG_EXPECT_EQ(std::memcmp(serialDepth.Data(), jobDepth.Data(), serialDepth.DataSizeBytes()), 0);
    jobSystem.Shutdown();
}
