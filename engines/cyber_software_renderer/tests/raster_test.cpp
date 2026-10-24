#include <cyber/math/color.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/raster.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::i32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::core::usize;
using cyber::math::Color;
using cyber::math::Vec2;

namespace
{

bool CreateBuffer(HeapAllocator& heap, i32 width, i32 height, PixelFormat format, Framebuffer& out)
{
    FramebufferDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = format;
    auto created = Framebuffer::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

bool SameColor(const Color& lhs, const Color& rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

u64 CountChanged(const Framebuffer& target, const Color& background)
{
    u64 count = 0;
    for (i32 y = 0; y < target.Height(); ++y)
    {
        for (i32 x = 0; x < target.Width(); ++x)
        {
            if (!SameColor(target.GetPixel(x, y), background))
            {
                ++count;
            }
        }
    }
    return count;
}

RasterTriangle MakeTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& color)
{
    RasterTriangle triangle{};
    triangle.v0.position = a;
    triangle.v0.color = color;
    triangle.v1.position = b;
    triangle.v1.color = color;
    triangle.v2.position = c;
    triangle.v2.color = color;
    return triangle;
}

RasterTriangle MakeTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Color& ca,
                            const Color& cb, const Color& cc)
{
    RasterTriangle triangle{};
    triangle.v0 = RasterVertex{a, ca};
    triangle.v1 = RasterVertex{b, cb};
    triangle.v2 = RasterVertex{c, cc};
    return triangle;
}

}  // namespace

CG_TEST(software_raster_line_basic)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 8, 8, PixelFormat::RGBA32F, target));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};

    target.Clear(background);
    DrawLine(target, Vec2{0.5f, 0.5f}, Vec2{7.5f, 0.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    for (i32 x = 0; x < 8; ++x)
    {
        CG_EXPECT_TRUE(SameColor(target.GetPixel(x, 0), red));
    }

    target.Clear(background);
    DrawLine(target, Vec2{3.5f, 0.5f}, Vec2{3.5f, 7.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    for (i32 y = 0; y < 8; ++y)
    {
        CG_EXPECT_TRUE(SameColor(target.GetPixel(3, y), red));
    }

    target.Clear(background);
    DrawLine(target, Vec2{0.5f, 0.5f}, Vec2{7.5f, 7.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    for (i32 index = 0; index < 8; ++index)
    {
        CG_EXPECT_TRUE(SameColor(target.GetPixel(index, index), red));
    }

    target.Clear(background);
    DrawLine(target, Vec2{7.5f, 7.5f}, Vec2{0.5f, 0.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    for (i32 index = 0; index < 8; ++index)
    {
        CG_EXPECT_TRUE(SameColor(target.GetPixel(index, index), red));
    }

    target.Clear(background);
    DrawLine(target, Vec2{3.5f, 3.5f}, Vec2{3.5f, 3.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{1});
    CG_EXPECT_TRUE(SameColor(target.GetPixel(3, 3), red));

    target.Clear(background);
    DrawLine(target, Vec2{0.5f, 0.5f}, Vec2{7.5f, 3.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 0), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(7, 3), red));

    target.Clear(background);
    DrawLine(target, Vec2{0.5f, 0.5f}, Vec2{3.5f, 7.5f}, red);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 0), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(3, 7), red));
}

CG_TEST(software_raster_line_clipping)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 8, 8, PixelFormat::RGBA32F, target));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color green{0.0f, 1.0f, 0.0f, 1.0f};

    target.Clear(background);
    DrawLine(target, Vec2{-10.0f, 3.5f}, Vec2{4.5f, 3.5f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{5});
    for (i32 x = 0; x < 5; ++x)
    {
        CG_EXPECT_TRUE(SameColor(target.GetPixel(x, 3), green));
    }

    target.Clear(background);
    DrawLine(target, Vec2{2.5f, -10.0f}, Vec2{2.5f, 3.5f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{4});
    for (i32 y = 0; y < 4; ++y)
    {
        CG_EXPECT_TRUE(SameColor(target.GetPixel(2, y), green));
    }

    target.Clear(background);
    DrawLine(target, Vec2{100.0f, 100.0f}, Vec2{200.0f, 200.0f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{0});

    target.Clear(background);
    DrawLine(target, Vec2{-10.0f, 3.5f}, Vec2{-1.0f, 3.5f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{0});

    target.Clear(background);
    DrawLine(target, Vec2{3.5f, 8.0f}, Vec2{3.5f, 20.0f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{0});

    target.Clear(background);
    DrawLine(target, Vec2{0.0f, 0.0f}, Vec2{8.0f, 8.0f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 0), green));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(7, 7), green));

    target.Clear(background);
    DrawLine(target, Vec2{std::nanf(""), 1.0f}, Vec2{3.0f, 4.0f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{0});

    // Endpoints beyond the guard band are dropped before any clipping ratio
    // could overflow; a long but finite segment still clips normally.
    target.Clear(background);
    DrawLine(target, Vec2{-1.0e30f, 3.5f}, Vec2{1.0e30f, 3.5f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{0});

    target.Clear(background);
    DrawLine(target, Vec2{-100000.0f, 3.5f}, Vec2{100000.0f, 3.5f}, green);
    CG_EXPECT_EQ(CountChanged(target, background), u64{8});
}

CG_TEST(software_raster_triangle_fill_rule)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 4, 4, PixelFormat::RGBA32F, target));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    const RasterTriangle triangle =
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, Vec2{0.0f, 4.0f}, red);

    target.Clear(background);
    FillTriangle(target, triangle);
    CG_EXPECT_EQ(CountChanged(target, background), u64{6});

    // Six pixel centers satisfy x + y <= 2. The hypotenuse is a bottom-right
    // edge for clockwise winding, so centers exactly on x + y == 3 are owned by
    // the adjacent triangle, not by this one.
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 0), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(2, 0), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(1, 1), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 2), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(3, 0), background));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(2, 1), background));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 3), background));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(3, 1), background));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(2, 2), background));

    target.Clear(background);
    RasterStats stats{};
    const RasterTriangle triangles[1] = {triangle};
    const auto status = RasterizeTriangles(target, Span<const RasterTriangle>(triangles), nullptr,
                                           heap, {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.triangles, u64{1});
    CG_EXPECT_EQ(stats.trianglesDrawn, u64{1});
    CG_EXPECT_EQ(stats.trianglesSkipped, u64{0});
    CG_EXPECT_EQ(stats.pixels, u64{6});
    CG_EXPECT_EQ(CountChanged(target, background), u64{6});
}

CG_TEST(software_raster_square_partition)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 4, 4, PixelFormat::RGBA32F, target));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    const Color green{0.0f, 1.0f, 0.0f, 1.0f};

    // Both triangles are clockwise in screen space. They share the diagonal
    // (0, 0) - (4, 4), which is top-left only in the first triangle (it goes
    // up-left in that winding order).
    const RasterTriangle triangles[2] = {
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, Vec2{4.0f, 4.0f}, red),
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{4.0f, 4.0f}, Vec2{0.0f, 4.0f}, green),
    };

    target.Clear(background);
    RasterStats stats{};
    auto status = RasterizeTriangles(target, Span<const RasterTriangle>(triangles), nullptr, heap,
                                     {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.trianglesDrawn, u64{2});
    CG_EXPECT_EQ(stats.pixels, u64{16});
    CG_EXPECT_EQ(CountChanged(target, background), u64{16});
    CG_EXPECT_TRUE(SameColor(target.GetPixel(1, 1), red));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(3, 3), red));

    // Reversing submission order must not change coverage. Only the first
    // triangle owns the shared diagonal in both submissions.
    target.Clear(background);
    const RasterTriangle reversed[2] = {triangles[1], triangles[0]};
    stats = RasterStats{};
    status =
        RasterizeTriangles(target, Span<const RasterTriangle>(reversed), nullptr, heap, {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.pixels, u64{16});
    CG_EXPECT_EQ(CountChanged(target, background), u64{16});
    CG_EXPECT_TRUE(SameColor(target.GetPixel(1, 1), red));
}

CG_TEST(software_raster_vertex_fan)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 8, 8, PixelFormat::RGBA32F, target));

    const Color background{0.0f, 0.0f, 0.0f, 0.0f};
    const Color colors[4] = {
        Color{1.0f, 0.0f, 0.0f, 1.0f},
        Color{0.0f, 1.0f, 0.0f, 1.0f},
        Color{0.0f, 0.0f, 1.0f, 1.0f},
        Color{1.0f, 1.0f, 0.0f, 1.0f},
    };

    // The center is (4.5, 4.5), exactly the sample point of pixel (4, 4).
    const Vec2 center{4.5f, 4.5f};
    const RasterTriangle triangles[4] = {
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{8.0f, 0.0f}, center, colors[0]),
        MakeTriangle(Vec2{8.0f, 0.0f}, Vec2{8.0f, 8.0f}, center, colors[1]),
        MakeTriangle(Vec2{8.0f, 8.0f}, Vec2{0.0f, 8.0f}, center, colors[2]),
        MakeTriangle(Vec2{0.0f, 8.0f}, Vec2{0.0f, 0.0f}, center, colors[3]),
    };

    target.Clear(background);
    RasterStats stats{};
    const auto status = RasterizeTriangles(target, Span<const RasterTriangle>(triangles), nullptr,
                                           heap, {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.trianglesDrawn, u64{4});
    CG_EXPECT_EQ(stats.trianglesSkipped, u64{0});
    CG_EXPECT_EQ(stats.pixels, u64{64});
    CG_EXPECT_EQ(CountChanged(target, background), u64{64});
    CG_EXPECT_TRUE(target.GetPixel(4, 4).a == 1.0f);
}

CG_TEST(software_raster_winding_and_interpolation)
{
    HeapAllocator heap;
    Framebuffer target;
    Framebuffer reversed;
    CG_REQUIRE(CreateBuffer(heap, 8, 8, PixelFormat::RGBA32F, target));
    CG_REQUIRE(CreateBuffer(heap, 8, 8, PixelFormat::RGBA32F, reversed));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    const Color green{0.0f, 1.0f, 0.0f, 1.0f};
    const Color blue{0.0f, 0.0f, 1.0f, 1.0f};

    const Vec2 a{0.5f, 0.5f};
    const Vec2 b{7.5f, 0.5f};
    const Vec2 c{0.5f, 7.5f};
    const RasterTriangle triangle = MakeTriangle(a, b, c, red, green, blue);
    const RasterTriangle swapped = MakeTriangle(a, c, b, red, blue, green);

    target.Clear(background);
    FillTriangle(target, triangle);
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 0), red));

    // Pixel (6, 0) sits on the top edge, which is top-left and therefore owned
    // by this triangle. Its weights are 1/7 for v0 and 6/7 for v1.
    const Color topEdge = target.GetPixel(6, 0);
    CG_EXPECT_NEAR(topEdge.r, 1.0f / 7.0f, 1.0e-5f);
    CG_EXPECT_NEAR(topEdge.g, 6.0f / 7.0f, 1.0e-5f);
    CG_EXPECT_NEAR(topEdge.b, 0.0f, 1.0e-5f);

    // Pixel (0, 6) sits on the left edge, which is also top-left.
    const Color leftEdge = target.GetPixel(0, 6);
    CG_EXPECT_NEAR(leftEdge.r, 1.0f / 7.0f, 1.0e-5f);
    CG_EXPECT_NEAR(leftEdge.g, 0.0f, 1.0e-5f);
    CG_EXPECT_NEAR(leftEdge.b, 6.0f / 7.0f, 1.0e-5f);

    // Pixel (2, 2) has barycentric weights 3/7, 2/7, 2/7.
    const Color middle = target.GetPixel(2, 2);
    CG_EXPECT_NEAR(middle.r, 3.0f / 7.0f, 1.0e-5f);
    CG_EXPECT_NEAR(middle.g, 2.0f / 7.0f, 1.0e-5f);
    CG_EXPECT_NEAR(middle.b, 2.0f / 7.0f, 1.0e-5f);
    CG_EXPECT_NEAR(middle.a, 1.0f, 1.0e-5f);

    // The top-right and bottom-left vertices lie on a top-left edge and a
    // right edge. The half-open rule leaves them to the adjacent triangles.
    CG_EXPECT_TRUE(SameColor(target.GetPixel(7, 0), background));
    CG_EXPECT_TRUE(SameColor(target.GetPixel(0, 7), background));

    reversed.Clear(background);
    FillTriangle(reversed, swapped);
    CG_EXPECT_EQ(std::memcmp(target.Data(), reversed.Data(), target.DataSizeBytes()), 0);
}

CG_TEST(software_raster_rejects_invalid_geometry)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 8, 8, PixelFormat::RGBA32F, target));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color red{1.0f, 0.0f, 0.0f, 1.0f};
    const RasterTriangle triangles[6] = {
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, Vec2{8.0f, 0.0f}, red),
        MakeTriangle(Vec2{2.0f, 2.0f}, Vec2{2.0f, 2.0f}, Vec2{2.0f, 2.0f}, red),
        MakeTriangle(Vec2{std::nanf(""), 0.0f}, Vec2{4.0f, 0.0f}, Vec2{0.0f, 4.0f}, red),
        MakeTriangle(Vec2{20000.0f, 0.0f}, Vec2{0.0f, 0.0f}, Vec2{0.0f, 4.0f}, red),
        MakeTriangle(Vec2{100.0f, 100.0f}, Vec2{200.0f, 100.0f}, Vec2{100.0f, 200.0f}, red),
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{8.0f, 0.0f}, Vec2{0.0f, 8.0f}, red),
    };

    target.Clear(background);
    FillTriangle(target, triangles[0]);
    CG_EXPECT_EQ(CountChanged(target, background), u64{0});

    RasterStats stats{};
    const auto status = RasterizeTriangles(target, Span<const RasterTriangle>(triangles), nullptr,
                                           heap, {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.triangles, u64{6});
    CG_EXPECT_EQ(stats.trianglesDrawn, u64{1});
    CG_EXPECT_EQ(stats.trianglesSkipped, u64{5});
    CG_EXPECT_EQ(stats.tiles, u64{1});

    target.Clear(background);
    const RasterTriangle empty[1] = {triangles[5]};
    stats = RasterStats{};
    CG_EXPECT_TRUE(static_cast<bool>(
        RasterizeTriangles(target, Span<const RasterTriangle>(), nullptr, heap, {}, &stats)));
    CG_EXPECT_EQ(stats.triangles, u64{0});
    CG_EXPECT_EQ(stats.pixels, u64{0});

    stats = RasterStats{};
    CG_EXPECT_FALSE(static_cast<bool>(RasterizeTriangles(
        target, Span<const RasterTriangle>(nullptr, 1), nullptr, heap, {}, &stats)));

    if constexpr (sizeof(usize) > 4)
    {
        // Bin indices are 32-bit; a larger span is rejected before it is read.
        const RasterTriangle one = triangles[5];
        stats = RasterStats{};
        const auto overflow = RasterizeTriangles(
            target, Span<const RasterTriangle>(&one, std::numeric_limits<usize>::max()), nullptr,
            heap, {}, &stats);
        CG_REQUIRE(!static_cast<bool>(overflow));
        CG_EXPECT_EQ(overflow.Error().code, cyber::core::ErrorCode::Overflow);
        CG_EXPECT_EQ(stats.triangles, u64{0});
    }

    Framebuffer invalid;
    CG_EXPECT_FALSE(static_cast<bool>(
        RasterizeTriangles(invalid, Span<const RasterTriangle>(), nullptr, heap, {})));
    FillTriangle(invalid, empty[0]);
    DrawLine(invalid, Vec2{0.0f, 0.0f}, Vec2{1.0f, 1.0f}, red);
}

CG_TEST(software_raster_stats_match_writes)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 16, 16, PixelFormat::RGBA32F, target));

    const Color background{0.25f, 0.5f, 0.75f, 1.0f};
    const RasterTriangle triangles[2] = {
        MakeTriangle(Vec2{1.0f, 1.0f}, Vec2{7.0f, 1.0f}, Vec2{3.0f, 6.0f},
                     Color{1.0f, 0.0f, 0.0f, 1.0f}),
        MakeTriangle(Vec2{9.0f, 9.0f}, Vec2{15.0f, 9.0f}, Vec2{12.0f, 14.0f},
                     Color{0.0f, 1.0f, 0.0f, 1.0f}),
    };

    target.Clear(background);
    RasterStats stats{};
    const auto status = RasterizeTriangles(target, Span<const RasterTriangle>(triangles), nullptr,
                                           heap, {}, &stats);
    CG_REQUIRE(static_cast<bool>(status));
    CG_EXPECT_EQ(stats.triangles, u64{2});
    CG_EXPECT_EQ(stats.trianglesDrawn, u64{2});
    CG_EXPECT_EQ(stats.trianglesSkipped, u64{0});
    CG_EXPECT_EQ(stats.pixels, CountChanged(target, background));
    CG_EXPECT_TRUE(stats.pixels > u64{0});
}

CG_TEST(software_raster_unorm_quantization)
{
    HeapAllocator heap;
    Framebuffer target;
    CG_REQUIRE(CreateBuffer(heap, 4, 4, PixelFormat::RGBA8Unorm, target));

    const Color background{0.0f, 0.0f, 0.0f, 1.0f};
    const Color gray{0.5f, 0.5f, 0.5f, 1.0f};
    const RasterTriangle triangle =
        MakeTriangle(Vec2{0.0f, 0.0f}, Vec2{4.0f, 0.0f}, Vec2{0.0f, 4.0f}, gray);

    target.Clear(background);
    FillTriangle(target, triangle);
    CG_EXPECT_EQ(CountChanged(target, background), u64{6});

    const u8* data = target.Data();
    CG_EXPECT_EQ(data[0], u8{128});
    CG_EXPECT_EQ(data[1], u8{128});
    CG_EXPECT_EQ(data[2], u8{128});
    CG_EXPECT_EQ(data[3], u8{255});
}
