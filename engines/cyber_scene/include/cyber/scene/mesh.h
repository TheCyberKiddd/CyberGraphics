#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/math/aabb.h>
#include <cyber/math/mat4.h>
#include <cyber/math/vec2.h>
#include <cyber/math/vec3.h>
#include <cyber/math/vec4.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>

#include <utility>

namespace cyber::scene
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u16;
using cyber::core::u32;
using cyber::core::u64;
using cyber::memory::Span;
using cyber::memory::usize;

/// Four tightly packed float attributes without SIMD alignment padding.
struct Float4
{
    cyber::core::f32 x = 0.0f;
    cyber::core::f32 y = 0.0f;
    cyber::core::f32 z = 0.0f;
    cyber::core::f32 w = 0.0f;

    /// Constructs zeroed components.
    constexpr Float4() noexcept = default;
    /// Constructs four components.
    /// @param xValue First component.
    /// @param yValue Second component.
    /// @param zValue Third component.
    /// @param wValue Fourth component.
    constexpr Float4(cyber::core::f32 xValue, cyber::core::f32 yValue, cyber::core::f32 zValue,
                     cyber::core::f32 wValue) noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }

    /// Constructs from a math vector.
    /// @param value Source vector.
    constexpr Float4(const cyber::math::Vec4& value) noexcept
        : x(value.x), y(value.y), z(value.z), w(value.w)
    {
    }

    /// Converts to the aligned math vector type for calculations.
    /// @return A value copy.
    [[nodiscard]] constexpr operator cyber::math::Vec4() const noexcept
    {
        return {x, y, z, w};
    }

    /// Returns one component by index.
    /// @param index Component index in [0, 3].
    /// @return The selected component; programmer errors are the caller's responsibility.
    [[nodiscard]] constexpr cyber::core::f32& operator[](usize index) noexcept
    {
        return index == 0 ? x : (index == 1 ? y : (index == 2 ? z : w));
    }

    /// Returns one component by index.
    /// @param index Component index in [0, 3].
    /// @return The selected component; programmer errors are the caller's responsibility.
    [[nodiscard]] constexpr const cyber::core::f32& operator[](usize index) const noexcept
    {
        return index == 0 ? x : (index == 1 ? y : (index == 2 ? z : w));
    }

    /// Adds components.
    /// @param rhs Value to add.
    /// @return Component-wise sum.
    [[nodiscard]] constexpr Float4 operator+(const Float4& rhs) const noexcept
    {
        return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }

    /// Multiplies components by a scalar.
    /// @param scalar Scale factor.
    /// @return Scaled components.
    [[nodiscard]] constexpr Float4 operator*(cyber::core::f32 scalar) const noexcept
    {
        return {x * scalar, y * scalar, z * scalar, w * scalar};
    }

    /// Divides components by a nonzero scalar.
    /// @param scalar Divisor.
    /// @return This value after component-wise division.
    constexpr Float4& operator/=(cyber::core::f32 scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }
};

static_assert(sizeof(Float4) == sizeof(cyber::core::f32) * 4);

/// Full-float CPU vertex; packed GPU representations are produced explicitly.
struct Vertex
{
    cyber::math::Vec3 position{};
    cyber::math::Vec3 normal{0.0f, 0.0f, 1.0f};
    /// Tangent direction in xyz and bitangent handedness in w (+1 or -1).
    Float4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    cyber::math::Vec2 texCoord0{};
    cyber::math::Vec2 texCoord1{};
    Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
    u16 joints[4]{};
    Float4 weights{1.0f, 0.0f, 0.0f, 0.0f};
};

static_assert(sizeof(Vertex) == 12 + 12 + 16 + 8 + 8 + 16 + 8 + 16);

/// One contiguous indexed triangle range using a single material.
struct SubmeshDesc
{
    u32 firstIndex = 0;
    u32 indexCount = 0;
    u32 materialId = 0;
};

/// Non-owning mesh input copied by `Mesh::Create`.
struct MeshDesc
{
    Span<const Vertex> vertices{};
    Span<const u32> indices{};
    Span<const SubmeshDesc> submeshes{};
};

/// One validated submesh and its position-derived bounds.
struct Submesh
{
    u32 firstIndex = 0;
    u32 indexCount = 0;
    u32 materialId = 0;
    cyber::math::Aabb bounds{};
};

/// Owning, validated, indexed CPU mesh. The allocator must outlive this value.
class Mesh
{
public:
    /// Releases arrays through the allocator used to create the mesh.
    ~Mesh() = default;
    /// Meshes transfer their arrays and cannot be copied.
    Mesh(const Mesh&) = delete;
    /// Meshes cannot be copy-assigned.
    Mesh& operator=(const Mesh&) = delete;
    /// Transfers the arrays and leaves `other` empty.
    /// @param other Mesh whose storage is transferred.
    Mesh(Mesh&& other) noexcept = default;
    /// Releases this mesh and transfers arrays from `other`.
    /// @param other Mesh whose storage is transferred.
    /// @return This mesh.
    Mesh& operator=(Mesh&& other) noexcept = default;

    /// Validates and copies full-float vertices, indices, and submeshes.
    /// @param desc Non-owning input; vertices and indices must describe indexed triangles.
    /// @param allocator Storage allocator, which must outlive the returned mesh.
    /// @return A mesh, or InvalidArgument/OutOfRange/Internal for invalid input, overflow, or
    /// allocation failure.
    [[nodiscard]] static Result<Mesh, Error> Create(const MeshDesc& desc,
                                                    cyber::memory::Allocator& allocator) noexcept;

    /// Returns copied mesh vertices.
    /// @return Read-only vertices valid until the mesh is destroyed or moved.
    [[nodiscard]] Span<const Vertex> Vertices() const noexcept;
    /// Returns canonical 32-bit indices.
    /// @return Read-only indices valid until the mesh is destroyed or moved.
    [[nodiscard]] Span<const u32> Indices() const noexcept;
    /// Returns submeshes in index order.
    /// @return Read-only submeshes valid until the mesh is destroyed or moved.
    [[nodiscard]] Span<const Submesh> Submeshes() const noexcept;
    /// Returns bounds enclosing every mesh vertex.
    /// @return Position-derived mesh bounds.
    [[nodiscard]] const cyber::math::Aabb& Bounds() const noexcept;
    /// Returns the number of indexed triangles.
    /// @return Triangle count.
    [[nodiscard]] usize TriangleCount() const noexcept;

private:
    friend Status<Error> GenerateTangents(Mesh& mesh) noexcept;
    explicit Mesh(cyber::memory::Allocator& allocator) noexcept;

    cyber::memory::Allocator* m_allocator = nullptr;
    cyber::memory::DynamicArray<Vertex> m_vertices;
    cyber::memory::DynamicArray<u32> m_indices;
    cyber::memory::DynamicArray<Submesh> m_submeshes;
    cyber::math::Aabb m_bounds{};
};

/// Simplified mesh and measured maximum source-vertex displacement.
struct SimplifiedMesh
{
    /// Takes ownership of the simplified mesh and its measured error.
    /// @param meshValue Simplified mesh to move into this result.
    /// @param error Maximum referenced source-vertex displacement.
    SimplifiedMesh(Mesh&& meshValue, cyber::core::f32 error) noexcept
        : mesh(std::move(meshValue)), maximumVertexError(error)
    {
    }

    Mesh mesh;
    cyber::core::f32 maximumVertexError = 0.0f;
};

/// Reduces triangle count with deterministic, boundary-preserving edge
/// collapses. It may stop above the target when no safe collapse remains.
/// @param source Validated source mesh.
/// @param targetTriangleCount Requested total triangles; must be non-zero.
/// @param allocator Storage allocator, which must outlive the returned mesh.
/// @return Simplified mesh and measured displacement, or InvalidArgument/OutOfRange/Internal on
/// invalid target, overflow, or allocation failure.
[[nodiscard]] Result<SimplifiedMesh, Error>
SimplifyMesh(const Mesh& source, u32 targetTriangleCount,
             cyber::memory::Allocator& allocator) noexcept;

/// GPU-friendly packed vertex format: float positions, octahedral signed
/// 16-bit normal/tangent, half-float UVs, normalized 8-bit color, and skin data.
struct PackedVertex
{
    float position[3]{};
    cyber::core::i16 normalOct[2]{};
    cyber::core::i16 tangentOct[2]{};
    cyber::core::i16 tangentSign = 1;
    cyber::core::i16 reserved = 0;
    u16 texCoord0[2]{};
    u16 texCoord1[2]{};
    cyber::core::u8 color[4]{};
    u16 joints[4]{};
    u16 weights[4]{};
};

static_assert(sizeof(PackedVertex) == 52);
static_assert(offsetof(PackedVertex, normalOct) == 12);
static_assert(offsetof(PackedVertex, tangentOct) == 16);
static_assert(offsetof(PackedVertex, texCoord0) == 24);
static_assert(offsetof(PackedVertex, color) == 32);
static_assert(offsetof(PackedVertex, joints) == 36);
static_assert(offsetof(PackedVertex, weights) == 44);

/// Index element width selected for a vertex count.
enum class IndexFormat : cyber::core::u8
{
    UInt16,
    UInt32,
};

/// Returns the narrowest index format that can address all vertices.
/// @param vertexCount Number of vertices.
/// @return UInt16 for at most 65,536 vertices, UInt32 otherwise.
[[nodiscard]] IndexFormat SelectIndexFormat(usize vertexCount) noexcept;

/// Recomputes tangent directions and handedness from positions, normals, UV0,
/// and indexed triangles; invalid normals cause InvalidArgument.
/// @param mesh Mutable mesh whose tangents are replaced on success.
/// @return Success, or InvalidArgument/Internal if input is invalid or scratch allocation fails.
[[nodiscard]] Status<Error> GenerateTangents(Mesh& mesh) noexcept;

/// Writes packed vertices after validating representable values and capacity.
/// @param vertices Full-float source vertices.
/// @param output Destination records; must hold every source vertex.
/// @return Success, or InvalidArgument/OutOfRange when an attribute is invalid or output is too
/// small.
[[nodiscard]] Status<Error> PackVertices(Span<const Vertex> vertices,
                                         Span<PackedVertex> output) noexcept;

/// Writes host-native 16-bit or 32-bit indices selected by vertex count.
/// @param indices Canonical indices, all below `vertexCount`.
/// @param vertexCount Number of addressable vertices.
/// @param output Destination bytes.
/// @param format Receives the selected format only on success.
/// @return Bytes written, or InvalidArgument/OutOfRange for invalid indices or insufficient
/// capacity.
[[nodiscard]] Result<usize, Error> PackIndices(Span<const u32> indices, usize vertexCount,
                                               Span<cyber::core::Byte> output,
                                               IndexFormat& format) noexcept;

/// One visible mesh instance with its already-selected LOD and indexed draw.
struct InstanceDrawInput
{
    u64 meshId = 0;
    u32 materialId = 0;
    u32 lod = 0;
    u32 indexCount = 0;
    u32 firstIndex = 0;
    cyber::core::i32 vertexOffset = 0;
    cyber::math::Mat4 transform{};
};

/// Backend-neutral indexed indirect argument layout.
struct DrawIndexedIndirectArguments
{
    u32 indexCount = 0;
    u32 instanceCount = 0;
    u32 firstIndex = 0;
    cyber::core::i32 vertexOffset = 0;
    u32 firstInstance = 0;
};

static_assert(sizeof(DrawIndexedIndirectArguments) == 20);
static_assert(offsetof(DrawIndexedIndirectArguments, instanceCount) == 4);
static_assert(offsetof(DrawIndexedIndirectArguments, firstIndex) == 8);
static_assert(offsetof(DrawIndexedIndirectArguments, vertexOffset) == 12);
static_assert(offsetof(DrawIndexedIndirectArguments, firstInstance) == 16);

/// A batch sharing mesh, material, and LOD; its instance range addresses the
/// transform array owned by `InstanceBatchList`.
struct InstanceBatch
{
    u64 meshId = 0;
    u32 materialId = 0;
    u32 lod = 0;
    DrawIndexedIndirectArguments arguments{};
};

/// Move-only storage for deterministically grouped draws and transforms.
class InstanceBatchList
{
public:
    /// Releases the batch and transform storage through its borrowed allocator.
    ~InstanceBatchList() = default;
    /// Batch lists transfer their allocator-backed storage and cannot be copied.
    InstanceBatchList(const InstanceBatchList&) = delete;
    /// Batch lists cannot be copy-assigned.
    InstanceBatchList& operator=(const InstanceBatchList&) = delete;
    /// Transfers the batch and transform storage.
    /// @param other List whose storage is transferred.
    InstanceBatchList(InstanceBatchList&& other) noexcept = default;
    /// Releases this list and transfers storage from `other`.
    /// @param other List whose storage is transferred.
    /// @return This list.
    InstanceBatchList& operator=(InstanceBatchList&& other) noexcept = default;

    /// Returns deterministic batches.
    /// @return Read-only batch view valid until the list is destroyed or moved.
    [[nodiscard]] Span<const InstanceBatch> Batches() const noexcept;
    /// Returns transforms in batch order.
    /// @return Read-only transforms valid until the list is destroyed or moved.
    [[nodiscard]] Span<const cyber::math::Mat4> Transforms() const noexcept;

private:
    friend Result<InstanceBatchList, Error>
    BuildInstanceBatches(Span<const InstanceDrawInput>, cyber::memory::Allocator&) noexcept;
    explicit InstanceBatchList(cyber::memory::Allocator& allocator) noexcept;

    cyber::memory::DynamicArray<InstanceBatch> m_batches;
    cyber::memory::DynamicArray<cyber::math::Mat4> m_transforms;
};

/// Groups instances by mesh/material/LOD/index range and emits checked indirect
/// argument records without issuing GPU commands.
/// @param instances Visible instance inputs; input order does not affect output.
/// @param allocator Storage allocator, which must outlive the returned list.
/// @return Sorted draw batches, or InvalidArgument/OutOfRange/Internal for bad input or allocation
/// failure.
[[nodiscard]] Result<InstanceBatchList, Error>
BuildInstanceBatches(Span<const InstanceDrawInput> instances,
                     cyber::memory::Allocator& allocator) noexcept;

}  // namespace cyber::scene
