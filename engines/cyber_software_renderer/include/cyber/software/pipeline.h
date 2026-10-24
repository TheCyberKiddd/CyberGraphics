#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/math/color.h>
#include <cyber/math/mat4.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>

namespace cyber::jobs
{
class JobSystem;
}

namespace cyber::software
{
class DepthBuffer;
class Framebuffer;

using cyber::core::f32;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::u64;

// Object-space vertex for the Phase 8 pipeline: a position and a linear color.
// Phase 9 replaces the color with arbitrary shader varyings.
struct Vertex
{
    math::Vec3 position;
    math::Color color;
};

// Destination rectangle in the color target. NDC (-1..1, +Y up) maps onto it
// with a y flip, so a right-handed scene appears upright on a top-left-origin
// screen. The rectangle may be a sub-rectangle of the target; coverage is
// clipped to the target.
struct Viewport
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
};

enum class CullMode : core::u8
{
    // Draw both windings. The rasterizer normalizes coverage.
    None = 0,
    // Reject triangles that are not front-facing under `FrontFace`.
    Back,
    // Reject triangles that are front-facing under `FrontFace`.
    Front,
};

// Which screen-space winding is front-facing. Positive signed screen-space area
// is clockwise on screen (ADR 0009) and negative is counter-clockwise. The
// default is counter-clockwise, which is the front face of right-handed object
// geometry whose triangles wind counter-clockwise when seen from outside. This
// matches D3D12's `FrontCounterClockwise = TRUE` for a right-handed scene.
enum class FrontFace : core::u8
{
    Clockwise = 0,
    CounterClockwise,
};

struct PipelineDesc
{
    // Object-to-world transform, applied before the view and projection.
    math::Mat4 model = math::Mat4::Identity();
    // World-to-view transform, usually `CameraView`.
    math::Mat4 view = math::Mat4::Identity();
    // View-to-clip transform, usually `CameraProjection`.
    math::Mat4 projection = math::Mat4::Identity();
    Viewport viewport{};
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    // Screen tile edge in pixels for the rasterizer. Clamped to [8, 256]; it
    // changes scheduling, never the resulting image.
    i32 tileSize = 64;
    // False forces the single-threaded tile loop even when a job system is
    // supplied.
    bool useJobs = true;
};

// Per-draw diagnostics. Every counter is deterministic.
struct PipelineStats
{
    // Input triangles (vertex count / 3).
    u64 triangles = 0;
    // Input triangles that crossed the near plane and were clipped.
    u64 trianglesClipped = 0;
    // Rejected by winding or because they became degenerate.
    u64 trianglesCulled = 0;
    // Rejected because a screen coordinate left the +/-16384-pixel guard band.
    u64 trianglesOffscreen = 0;
    // Rejected as non-finite or with a non-positive clip w after clipping.
    u64 trianglesInvalid = 0;
    // Triangles handed to the rasterizer after clipping, culling, and the
    // viewport transform. A clipped input triangle can produce two.
    u64 trianglesSubmitted = 0;
    // Samples written to the color target.
    u64 pixels = 0;
    // Covered samples that failed the reverse-Z depth test.
    u64 pixelsRejected = 0;
};

// A vertex in homogeneous clip space: `-w <= x <= w`, `-w <= y <= w`,
// `0 <= z <= w` (D3D convention, reverse-Z near plane at z = w).
struct ClipVertex
{
    math::Vec4 position;
    math::Color color;
};

// Maximum vertices a triangle can produce by clipping against one plane:
// three unchanged, or two original vertices plus two intersections.
inline constexpr u32 kMaxClipPolygonVertices = 4;

// Output of `ClipTriangleNearPlane`. `count` is 0 (nothing visible), 3
// (triangle, vertices unchanged), or 4 (quad). Vertices keep input order, so a
// convex polygon winds the same way as the input triangle.
struct ClipPolygon
{
    ClipVertex vertices[kMaxClipPolygonVertices]{};
    u32 count = 0;
};

// Clips a triangle against the near plane `z = w` in homogeneous clip space.
//
// Vertices with `z <= w` are inside. The intersection parameter is
// `f(a) / (f(a) - f(b))` with `f(p) = p.z - p.w`, interpolating the whole clip
// position and the color. A vertex exactly on the plane counts as inside, and
// an intersection that coincides with an endpoint is not emitted twice. The
// function is allocation-free and performs no perspective divide, so vertices
// behind the camera never wrap around the screen.
[[nodiscard]] ClipPolygon ClipTriangleNearPlane(const ClipVertex& v0, const ClipVertex& v1,
                                                const ClipVertex& v2) noexcept;

// Runs the Phase 8 vertex pipeline over a triangle list.
//
// For every three vertices: transform by `projection * view * model`, clip the
// near plane in homogeneous space, divide by w, map into `desc.viewport` with a
// y flip, cull by `desc.cullMode`/`desc.frontFace`, and reject triangles whose
// screen coordinates leave the +/-16384-pixel guard band. Surviving triangles
// are rasterized into `color` and `depth` with perspective-correct color and a
// reverse-Z greater-or-equal depth test.
//
// `depth` must be valid and have the color target's dimensions. The caller
// clears both targets before the draw. The allocator supplies temporary clip
// and triangle arrays and is not accessed by workers.
//
// Returns InvalidArgument for an invalid target, a dimension mismatch, an
// invalid viewport, or a vertex count that is not a multiple of three; Overflow
// when a temporary count exceeds 32 bits; Internal when an allocation fails.
// `stats` is optional and is zeroed on entry.
[[nodiscard]] core::Status<core::Error>
DrawTriangles(const PipelineDesc& desc, memory::Span<const Vertex> vertices, Framebuffer& color,
              DepthBuffer& depth, jobs::JobSystem* jobSystem, memory::Allocator& allocator,
              PipelineStats* stats = nullptr) noexcept;

}  // namespace cyber::software
