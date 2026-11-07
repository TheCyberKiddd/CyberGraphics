#pragma once

#include <cyber/assets/asset.h>
#include <cyber/math/aabb.h>
#include <cyber/math/vec2.h>
#include <cyber/math/vec3.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/string.h>

#include <cstddef>
#include <utility>

namespace cyber::assets
{

using cyber::core::f32;
using cyber::core::u16;
using cyber::core::u32;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::String;

/// Four floats without SIMD alignment padding, so a vertex is a stable,
/// directly serializable byte layout.
struct PackedFloat4
{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr PackedFloat4() noexcept = default;

    constexpr PackedFloat4(f32 xValue, f32 yValue, f32 zValue, f32 wValue) noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
};

/// Canonical CPU vertex produced by importers and written by the cooker. The
/// layout is part of the cooked format: 96 bytes, no padding.
struct MeshVertex
{
    cyber::math::Vec3 position{};
    cyber::math::Vec3 normal{0.0f, 0.0f, 1.0f};
    /// Tangent xyz plus bitangent handedness in w (+1 or -1).
    PackedFloat4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    cyber::math::Vec2 texCoord0{};
    cyber::math::Vec2 texCoord1{};
    PackedFloat4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /// Up to four influencing joints; unused entries are zero.
    u16 joints[4] = {0, 0, 0, 0};
    /// Normalized skin weights matching `joints`; unused entries are zero.
    f32 weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(MeshVertex) == 96, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, normal) == 12, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, tangent) == 24, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, texCoord0) == 40, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, texCoord1) == 48, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, color) == 56, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, joints) == 72, "cooked vertex layout changed");
static_assert(offsetof(MeshVertex, weights) == 80, "cooked vertex layout changed");

/// One contiguous indexed triangle range using a single material. Importers emit
/// submeshes in ascending, non-overlapping order that covers the index array.
struct SubmeshAsset
{
    u32 firstIndex = 0;
    u32 indexCount = 0;
    u32 materialIndex = 0;
    cyber::math::Aabb bounds{};
};

/// Imported mesh. Vertex data stays full precision; packing happens explicitly
/// (cooked output or `cyber_scene`). The allocator must outlive the mesh.
class MeshAsset
{
public:
    explicit MeshAsset(Allocator& allocator) noexcept
        : vertices(allocator), indices(allocator), submeshes(allocator), name(allocator)
    {
    }

    MeshAsset(const MeshAsset&) = delete;
    MeshAsset& operator=(const MeshAsset&) = delete;
    MeshAsset(MeshAsset&&) noexcept = default;
    MeshAsset& operator=(MeshAsset&&) noexcept = default;
    ~MeshAsset() = default;

    /// Recomputes mesh and submesh bounds from vertex positions.
    void RecomputeBounds() noexcept;

    /// Validates index ranges, submesh coverage, and finite vertex data without
    /// touching the mesh. Returns `InvalidArgument` or `OutOfRange` on failure.
    [[nodiscard]] ImportStatus Check() const noexcept;

    /// Runs Check(), then recomputes mesh and submesh bounds. On success the
    /// mesh is ready to cook.
    [[nodiscard]] ImportStatus Validate() noexcept;

    DynamicArray<MeshVertex> vertices;
    DynamicArray<u32> indices;
    DynamicArray<SubmeshAsset> submeshes;
    cyber::math::Aabb bounds{};
    String name;
};

/// Generates tangents from positions, normals, and the primary UV set. Existing
/// tangents are replaced. Degenerate UV triangles fall back to a deterministic
/// perpendicular of the normal.
[[nodiscard]] ImportStatus GenerateTangents(MeshAsset& mesh, Allocator& scratch) noexcept;

}  // namespace cyber::assets
