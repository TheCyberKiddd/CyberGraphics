#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/math/color.h>
#include <cyber/math/vec2.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/types.h>
#include <cyber/software/framebuffer.h>

namespace cyber::jobs
{
class JobSystem;
}

namespace cyber::software
{
using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u64;
using cyber::core::usize;

// A screen-space vertex. Position is in continuous pixels with a top-left
// origin and y down, so pixel (x, y) is sampled at (x + 0.5, y + 0.5). Color is
// linear RGBA and is interpolated across the triangle.
struct RasterVertex
{
    math::Vec2 position;
    math::Color color;
};

// Three screen-space vertices. Winding is normalized internally; both
// clockwise and counter-clockwise input produce the same pixels and colors.
struct RasterTriangle
{
    RasterVertex v0;
    RasterVertex v1;
    RasterVertex v2;
};

struct RasterizerDesc
{
    // Screen tile edge in pixels. Clamped to [8, 256]. The tile size changes
    // scheduling, never the resulting image.
    i32 tileSize = 64;
    // False forces the single-threaded tile loop even when a job system is
    // supplied. Both paths write identical bytes.
    bool useJobs = true;
};

// Diagnostics for one RasterizeTriangles call. All counters are deterministic:
// pixel writes are accumulated per tile and summed on the calling thread.
struct RasterStats
{
    u64 triangles = 0;
    u64 trianglesDrawn = 0;
    // Degenerate, non-finite, outside the guard band, or fully off-screen.
    u64 trianglesSkipped = 0;
    u64 tiles = 0;
    // Number of pixel writes across all triangles. A watertight mesh that
    // covers the framebuffer has pixels == width * height.
    u64 pixels = 0;
};

// Draws one clipped, one-pixel-wide line in linear color. Coordinates are
// continuous screen positions; endpoints are mapped to the pixels that contain
// them. Both endpoints are included. Non-finite input, coordinates beyond the
// +/-1e7 guard band, and segments outside the framebuffer write nothing. Every
// covered pixel is written exactly once.
void DrawLine(Framebuffer& target, const math::Vec2& start, const math::Vec2& end,
              const math::Color& color) noexcept;

// Fills one triangle with per-vertex linear color interpolation. Degenerate,
// non-finite, and guard-band triangles write nothing. Allocation-free.
//
// Coverage uses an exact integer top-left rule: pixels whose centers lie on a
// shared edge are written by exactly one of the two adjacent triangles, and a
// shared vertex is owned by exactly one triangle of a closed fan. Color is
// interpolated linearly; perspective correction arrives in Phase 8.
void FillTriangle(Framebuffer& target, const RasterTriangle& triangle) noexcept;

// Rasterizes triangles into tiles taken from the caller's allocator.
//
// Triangles are binned into screen tiles and one job per tile writes that
// tile's disjoint pixels through jobSystem. When jobSystem is null, or
// desc.useJobs is false, the same tile loop runs sequentially. Bin lists keep
// input order, so every configuration produces byte-identical output.
//
// The allocator is used only for the temporary prepared-triangle and tile-bin
// arrays, which are released before returning. It is not accessed by workers.
//
// Returns InvalidArgument for an invalid target or a null triangle pointer with
// a non-zero count, Overflow when the triangle or bin count exceeds 32 bits, and
// Internal when a temporary allocation fails. `stats` is optional and is zeroed
// on entry.
[[nodiscard]] core::Status<core::Error>
RasterizeTriangles(Framebuffer& target, memory::Span<const RasterTriangle> triangles,
                   jobs::JobSystem* jobSystem, memory::Allocator& allocator,
                   const RasterizerDesc& desc = {}, RasterStats* stats = nullptr) noexcept;

}  // namespace cyber::software
