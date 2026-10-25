#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/math/color.h>
#include <cyber/math/vec2.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/types.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/shading.h>

namespace cyber::jobs
{
class JobSystem;
}

namespace cyber::software
{
class DepthBuffer;
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

// A screen-space vertex for the depth-tested path. Coverage and color use the
// same conventions as RasterVertex; the extra fields drive visibility and
// perspective correction:
//
// - `depth` is the reverse-Z NDC depth: 1 at the near plane, 0 at the far
//   plane. It is interpolated linearly because NDC depth is affine in screen
//   space. A value below 0 lies beyond the far plane and loses against a
//   buffer cleared to 0.
// - `inverseW` is 1 / clip.w. Attributes are interpolated as
//   (sum b_i * attribute_i * inverseW_i) / (sum b_i * inverseW_i). The value
//   must be finite and greater than zero; a non-positive value skips the
//   triangle.
struct DepthRasterVertex
{
    math::Vec2 position;
    math::Color color;
    f32 depth = 0.0f;
    f32 inverseW = 1.0f;
};

struct DepthRasterTriangle
{
    DepthRasterVertex v0;
    DepthRasterVertex v1;
    DepthRasterVertex v2;
};

// A screen-space vertex for the shaded path. Coverage, depth, and perspective
// correction follow DepthRasterVertex; `varyings` replaces the single color and
// is interpolated perspective-correctly before the pixel shader runs.
struct ShadedRasterVertex
{
    math::Vec2 position;
    Varyings varyings;
    f32 depth = 0.0f;
    f32 inverseW = 1.0f;
};

struct ShadedRasterTriangle
{
    ShadedRasterVertex v0;
    ShadedRasterVertex v1;
    ShadedRasterVertex v2;
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
    // Covered samples that failed the depth test. Always 0 for the
    // non-depth-tested entry points.
    u64 pixelsRejected = 0;
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

// Rasterizes depth-tested triangles with perspective-correct color.
//
// Coverage is identical to RasterizeTriangles: the same fixed-point snapping,
// top-left rule, tile binning, and deterministic output. A covered sample then
// compares its interpolated depth with `depth` using greater-or-equal, writes
// the depth on success, and interpolates color through 1 / w. A sample that
// fails the test leaves both targets untouched and counts in
// RasterStats::pixelsRejected.
//
// `depth` must be valid and have the color target's dimensions. Both targets
// are written; the caller clears them before the draw. The allocator is used
// only for temporary arrays and is not accessed by workers.
//
// Returns InvalidArgument for an invalid target, a dimension mismatch, or a
// null triangle pointer with a non-zero count, Overflow when the triangle or
// bin count exceeds 32 bits, and Internal when a temporary allocation fails.
// `stats` is optional and is zeroed on entry.
[[nodiscard]] core::Status<core::Error> RasterizeDepthTestedTriangles(
    Framebuffer& target, DepthBuffer& depth, memory::Span<const DepthRasterTriangle> triangles,
    jobs::JobSystem* jobSystem, memory::Allocator& allocator, const RasterizerDesc& desc = {},
    RasterStats* stats = nullptr) noexcept;

// Rasterizes depth-tested triangles whose color is produced by a pixel shader.
//
// Coverage, snapping, the top-left rule, and tile binning are identical to
// RasterizeDepthTestedTriangles. A covered sample interpolates every varying
// perspective-correctly, runs the depth test, and on success calls
// `pixelShader(varyings, userData)` and writes the returned linear color.
// A failing depth test leaves the targets untouched and increments
// RasterStats::pixelsRejected.
//
// `pixelShader` must be non-null; a null shader returns InvalidArgument. The
// depth buffer must be valid and match the color target. The allocator supplies
// temporary arrays only and is not accessed by workers. Both the allocator and
// the pixel shader must be safe to call concurrently: the shader runs on the
// thread that owns the tile.
[[nodiscard]] core::Status<core::Error>
RasterizeShadedTriangles(Framebuffer& target, DepthBuffer& depth,
                         memory::Span<const ShadedRasterTriangle> triangles,
                         PixelShaderFn pixelShader, const void* userData,
                         jobs::JobSystem* jobSystem, memory::Allocator& allocator,
                         const RasterizerDesc& desc = {}, RasterStats* stats = nullptr) noexcept;

}  // namespace cyber::software
