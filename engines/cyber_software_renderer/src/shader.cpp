#include <cyber/memory/dynamic_array.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/software/pipeline.h>
#include <cyber/software/raster.h>
#include <cyber/software/shader.h>

#include <cmath>
#include <utility>

namespace cyber::software
{

using cyber::core::u32;
using cyber::core::usize;
using cyber::math::Color;
using cyber::math::Mat4;
using cyber::math::Vec2;
using cyber::math::Vec3;
using cyber::math::Vec4;

namespace
{

constexpr f32 kMaxScreenCoordinate = 16384.0f;
constexpr u32 kMaxShadedClipVertices = 4;

struct ShadedClipVertex
{
    Vec4 position;
    Varyings varyings;
};

struct ShadedClipPolygon
{
    ShadedClipVertex vertices[kMaxShadedClipVertices]{};
    u32 count = 0;
};

f32 NearDistance(const ShadedClipVertex& vertex) noexcept
{
    return vertex.position.z - vertex.position.w;
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

ShadedClipPolygon ClipShadedNearPlane(const ShadedClipVertex& v0, const ShadedClipVertex& v1,
                                      const ShadedClipVertex& v2) noexcept
{
    const ShadedClipVertex input[3] = {v0, v1, v2};
    ShadedClipPolygon polygon;

    for (u32 index = 0; index < 3; ++index)
    {
        const ShadedClipVertex& current = input[index];
        const ShadedClipVertex& next = input[(index + 1) % 3];
        const f32 currentDistance = NearDistance(current);
        const f32 nextDistance = NearDistance(next);
        const bool currentInside = currentDistance <= 0.0f;
        const bool nextInside = nextDistance <= 0.0f;

        if (currentInside)
        {
            if (polygon.count >= kMaxShadedClipVertices)
            {
                return ShadedClipPolygon{};
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
        if (!(factor > 0.0f) || !(factor < 1.0f))
        {
            continue;
        }
        if (polygon.count >= kMaxShadedClipVertices)
        {
            return ShadedClipPolygon{};
        }

        ShadedClipVertex intersection;
        intersection.position = cyber::math::Lerp(current.position, next.position, factor);
        intersection.varyings = LerpVaryings(current.varyings, next.varyings, factor);
        polygon.vertices[polygon.count++] = intersection;
    }

    if (polygon.count < 3)
    {
        polygon.count = 0;
    }
    return polygon;
}

}  // namespace

void TransformVertexShader(const VertexInput& input, const void* userData,
                           VertexStageOutput& output) noexcept
{
    const auto* data = static_cast<const TransformShaderData*>(userData);
    if (data == nullptr)
    {
        output = VertexStageOutput{};
        return;
    }

    output.clipPosition = data->modelViewProjection *
                          Vec4{input.position.x, input.position.y, input.position.z, 1.0f};
    output.varyings.worldPosition = data->modelToWorld.TransformPoint(input.position);
    output.varyings.normal =
        cyber::math::Normalize(data->normalToWorld.TransformVector(input.normal));
    output.varyings.uv = input.uv;
    output.varyings.color = input.color;
}

core::Status<core::Error>
DrawShadedTriangles(const ShaderDesc& desc, memory::Span<const VertexInput> vertices,
                    Framebuffer& color, DepthBuffer& depth, jobs::JobSystem* jobSystem,
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
    if (!IsValidViewport(desc.pipeline.viewport))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument,
                        "viewport must be finite with positive width and height"});
    }
    if (desc.vertexShader == nullptr || desc.pixelShader == nullptr)
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "shader stage is null"});
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

    memory::DynamicArray<ShadedRasterTriangle> triangles(allocator);
    if (!triangles.TryReserve(inputTriangles * 2))
    {
        return core::Status<core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to allocate shaded triangles"});
    }

    u64 clipped = 0;
    u64 culled = 0;
    u64 offscreen = 0;
    u64 invalid = 0;
    u64 submitted = 0;

    for (usize index = 0; index < inputTriangles; ++index)
    {
        VertexStageOutput stage[3];
        for (u32 corner = 0; corner < 3; ++corner)
        {
            desc.vertexShader(vertices[index * 3 + corner], desc.userData, stage[corner]);
        }

        if (!IsFinite(stage[0].clipPosition) || !IsFinite(stage[1].clipPosition) ||
            !IsFinite(stage[2].clipPosition))
        {
            ++invalid;
            continue;
        }

        ShadedClipVertex input[3];
        for (u32 corner = 0; corner < 3; ++corner)
        {
            input[corner].position = stage[corner].clipPosition;
            input[corner].varyings = stage[corner].varyings;
        }

        const bool crossedNearPlane = NearDistance(input[0]) > 0.0f ||
                                      NearDistance(input[1]) > 0.0f ||
                                      NearDistance(input[2]) > 0.0f;
        const ShadedClipPolygon polygon = ClipShadedNearPlane(input[0], input[1], input[2]);
        if (polygon.count == 0)
        {
            ++culled;
            continue;
        }
        if (crossedNearPlane)
        {
            ++clipped;
        }

        for (u32 fan = 1; fan + 1 < polygon.count; ++fan)
        {
            const ShadedClipVertex* corners[3] = {&polygon.vertices[0], &polygon.vertices[fan],
                                                  &polygon.vertices[fan + 1]};

            Vec2 screen[3];
            f32 sampleDepth[3] = {0.0f, 0.0f, 0.0f};
            f32 inverseW[3] = {0.0f, 0.0f, 0.0f};
            bool usable = true;
            bool outsideGuard = false;

            for (u32 corner = 0; corner < 3; ++corner)
            {
                const f32 w = corners[corner]->position.w;
                if (!(w > 0.0f) || !std::isfinite(w))
                {
                    usable = false;
                    break;
                }
                const f32 reciprocal = 1.0f / w;
                const f32 ndcX = corners[corner]->position.x * reciprocal;
                const f32 ndcY = corners[corner]->position.y * reciprocal;
                const f32 ndcZ = corners[corner]->position.z * reciprocal;
                const f32 screenX =
                    desc.pipeline.viewport.x + (0.5f * ndcX + 0.5f) * desc.pipeline.viewport.width;
                const f32 screenY =
                    desc.pipeline.viewport.y + (0.5f - 0.5f * ndcY) * desc.pipeline.viewport.height;
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

            const f32 area = (screen[1].x - screen[0].x) * (screen[2].y - screen[0].y) -
                             (screen[1].y - screen[0].y) * (screen[2].x - screen[0].x);
            if (area == 0.0f)
            {
                ++culled;
                continue;
            }

            const bool frontFacing =
                desc.pipeline.frontFace == FrontFace::Clockwise ? area > 0.0f : area < 0.0f;
            if ((desc.pipeline.cullMode == CullMode::Back && !frontFacing) ||
                (desc.pipeline.cullMode == CullMode::Front && frontFacing))
            {
                ++culled;
                continue;
            }

            ShadedRasterTriangle output;
            ShadedRasterVertex* destinations[3] = {&output.v0, &output.v1, &output.v2};
            for (u32 corner = 0; corner < 3; ++corner)
            {
                destinations[corner]->position = screen[corner];
                destinations[corner]->varyings = corners[corner]->varyings;
                destinations[corner]->depth = sampleDepth[corner];
                destinations[corner]->inverseW = inverseW[corner];
            }
            if (!triangles.TryPushBack(output))
            {
                return core::Status<core::Error>::Err(
                    core::Error{core::ErrorCode::Internal, "failed to store a shaded triangle"});
            }
            ++submitted;
        }
    }

    RasterStats rasterStats{};
    if (!triangles.Empty())
    {
        RasterizerDesc rasterDesc{};
        rasterDesc.tileSize = desc.pipeline.tileSize;
        rasterDesc.useJobs = desc.pipeline.useJobs;
        const auto status = RasterizeShadedTriangles(
            color, depth,
            memory::Span<const ShadedRasterTriangle>(triangles.Data(), triangles.Size()),
            desc.pixelShader, desc.userData, jobSystem, allocator, rasterDesc, &rasterStats);
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
