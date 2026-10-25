#pragma once

#include <cyber/core/types.h>
#include <cyber/math/color.h>
#include <cyber/math/vec2.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>

namespace cyber::software
{

using cyber::core::f32;

// Object-space vertex handed to a vertex shader. Positions, normals, and UVs
// are in the mesh's local space; the shader applies its own transforms.
struct VertexInput
{
    math::Vec3 position;
    // Unit normal in object space. The built-in transform shader maps it to
    // world space through the normal matrix.
    math::Vec3 normal{0.0f, 0.0f, 1.0f};
    math::Vec2 uv;
    // Linear vertex color, multiplied into the material albedo by the standard
    // lighting shader when a scene uses it.
    math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Attributes produced by the vertex stage, interpolated perspective-correctly
// across a triangle, and consumed by the pixel stage.
struct Varyings
{
    math::Vec2 uv;
    math::Vec3 normal;
    math::Vec3 worldPosition;
    math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Output of a vertex shader: a homogeneous clip position and its varyings.
// The pipeline clips the position against the near plane, divides by w, maps it
// into the viewport, and interpolates the varyings.
struct VertexStageOutput
{
    math::Vec4 clipPosition;
    Varyings varyings;
};

// Vertex stage. Receives one object-space vertex and the caller's user data,
// and fills `output`. Must be noexcept and must not allocate.
using VertexShaderFn = void (*)(const VertexInput& input, const void* userData,
                                VertexStageOutput& output) noexcept;

// Pixel stage. Receives the interpolated varyings and the caller's user data,
// and returns a linear RGBA color. Must be noexcept and must not allocate.
using PixelShaderFn = math::Color (*)(const Varyings& varyings, const void* userData) noexcept;

// Component-wise linear interpolation of every varying field. The clipper uses
// it with the same factor it applies to the clip position, so a clipped vertex
// keeps correct UVs and normals.
[[nodiscard]] inline Varyings LerpVaryings(const Varyings& from, const Varyings& to,
                                           f32 factor) noexcept
{
    Varyings result;
    result.uv = math::Lerp(from.uv, to.uv, factor);
    result.normal = math::Lerp(from.normal, to.normal, factor);
    result.worldPosition = math::Lerp(from.worldPosition, to.worldPosition, factor);
    result.color = math::Color{from.color.r + (to.color.r - from.color.r) * factor,
                               from.color.g + (to.color.g - from.color.g) * factor,
                               from.color.b + (to.color.b - from.color.b) * factor,
                               from.color.a + (to.color.a - from.color.a) * factor};
    return result;
}

}  // namespace cyber::software
