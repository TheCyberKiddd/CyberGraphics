#include <cyber/core/types.h>
#include <cyber/jobs/job_system.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/software/raster.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cyber::software
{
using cyber::core::f64;
using cyber::core::i64;
using cyber::core::u32;

namespace
{

// 24.8 fixed point: 8 fractional bits, so one pixel is 256 units.
constexpr i32 kSubpixelScale = 256;
// Pixel (x, y) is sampled at (x + 0.5, y + 0.5).
constexpr i32 kSampleOffset = kSubpixelScale / 2;
// Keeps every edge product inside 64 bits. The framebuffer limit is 16384.
constexpr i64 kMaxCoordinateFixed = static_cast<i64>(1) << 22;
constexpr i32 kDefaultTileSize = 64;
constexpr i32 kMinTileSize = 8;
constexpr i32 kMaxTileSize = 256;

// E(p) = a * p.x + b * p.y + c, with p in fixed-point units. Edge constants of
// non-top-left edges carry the -1 bias.
struct EdgeFunction
{
    i64 a = 0;
    i64 b = 0;
    i64 c = 0;
};

struct PreparedTriangle
{
    EdgeFunction e0;
    EdgeFunction e1;
    EdgeFunction e2;
    math::Color c0;
    math::Color c1;
    math::Color c2;
    // Coverage bias per edge: 0 for top-left, -1 otherwise. The unbiased edge
    // value is biasedValue - bias, which keeps interpolation exact.
    i64 bias0 = 0;
    i64 bias1 = 0;
    i64 bias2 = 0;
    i64 area = 0;
    i32 minX = 0;
    i32 minY = 0;
    i32 maxX = -1;
    i32 maxY = -1;
    i32 tileMinX = 0;
    i32 tileMinY = 0;
    i32 tileMaxX = -1;
    i32 tileMaxY = -1;
    bool drawable = false;
};

struct TileContext
{
    Framebuffer* target = nullptr;
    const PreparedTriangle* triangles = nullptr;
    const u32* offsets = nullptr;
    const u32* indices = nullptr;
    u64* tilePixels = nullptr;
    u32 tilesX = 0;
    i32 tileSize = kDefaultTileSize;
    i32 width = 0;
    i32 height = 0;
};

i64 FloorDiv(i64 value, i64 divisor) noexcept
{
    const i64 quotient = value / divisor;
    const i64 remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

i64 CeilDiv(i64 value, i64 divisor) noexcept
{
    const i64 quotient = value / divisor;
    const i64 remainder = value % divisor;
    return remainder > 0 ? quotient + 1 : quotient;
}

bool SnapCoordinate(f32 value, i32& result) noexcept
{
    if (!std::isfinite(value))
    {
        return false;
    }
    const f64 scaled = static_cast<f64>(value) * static_cast<f64>(kSubpixelScale);
    if (scaled < -static_cast<f64>(kMaxCoordinateFixed) ||
        scaled > static_cast<f64>(kMaxCoordinateFixed))
    {
        return false;
    }
    result = static_cast<i32>(std::lround(scaled));
    return true;
}

i64 SetEdge(EdgeFunction& edge, i32 x0, i32 y0, i32 x1, i32 y1) noexcept
{
    const i64 dx = static_cast<i64>(x1) - static_cast<i64>(x0);
    const i64 dy = static_cast<i64>(y1) - static_cast<i64>(y0);
    edge.a = -dy;
    edge.b = dx;
    edge.c = dy * static_cast<i64>(x0) - dx * static_cast<i64>(y0);

    // Screen space is y down and the rasterizer normalizes to positive
    // (clockwise) area. A directed edge is top-left when it is horizontal and
    // goes right, or when it goes up. A -1 bias on every other edge turns
    // "E > 0, or E == 0 and top-left" into "biased E >= 0".
    const bool topLeft = (dy == 0 && dx > 0) || dy < 0;
    if (!topLeft)
    {
        edge.c -= 1;
        return -1;
    }
    return 0;
}

bool PrepareTriangle(const RasterTriangle& input, i32 width, i32 height, i32 tileSize,
                     PreparedTriangle& out) noexcept
{
    out.drawable = false;
    i32 x[3] = {0, 0, 0};
    i32 y[3] = {0, 0, 0};
    if (!SnapCoordinate(input.v0.position.x, x[0]) || !SnapCoordinate(input.v0.position.y, y[0]) ||
        !SnapCoordinate(input.v1.position.x, x[1]) || !SnapCoordinate(input.v1.position.y, y[1]) ||
        !SnapCoordinate(input.v2.position.x, x[2]) || !SnapCoordinate(input.v2.position.y, y[2]))
    {
        return false;
    }

    i64 area = (static_cast<i64>(x[1]) - x[0]) * (static_cast<i64>(y[2]) - y[0]) -
               (static_cast<i64>(y[1]) - y[0]) * (static_cast<i64>(x[2]) - x[0]);
    if (area == 0)
    {
        return false;
    }

    math::Color c0 = input.v0.color;
    math::Color c1 = input.v1.color;
    math::Color c2 = input.v2.color;
    if (area < 0)
    {
        std::swap(x[1], x[2]);
        std::swap(y[1], y[2]);
        std::swap(c1, c2);
        area = -area;
    }

    out.e0 = EdgeFunction{};
    out.e1 = EdgeFunction{};
    out.e2 = EdgeFunction{};
    out.bias0 = SetEdge(out.e0, x[0], y[0], x[1], y[1]);
    out.bias1 = SetEdge(out.e1, x[1], y[1], x[2], y[2]);
    out.bias2 = SetEdge(out.e2, x[2], y[2], x[0], y[0]);
    out.c0 = c0;
    out.c1 = c1;
    out.c2 = c2;
    out.area = area;

    const i64 minFixedX =
        std::min({static_cast<i64>(x[0]), static_cast<i64>(x[1]), static_cast<i64>(x[2])});
    const i64 maxFixedX =
        std::max({static_cast<i64>(x[0]), static_cast<i64>(x[1]), static_cast<i64>(x[2])});
    const i64 minFixedY =
        std::min({static_cast<i64>(y[0]), static_cast<i64>(y[1]), static_cast<i64>(y[2])});
    const i64 maxFixedY =
        std::max({static_cast<i64>(y[0]), static_cast<i64>(y[1]), static_cast<i64>(y[2])});

    // Pixel centers are at (pixel + 0.5), so the exact sample span is derived in
    // fixed point instead of rounding outward in floating point.
    const i64 pixelMinX = CeilDiv(minFixedX - kSampleOffset, kSubpixelScale);
    const i64 pixelMaxX = FloorDiv(maxFixedX - kSampleOffset, kSubpixelScale);
    const i64 pixelMinY = CeilDiv(minFixedY - kSampleOffset, kSubpixelScale);
    const i64 pixelMaxY = FloorDiv(maxFixedY - kSampleOffset, kSubpixelScale);
    if (pixelMinX > pixelMaxX || pixelMinY > pixelMaxY)
    {
        return false;
    }

    const i64 clippedMinX = std::max<i64>(pixelMinX, 0);
    const i64 clippedMinY = std::max<i64>(pixelMinY, 0);
    const i64 clippedMaxX = std::min<i64>(pixelMaxX, static_cast<i64>(width) - 1);
    const i64 clippedMaxY = std::min<i64>(pixelMaxY, static_cast<i64>(height) - 1);
    if (clippedMinX > clippedMaxX || clippedMinY > clippedMaxY)
    {
        return false;
    }

    out.minX = static_cast<i32>(clippedMinX);
    out.minY = static_cast<i32>(clippedMinY);
    out.maxX = static_cast<i32>(clippedMaxX);
    out.maxY = static_cast<i32>(clippedMaxY);
    out.tileMinX = out.minX / tileSize;
    out.tileMinY = out.minY / tileSize;
    out.tileMaxX = out.maxX / tileSize;
    out.tileMaxY = out.maxY / tileSize;
    out.drawable = true;
    return true;
}

template <PixelFormat kFormat>
void StorePixel(u8* row, i32 x, f32 r, f32 g, f32 b, f32 a) noexcept
{
    if constexpr (kFormat == PixelFormat::RGBA8Unorm)
    {
        u8* pixel = row + static_cast<usize>(x) * 4;
        pixel[0] = QuantizeUnorm8(r);
        pixel[1] = QuantizeUnorm8(g);
        pixel[2] = QuantizeUnorm8(b);
        pixel[3] = QuantizeUnorm8(a);
    }
    else
    {
        u8* pixel = row + static_cast<usize>(x) * 16;
        std::memcpy(pixel + 0, &r, sizeof(r));
        std::memcpy(pixel + 4, &g, sizeof(g));
        std::memcpy(pixel + 8, &b, sizeof(b));
        std::memcpy(pixel + 12, &a, sizeof(a));
    }
}

template <PixelFormat kFormat>
u64 RasterizeBox(Framebuffer& target, const PreparedTriangle& triangle, i32 minX, i32 minY,
                 i32 maxX, i32 maxY) noexcept
{
    const i64 sampleX = static_cast<i64>(minX) * kSubpixelScale + kSampleOffset;
    const i64 sampleY = static_cast<i64>(minY) * kSubpixelScale + kSampleOffset;
    i64 e0 = triangle.e0.a * sampleX + triangle.e0.b * sampleY + triangle.e0.c;
    i64 e1 = triangle.e1.a * sampleX + triangle.e1.b * sampleY + triangle.e1.c;
    i64 e2 = triangle.e2.a * sampleX + triangle.e2.b * sampleY + triangle.e2.c;

    const i64 stepX0 = triangle.e0.a * kSubpixelScale;
    const i64 stepX1 = triangle.e1.a * kSubpixelScale;
    const i64 stepX2 = triangle.e2.a * kSubpixelScale;
    const i64 stepY0 = triangle.e0.b * kSubpixelScale;
    const i64 stepY1 = triangle.e1.b * kSubpixelScale;
    const i64 stepY2 = triangle.e2.b * kSubpixelScale;

    // Interpolation uses the unbiased edge values. Coverage added a -1 bias to
    // non-top-left edges, so it is removed again before computing weights; the
    // bias must not shift the color of a vertex sample.
    const f64 inverseArea = 1.0 / static_cast<f64>(triangle.area);
    u64 written = 0;

    for (i32 y = minY; y <= maxY; ++y)
    {
        i64 rowE0 = e0;
        i64 rowE1 = e1;
        i64 rowE2 = e2;
        u8* row = target.Row(y);
        for (i32 x = minX; x <= maxX; ++x)
        {
            if (rowE0 >= 0 && rowE1 >= 0 && rowE2 >= 0)
            {
                const f64 unbiasedE0 = static_cast<f64>(rowE0 - triangle.bias0);
                const f64 unbiasedE1 = static_cast<f64>(rowE1 - triangle.bias1);
                const f64 unbiasedE2 = static_cast<f64>(rowE2 - triangle.bias2);
                const f32 w0 = static_cast<f32>(unbiasedE1 * inverseArea);
                const f32 w1 = static_cast<f32>(unbiasedE2 * inverseArea);
                const f32 w2 = static_cast<f32>(unbiasedE0 * inverseArea);
                const f32 r = triangle.c0.r * w0 + triangle.c1.r * w1 + triangle.c2.r * w2;
                const f32 g = triangle.c0.g * w0 + triangle.c1.g * w1 + triangle.c2.g * w2;
                const f32 b = triangle.c0.b * w0 + triangle.c1.b * w1 + triangle.c2.b * w2;
                const f32 a = triangle.c0.a * w0 + triangle.c1.a * w1 + triangle.c2.a * w2;
                StorePixel<kFormat>(row, x, r, g, b, a);
                ++written;
            }
            rowE0 += stepX0;
            rowE1 += stepX1;
            rowE2 += stepX2;
        }
        e0 += stepY0;
        e1 += stepY1;
        e2 += stepY2;
    }
    return written;
}

u64 RasterizeBoxDispatch(Framebuffer& target, const PreparedTriangle& triangle, i32 minX, i32 minY,
                         i32 maxX, i32 maxY) noexcept
{
    if (target.Format() == PixelFormat::RGBA8Unorm)
    {
        return RasterizeBox<PixelFormat::RGBA8Unorm>(target, triangle, minX, minY, maxX, maxY);
    }
    return RasterizeBox<PixelFormat::RGBA32F>(target, triangle, minX, minY, maxX, maxY);
}

void RasterizeTile(const TileContext& context, u32 tileIndex) noexcept
{
    const i32 tileX = static_cast<i32>(tileIndex % context.tilesX);
    const i32 tileY = static_cast<i32>(tileIndex / context.tilesX);
    const i32 tileMinX = tileX * context.tileSize;
    const i32 tileMinY = tileY * context.tileSize;
    const i32 tileMaxX = std::min(tileMinX + context.tileSize - 1, context.width - 1);
    const i32 tileMaxY = std::min(tileMinY + context.tileSize - 1, context.height - 1);

    u64 written = 0;
    for (u32 slot = context.offsets[tileIndex]; slot < context.offsets[tileIndex + 1]; ++slot)
    {
        const PreparedTriangle& triangle = context.triangles[context.indices[slot]];
        const i32 minX = std::max(triangle.minX, tileMinX);
        const i32 minY = std::max(triangle.minY, tileMinY);
        const i32 maxX = std::min(triangle.maxX, tileMaxX);
        const i32 maxY = std::min(triangle.maxY, tileMaxY);
        if (minX > maxX || minY > maxY)
        {
            continue;
        }
        written += RasterizeBoxDispatch(*context.target, triangle, minX, minY, maxX, maxY);
    }
    context.tilePixels[tileIndex] = written;
}

void RasterizeTileThunk(void* userData, u32 tileIndex) noexcept
{
    RasterizeTile(*static_cast<TileContext*>(userData), tileIndex);
}

bool ClipTest(f32 p, f32 q, f32& t0, f32& t1) noexcept
{
    if (p == 0.0f)
    {
        return q >= 0.0f;
    }
    const f32 ratio = q / p;
    if (p < 0.0f)
    {
        if (ratio > t1)
        {
            return false;
        }
        if (ratio > t0)
        {
            t0 = ratio;
        }
        return true;
    }
    if (ratio < t0)
    {
        return false;
    }
    if (ratio < t1)
    {
        t1 = ratio;
    }
    return true;
}

// Liang-Barsky clip against [0, width] x [0, height].
bool ClipSegment(f32& x0, f32& y0, f32& x1, f32& y1, f32 width, f32 height) noexcept
{
    const f32 originalX0 = x0;
    const f32 originalY0 = y0;
    const f32 dx = x1 - x0;
    const f32 dy = y1 - y0;
    f32 t0 = 0.0f;
    f32 t1 = 1.0f;
    if (!ClipTest(-dx, x0, t0, t1) || !ClipTest(dx, width - x0, t0, t1) ||
        !ClipTest(-dy, y0, t0, t1) || !ClipTest(dy, height - y0, t0, t1))
    {
        return false;
    }
    x0 = originalX0 + t0 * dx;
    y0 = originalY0 + t0 * dy;
    x1 = originalX0 + t1 * dx;
    y1 = originalY0 + t1 * dy;
    return true;
}

// Screen-space lines larger than this are rejected before clipping, so the
// float differences and clipping ratios cannot overflow to infinity.
constexpr f32 kMaxLineCoordinate = 1.0e7f;

i32 ClampToRange(i32 value, i32 low, i32 high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

void DrawLine(Framebuffer& target, const math::Vec2& start, const math::Vec2& end,
              const math::Color& color) noexcept
{
    if (!target.IsValid())
    {
        return;
    }
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) ||
        !std::isfinite(end.y))
    {
        return;
    }
    if (start.x < -kMaxLineCoordinate || start.x > kMaxLineCoordinate ||
        start.y < -kMaxLineCoordinate || start.y > kMaxLineCoordinate ||
        end.x < -kMaxLineCoordinate || end.x > kMaxLineCoordinate || end.y < -kMaxLineCoordinate ||
        end.y > kMaxLineCoordinate)
    {
        return;
    }

    const f32 width = static_cast<f32>(target.Width());
    const f32 height = static_cast<f32>(target.Height());
    f32 x0 = start.x;
    f32 y0 = start.y;
    f32 x1 = end.x;
    f32 y1 = end.y;
    if (!ClipSegment(x0, y0, x1, y1, width, height))
    {
        return;
    }

    // The right and bottom bounds are exclusive: the pixel domain is
    // [0, width) x [0, height). A segment that only touches those planes has no
    // pixel centers on it.
    if ((x0 >= width && x1 >= width) || (y0 >= height && y1 >= height))
    {
        return;
    }

    i32 pixelX0 = ClampToRange(static_cast<i32>(std::floor(x0)), 0, target.Width() - 1);
    i32 pixelY0 = ClampToRange(static_cast<i32>(std::floor(y0)), 0, target.Height() - 1);
    i32 pixelX1 = ClampToRange(static_cast<i32>(std::floor(x1)), 0, target.Width() - 1);
    i32 pixelY1 = ClampToRange(static_cast<i32>(std::floor(y1)), 0, target.Height() - 1);

    const i32 dx = pixelX1 > pixelX0 ? pixelX1 - pixelX0 : pixelX0 - pixelX1;
    const i32 dy = pixelY1 > pixelY0 ? pixelY1 - pixelY0 : pixelY0 - pixelY1;
    const i32 stepX = pixelX0 < pixelX1 ? 1 : -1;
    const i32 stepY = pixelY0 < pixelY1 ? 1 : -1;
    i32 error = dx - dy;

    i32 x = pixelX0;
    i32 y = pixelY0;
    for (;;)
    {
        target.SetPixel(x, y, color);
        if (x == pixelX1 && y == pixelY1)
        {
            break;
        }
        const i32 twiceError = 2 * error;
        if (twiceError >= -dy)
        {
            error -= dy;
            x += stepX;
        }
        if (twiceError <= dx)
        {
            error += dx;
            y += stepY;
        }
    }
}

void FillTriangle(Framebuffer& target, const RasterTriangle& triangle) noexcept
{
    if (!target.IsValid())
    {
        return;
    }
    PreparedTriangle prepared;
    if (!PrepareTriangle(triangle, target.Width(), target.Height(), kDefaultTileSize, prepared))
    {
        return;
    }
    RasterizeBoxDispatch(target, prepared, prepared.minX, prepared.minY, prepared.maxX,
                         prepared.maxY);
}

core::Status<core::Error>
RasterizeTriangles(Framebuffer& target, memory::Span<const RasterTriangle> triangles,
                   jobs::JobSystem* jobSystem, memory::Allocator& allocator,
                   const RasterizerDesc& desc, RasterStats* stats) noexcept
{
    if (stats != nullptr)
    {
        *stats = RasterStats{};
    }
    if (!target.IsValid())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "framebuffer is invalid"});
    }
    if (triangles.Data() == nullptr && triangles.Size() != 0)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "triangle span is null"});
    }
    if (triangles.Size() == 0)
    {
        return core::Status<core::Error>::Ok();
    }

    i32 tileSize = desc.tileSize;
    if (tileSize < kMinTileSize)
    {
        tileSize = kMinTileSize;
    }
    if (tileSize > kMaxTileSize)
    {
        tileSize = kMaxTileSize;
    }

    const i32 width = target.Width();
    const i32 height = target.Height();
    // Tile counts and bin indices are stored in 32-bit integers.
    constexpr u64 kMaxBinCount = 0xFFFFFFFFu;
    if (static_cast<u64>(triangles.Size()) > kMaxBinCount)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "triangle count is too large"});
    }
    const i64 tilesX64 = (static_cast<i64>(width) + tileSize - 1) / tileSize;
    const i64 tilesY64 = (static_cast<i64>(height) + tileSize - 1) / tileSize;
    const i64 tileCount64 = tilesX64 * tilesY64;
    if (tileCount64 <= 0 || static_cast<u64>(tileCount64) > kMaxBinCount)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "tile grid is too large"});
    }
    const u32 tilesX = static_cast<u32>(tilesX64);
    const u32 tileCount = static_cast<u32>(tileCount64);

    memory::DynamicArray<PreparedTriangle> prepared(allocator);
    if (!prepared.TryResize(triangles.Size()))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to prepare triangles"});
    }
    memory::DynamicArray<u32> counts(allocator);
    if (!counts.TryResize(tileCount))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate tile counters"});
    }

    u64 drawn = 0;
    u64 skipped = 0;
    u64 totalEntries = 0;
    for (usize index = 0; index < triangles.Size(); ++index)
    {
        PreparedTriangle& triangle = prepared[index];
        if (!PrepareTriangle(triangles[index], width, height, tileSize, triangle))
        {
            ++skipped;
            continue;
        }
        ++drawn;
        const u64 spanX = static_cast<u64>(triangle.tileMaxX - triangle.tileMinX) + 1;
        const u64 spanY = static_cast<u64>(triangle.tileMaxY - triangle.tileMinY) + 1;
        const u64 entries = spanX * spanY;
        if (entries > kMaxBinCount || totalEntries > kMaxBinCount - entries)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Overflow, "triangle-tile bin is too large"});
        }
        totalEntries += entries;
        for (i32 tileY = triangle.tileMinY; tileY <= triangle.tileMaxY; ++tileY)
        {
            for (i32 tileX = triangle.tileMinX; tileX <= triangle.tileMaxX; ++tileX)
            {
                const u32 tile = static_cast<u32>(tileY) * tilesX + static_cast<u32>(tileX);
                ++counts[tile];
            }
        }
    }

    if (totalEntries > kMaxBinCount)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "triangle-tile bin is too large"});
    }

    memory::DynamicArray<u32> offsets(allocator);
    if (!offsets.TryResize(static_cast<usize>(tileCount) + 1))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate tile offsets"});
    }
    u64 running = 0;
    for (u32 tile = 0; tile < tileCount; ++tile)
    {
        offsets[tile] = static_cast<u32>(running);
        running += counts[tile];
        if (running > kMaxBinCount)
        {
            return core::Status<core::Error>::Err(
                core::Error{core::ErrorCode::Overflow, "tile offsets overflow"});
        }
    }
    offsets[tileCount] = static_cast<u32>(running);

    memory::DynamicArray<u32> indices(allocator);
    if (!indices.TryResize(static_cast<usize>(running)))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate tile bins"});
    }
    memory::DynamicArray<u32> cursor(allocator);
    if (!cursor.TryResize(tileCount))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate tile cursors"});
    }
    for (u32 tile = 0; tile < tileCount; ++tile)
    {
        cursor[tile] = offsets[tile];
    }
    for (usize index = 0; index < triangles.Size(); ++index)
    {
        const PreparedTriangle& triangle = prepared[index];
        if (!triangle.drawable)
        {
            continue;
        }
        for (i32 tileY = triangle.tileMinY; tileY <= triangle.tileMaxY; ++tileY)
        {
            for (i32 tileX = triangle.tileMinX; tileX <= triangle.tileMaxX; ++tileX)
            {
                const u32 tile = static_cast<u32>(tileY) * tilesX + static_cast<u32>(tileX);
                indices[cursor[tile]++] = static_cast<u32>(index);
            }
        }
    }

    memory::DynamicArray<u64> tilePixels(allocator);
    if (!tilePixels.TryResize(tileCount))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate tile counters"});
    }

    TileContext context;
    context.target = &target;
    context.triangles = prepared.Data();
    context.offsets = offsets.Data();
    context.indices = indices.Data();
    context.tilePixels = tilePixels.Data();
    context.tilesX = tilesX;
    context.tileSize = tileSize;
    context.width = width;
    context.height = height;

    if (jobSystem != nullptr && desc.useJobs && tileCount > 1)
    {
        jobSystem->ParallelFor(tileCount, 1, &RasterizeTileThunk, &context);
    }
    else
    {
        for (u32 tile = 0; tile < tileCount; ++tile)
        {
            RasterizeTile(context, tile);
        }
    }

    u64 pixels = 0;
    for (u32 tile = 0; tile < tileCount; ++tile)
    {
        pixels += tilePixels[tile];
    }

    if (stats != nullptr)
    {
        stats->triangles = static_cast<u64>(triangles.Size());
        stats->trianglesDrawn = drawn;
        stats->trianglesSkipped = skipped;
        stats->tiles = static_cast<u64>(tileCount);
        stats->pixels = pixels;
    }
    return core::Status<core::Error>::Ok();
}

}  // namespace cyber::software
