#include <cyber/memory/dynamic_array.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/pipeline.h>
#include <cyber/software/raster.h>

#include <cmath>

namespace cyber::software
{

using cyber::core::usize;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec2;
using cyber::math::Vec3;
using cyber::math::Vec4;

namespace
{

// The Phase 7 rasterizer snaps positions to 24.8 fixed point and rejects
// coordinates beyond +/-16384 pixels. The pipeline applies the same bound so a
// rejected triangle is counted once, here, instead of disappearing inside the
// rasterizer.
constexpr f32 kMaxScreenCoordinate = 16384.0f;

// Near plane in homogeneous clip space: inside when `z <= w`.
f32 NearPlaneDistance(const ClipVertex& vertex) noexcept
{
    return vertex.position.z - vertex.position.w;
}

Color LerpColor(const Color& from, const Color& to, f32 factor) noexcept
{
    return Color{from.r + (to.r - from.r) * factor, from.g + (to.g - from.g) * factor,
                 from.b + (to.b - from.b) * factor, from.a + (to.a - from.a) * factor};
}

bool IsFinite(const Vec4& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::isfinite(value.w);
}

bool IsValidViewport(const Viewport& viewport) noexcept
{
    return std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
           std::isfinite(viewport.width) && std::isfinite(viewport.height) &&
           viewport.width > 0.0f && viewport.height > 0.0f;
}

}  // namespace

ClipPolygon ClipTriangleNearPlane(const ClipVertex& v0, const ClipVertex& v1,
                                  const ClipVertex& v2) noexcept
{
    const ClipVertex input[3] = {v0, v1, v2};
    ClipPolygon polygon;

    for (u32 index = 0; index < 3; ++index)
    {
        const ClipVertex& current = input[index];
        const ClipVertex& next = input[(index + 1) % 3];
        const f32 currentDistance = NearPlaneDistance(current);
        const f32 nextDistance = NearPlaneDistance(next);
        const bool currentInside = currentDistance <= 0.0f;
        const bool nextInside = nextDistance <= 0.0f;

        if (currentInside)
        {
            if (polygon.count >= kMaxClipPolygonVertices)
            {
                return ClipPolygon{};
            }
            polygon.vertices[polygon.count++] = current;
        }

        if (currentInside == nextInside)
        {
            continue;
        }

        const f32 denominator = currentDistance - nextDistance;
        if (denominator == 0.0f || !std::isfinite(denominator))
        {
            continue;
        }
        const f32 factor = currentDistance / denominator;
        // An intersection at an endpoint coincides with a vertex that is
        // emitted by the loop already or on the next edge.
        if (!(factor > 0.0f) || !(factor < 1.0f))
        {
            continue;
        }
        if (polygon.count >= kMaxClipPolygonVertices)
        {
            return ClipPolygon{};
        }

        ClipVertex intersection;
        intersection.position = cyber::math::Lerp(current.position, next.position, factor);
        intersection.color = LerpColor(current.color, next.color, factor);
        polygon.vertices[polygon.count++] = intersection;
    }

    if (polygon.count < 3)
    {
        polygon.count = 0;
    }
    return polygon;
}

core::Status<core::Error> DrawTriangles(const PipelineDesc& desc,
                                        memory::Span<const Vertex> vertices, Framebuffer& color,
                                        DepthBuffer& depth, jobs::JobSystem* jobSystem,
                                        memory::Allocator& allocator, PipelineStats* stats) noexcept
{
    if (stats != nullptr)
    {
        *stats = PipelineStats{};
    }
    if (!color.IsValid())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "framebuffer is invalid"});
    }
    if (!depth.IsValid())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "depth buffer is invalid"});
    }
    if (depth.Width() != color.Width() || depth.Height() != color.Height())
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument,
                        "depth buffer dimensions do not match the framebuffer"});
    }
    if (!IsValidViewport(desc.viewport))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument,
                        "viewport must be finite with positive width and height"});
    }
    if (vertices.Data() == nullptr && vertices.Size() != 0)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "vertex span is null"});
    }
    if ((vertices.Size() % 3) != 0)
    {
        return core::Status<core::Error>::Err(core::Error{
            core::ErrorCode::InvalidArgument, "vertex count must be a multiple of three"});
    }
    if (vertices.Size() == 0)
    {
        return core::Status<core::Error>::Ok();
    }

    const usize inputTriangles = vertices.Size() / 3;
    if (inputTriangles > static_cast<usize>(-1) / 2)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Overflow, "triangle count is too large"});
    }

    // One input triangle becomes at most two after the near-plane clip.
    memory::DynamicArray<DepthRasterTriangle> triangles(allocator);
    if (!triangles.TryReserve(inputTriangles * 2))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate pipeline triangles"});
    }

    const Mat4 modelViewProjection = desc.projection * desc.view * desc.model;
    u64 clipped = 0;
    u64 culled = 0;
    u64 offscreen = 0;
    u64 invalid = 0;
    u64 submitted = 0;

    for (usize index = 0; index < inputTriangles; ++index)
    {
        const Vertex& source0 = vertices[index * 3 + 0];
        const Vertex& source1 = vertices[index * 3 + 1];
        const Vertex& source2 = vertices[index * 3 + 2];

        ClipVertex input[3];
        input[0].position = modelViewProjection *
                            Vec4{source0.position.x, source0.position.y, source0.position.z, 1.0f};
        input[0].color = source0.color;
        input[1].position = modelViewProjection *
                            Vec4{source1.position.x, source1.position.y, source1.position.z, 1.0f};
        input[1].color = source1.color;
        input[2].position = modelViewProjection *
                            Vec4{source2.position.x, source2.position.y, source2.position.z, 1.0f};
        input[2].color = source2.color;

        if (!IsFinite(input[0].position) || !IsFinite(input[1].position) ||
            !IsFinite(input[2].position))
        {
            ++invalid;
            continue;
        }

        const bool crossedNearPlane = NearPlaneDistance(input[0]) > 0.0f ||
                                      NearPlaneDistance(input[1]) > 0.0f ||
                                      NearPlaneDistance(input[2]) > 0.0f;
        const ClipPolygon polygon = ClipTriangleNearPlane(input[0], input[1], input[2]);
        if (polygon.count == 0)
        {
            // Entirely behind the near plane or exactly on it.
            ++culled;
            continue;
        }
        if (crossedNearPlane)
        {
            ++clipped;
        }

        for (u32 fan = 1; fan + 1 < polygon.count; ++fan)
        {
            const ClipVertex* corners[3] = {&polygon.vertices[0], &polygon.vertices[fan],
                                            &polygon.vertices[fan + 1]};

            f32 clipW[3] = {0.0f, 0.0f, 0.0f};
            Vec2 screen[3];
            f32 sampleDepth[3] = {0.0f, 0.0f, 0.0f};
            f32 inverseW[3] = {0.0f, 0.0f, 0.0f};
            bool usable = true;
            bool outsideGuard = false;

            for (u32 corner = 0; corner < 3; ++corner)
            {
                clipW[corner] = corners[corner]->position.w;
                if (!(clipW[corner] > 0.0f) || !std::isfinite(clipW[corner]))
                {
                    usable = false;
                    break;
                }

                const f32 reciprocal = 1.0f / clipW[corner];
                const f32 ndcX = corners[corner]->position.x * reciprocal;
                const f32 ndcY = corners[corner]->position.y * reciprocal;
                const f32 ndcZ = corners[corner]->position.z * reciprocal;
                const f32 screenX = desc.viewport.x + (0.5f * ndcX + 0.5f) * desc.viewport.width;
                const f32 screenY = desc.viewport.y + (0.5f - 0.5f * ndcY) * desc.viewport.height;
                if (!std::isfinite(screenX) || !std::isfinite(screenY) || !std::isfinite(ndcZ))
                {
                    usable = false;
                    break;
                }
                if (screenX < -kMaxScreenCoordinate || screenX > kMaxScreenCoordinate ||
                    screenY < -kMaxScreenCoordinate || screenY > kMaxScreenCoordinate)
                {
                    outsideGuard = true;
                }

                screen[corner] = Vec2{screenX, screenY};
                sampleDepth[corner] = ndcZ;
                inverseW[corner] = reciprocal;
            }

            if (!usable)
            {
                ++invalid;
                continue;
            }
            if (outsideGuard)
            {
                ++offscreen;
                continue;
            }

            // Positive signed area is clockwise on screen. The viewport y flip
            // turns a right-handed front face into clockwise.
            const f32 area = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
                             (screen[1].y - screen[0].y) * (screen[2].x - screen[0].x);
            if (area == 0.0f)
            {
                ++culled;
                continue;
            }

            const bool frontFacing =
                desc.frontFace == FrontFace::Clockwise ? area > 0.0f : area < 0.0f;
            if ((desc.cullMode == CullMode::Back && !frontFacing) ||
                (desc.cullMode == CullMode::Front && frontFacing))
            {
                ++culled;
                continue;
            }

            DepthRasterVertex output[3];
            for (u32 corner = 0; corner < 3; ++corner)
            {
                output[corner].position = screen[corner];
                output[corner].color = corners[corner]->color;
                output[corner].depth = sampleDepth[corner];
                output[corner].inverseW = inverseW[corner];
            }
            if (!triangles.TryPushBack(DepthRasterTriangle{output[0], output[1], output[2]}))
            {
                return core::Status<core::Error>::Err(
                    core::Error{core::ErrorCode::Internal, "failed to store a pipeline triangle"});
            }
            ++submitted;
        }
    }

    RasterStats rasterStats{};
    if (!triangles.Empty())
    {
        RasterizerDesc rasterDesc{};
        rasterDesc.tileSize = desc.tileSize;
        rasterDesc.useJobs = desc.useJobs;
        const auto status = RasterizeDepthTestedTriangles(
            color, depth,
            memory::Span<const DepthRasterTriangle>(triangles.Data(), triangles.Size()), jobSystem,
            allocator, rasterDesc, &rasterStats);
        if (!status)
        {
            return status;
        }
    }

    if (stats != nullptr)
    {
        stats->triangles = static_cast<u64>(inputTriangles);
        stats->trianglesClipped = clipped;
        stats->trianglesCulled = culled;
        stats->trianglesOffscreen = offscreen;
        stats->trianglesInvalid = invalid;
        stats->trianglesSubmitted = submitted;
        stats->pixels = rasterStats.pixels;
        stats->pixelsRejected = rasterStats.pixelsRejected;
    }
    return core::Status<core::Error>::Ok();
}

}  // namespace cyber::software
