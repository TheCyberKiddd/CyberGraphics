#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/math/mat4.h>
#include <cyber/math/vec3.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/software/framebuffer.h>
#include <cyber/software/pipeline.h>
#include <cyber/software/shading.h>

namespace cyber::jobs
{
class JobSystem;
}

namespace cyber::software
{

class DepthBuffer;

// User data for `TransformVertexShader`: the matrices a scene applies before
// the pipeline clips and rasterizes.
struct TransformShaderData
{
    // Model -> view -> clip. Usually `projection * view * model`.
    math::Mat4 modelViewProjection = math::Mat4::Identity();
    // Model -> world, used for the interpolated world position.
    math::Mat4 modelToWorld = math::Mat4::Identity();
    // Inverse-transpose of the upper-left 3x3, used for normals.
    math::Mat4 normalToWorld = math::Mat4::Identity();
    // World-space eye position, copied into the varyings for lighting.
    math::Vec3 eyePosition;
};

// Built-in vertex shader. Transforms position into clip space, world position
// through `modelToWorld`, and the normal through `normalToWorld`, and forwards
// UV and vertex color. Requires `userData` to point at a `TransformShaderData`;
// a null pointer produces an origin vertex, which the pipeline then culls.
void TransformVertexShader(const VertexInput& input, const void* userData,
                           VertexStageOutput& output) noexcept;

struct ShaderDesc
{
    PipelineDesc pipeline{};
    // Required. A null shader returns InvalidArgument.
    VertexShaderFn vertexShader = nullptr;
    // Required. A null shader returns InvalidArgument.
    PixelShaderFn pixelShader = nullptr;
    // Passed unchanged to both stages. May be null when the shaders do not need
    // it.
    const void* userData = nullptr;
};

// Runs the Phase 9 vertex pipeline over a triangle list.
//
// For every three vertices: run `desc.vertexShader`, clip the near plane in
// homogeneous space (interpolating every varying), divide by w, map into the
// viewport with a y flip, cull by `desc.pipeline.cullMode`/`frontFace`, and
// reject triangles whose screen coordinates leave the +/-16384-pixel guard
// band. Surviving triangles are rasterized into `color` and `depth` with a
// reverse-Z depth test, perspective-correct varying interpolation, and
// `desc.pixelShader` producing each pixel.
//
// `depth` must be valid and have the color target's dimensions. The caller
// clears both targets before the draw. The allocator supplies temporary clip
// and triangle arrays and is not accessed by workers; the pixel shader runs on
// the thread that owns the tile.
//
// Returns InvalidArgument for an invalid target, a dimension mismatch, an
// invalid viewport, a null shader, or a vertex count that is not a multiple of
// three; Overflow when a temporary count exceeds 32 bits; Internal when an
// allocation fails. `stats` is optional and is zeroed on entry.
[[nodiscard]] core::Status<core::Error>
DrawShadedTriangles(const ShaderDesc& desc, memory::Span<const VertexInput> vertices,
                    Framebuffer& color, DepthBuffer& depth, jobs::JobSystem* jobSystem,
                    memory::Allocator& allocator, PipelineStats* stats = nullptr) noexcept;

}  // namespace cyber::software
