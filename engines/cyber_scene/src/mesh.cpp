#include <cyber/scene/mesh.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace cyber::scene
{
namespace
{

using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::f32;
using cyber::core::f64;
using cyber::core::i16;
using cyber::core::i32;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::usize;

[[nodiscard]] Error MakeError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool IsFinite(f32 value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool IsFinite(const cyber::math::Vec2& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] bool IsFinite(const cyber::math::Vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFinite(const cyber::math::Vec4& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] bool IsFinite(const cyber::math::Mat4& value) noexcept
{
    for (usize column = 0; column < 4; ++column)
    {
        const cyber::math::Vec4& entry = value[column];
        if (!IsFinite(entry))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsAffine(const cyber::math::Mat4& value) noexcept
{
    return value[0].w == 0.0f && value[1].w == 0.0f && value[2].w == 0.0f && value[3].w == 1.0f;
}

[[nodiscard]] bool IsValid(const Vertex& vertex) noexcept
{
    if (!IsFinite(vertex.position) || !IsFinite(vertex.normal) || !IsFinite(vertex.tangent) ||
        !IsFinite(vertex.texCoord0) || !IsFinite(vertex.texCoord1) || !IsFinite(vertex.color) ||
        !IsFinite(vertex.weights) || vertex.weights.x < 0.0f || vertex.weights.y < 0.0f ||
        vertex.weights.z < 0.0f || vertex.weights.w < 0.0f)
    {
        return false;
    }
    const f32 weightSum = vertex.weights.x + vertex.weights.y + vertex.weights.z + vertex.weights.w;
    return IsFinite(weightSum) && weightSum > 0.0f;
}

[[nodiscard]] bool ToHalf(f32 value, u16& result) noexcept
{
    if (!IsFinite(value) || std::abs(value) > 65504.0f)
    {
        return false;
    }

    const u32 bits = std::bit_cast<u32>(value);
    const u32 sign = (bits >> 16U) & 0x8000U;
    const u32 exponent = (bits >> 23U) & 0xffU;
    u32 mantissa = bits & 0x7fffffU;
    i32 halfExponent = static_cast<i32>(exponent) - 127 + 15;

    if (halfExponent <= 0)
    {
        if (halfExponent < -10)
        {
            result = static_cast<u16>(sign);
            return true;
        }
        mantissa |= 0x800000U;
        const u32 shift = static_cast<u32>(14 - halfExponent);
        const u32 halfway = (1U << (shift - 1U)) - 1U;
        const u32 roundBit = (mantissa >> shift) & 1U;
        result = static_cast<u16>(sign | ((mantissa + halfway + roundBit) >> shift));
        return true;
    }

    if (halfExponent >= 31)
    {
        return false;
    }

    mantissa += 0xfffU + ((mantissa >> 13U) & 1U);
    if ((mantissa & 0x800000U) != 0)
    {
        mantissa = 0;
        ++halfExponent;
        if (halfExponent >= 31)
        {
            return false;
        }
    }
    result = static_cast<u16>(sign | (static_cast<u32>(halfExponent) << 10U) | (mantissa >> 13U));
    return true;
}

[[nodiscard]] i16 PackSnorm16(f32 value) noexcept
{
    const f32 clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<i16>(std::lround(clamped * 32767.0f));
}

[[nodiscard]] bool PackOctahedral(const cyber::math::Vec3& direction, i16 output[2]) noexcept
{
    if (!IsFinite(direction))
    {
        return false;
    }
    cyber::math::Vec3 normalized = cyber::math::Normalize(direction);
    if (cyber::math::LengthSquared(normalized) < 0.5f)
    {
        return false;
    }
    const f32 inverseL1 =
        1.0f / (std::abs(normalized.x) + std::abs(normalized.y) + std::abs(normalized.z));
    f32 x = normalized.x * inverseL1;
    f32 y = normalized.y * inverseL1;
    if (normalized.z < 0.0f)
    {
        const f32 oldX = x;
        x = (1.0f - std::abs(y)) * (oldX < 0.0f ? -1.0f : 1.0f);
        y = (1.0f - std::abs(oldX)) * (y < 0.0f ? -1.0f : 1.0f);
    }
    output[0] = PackSnorm16(x);
    output[1] = PackSnorm16(y);
    return true;
}

void ExpandEdge(DynamicArray<u32>& edges, u32 first, u32 second, bool& success) noexcept
{
    if (first > second)
    {
        std::swap(first, second);
    }
    success = edges.TryPushBack(first);
    if (success)
    {
        success = edges.TryPushBack(second);
    }
}

[[nodiscard]] u32 FindRoot(DynamicArray<u32>& parent, u32 index) noexcept
{
    u32 root = index;
    while (parent[root] != root)
    {
        root = parent[root];
    }
    while (parent[index] != index)
    {
        const u32 next = parent[index];
        parent[index] = root;
        index = next;
    }
    return root;
}

struct EdgeCandidate
{
    u32 first = 0;
    u32 second = 0;
    f64 distanceSquared = 0.0;
    bool boundary = false;
};

struct EdgePair
{
    u32 first = 0;
    u32 second = 0;
};

[[nodiscard]] bool HasInvertedFace(const DynamicArray<Vertex>& vertices,
                                   const DynamicArray<u32>& indices, u32 firstIndex, u32 indexCount,
                                   u32 keep, u32 remove,
                                   const cyber::math::Vec3& replacement) noexcept
{
    for (u32 offset = 0; offset < indexCount; offset += 3)
    {
        const u32 triangle[3] = {indices[firstIndex + offset], indices[firstIndex + offset + 1],
                                 indices[firstIndex + offset + 2]};
        if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0])
        {
            continue;
        }
        bool containsKeep = false;
        bool containsRemove = false;
        for (u32 corner = 0; corner < 3; ++corner)
        {
            containsKeep = containsKeep || triangle[corner] == keep;
            containsRemove = containsRemove || triangle[corner] == remove;
        }
        if (!containsKeep && !containsRemove)
        {
            continue;
        }
        if (containsKeep && containsRemove)
        {
            continue;
        }

        cyber::math::Vec3 oldPositions[3];
        cyber::math::Vec3 newPositions[3];
        for (u32 corner = 0; corner < 3; ++corner)
        {
            const u32 vertexIndex = triangle[corner];
            oldPositions[corner] = vertices[vertexIndex].position;
            newPositions[corner] =
                (vertexIndex == keep || vertexIndex == remove) ? replacement : oldPositions[corner];
        }
        const cyber::math::Vec3 oldNormal = cyber::math::Cross(oldPositions[1] - oldPositions[0],
                                                               oldPositions[2] - oldPositions[0]);
        if (cyber::math::LengthSquared(oldNormal) <= 1.0e-16f)
        {
            continue;
        }
        const cyber::math::Vec3 newNormal = cyber::math::Cross(newPositions[1] - newPositions[0],
                                                               newPositions[2] - newPositions[0]);
        if (cyber::math::LengthSquared(newNormal) <= 1.0e-16f ||
            cyber::math::Dot(oldNormal, newNormal) <= 0.0f)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Vertex MergeVertices(const Vertex& first, const Vertex& second) noexcept
{
    Vertex merged = first;
    merged.position = first.position * 0.5f + second.position * 0.5f;
    merged.normal = cyber::math::Normalize(first.normal + second.normal);
    merged.texCoord0 = (first.texCoord0 + second.texCoord0) * 0.5f;
    merged.texCoord1 = (first.texCoord1 + second.texCoord1) * 0.5f;
    merged.color = (first.color + second.color) * 0.5f;
    merged.tangent = (first.tangent + second.tangent) * 0.5f;
    merged.weights = (first.weights + second.weights) * 0.5f;
    const f32 weightSum = merged.weights.x + merged.weights.y + merged.weights.z + merged.weights.w;
    if (weightSum > 0.0f)
    {
        merged.weights /= weightSum;
    }
    return merged;
}

}  // namespace

Mesh::Mesh(Allocator& allocator) noexcept
    : m_allocator(&allocator), m_vertices(allocator), m_indices(allocator), m_submeshes(allocator)
{
}

Result<Mesh, Error> Mesh::Create(const MeshDesc& desc, Allocator& allocator) noexcept
{
    if (desc.vertices.Data() == nullptr || desc.indices.Data() == nullptr ||
        desc.submeshes.Data() == nullptr || desc.vertices.Empty() || desc.indices.Size() < 3 ||
        desc.indices.Size() % 3 != 0 || desc.submeshes.Empty())
    {
        return Result<Mesh, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "mesh counts or submesh list are invalid"));
    }
    if (desc.vertices.Size() > std::numeric_limits<u32>::max() ||
        desc.indices.Size() > std::numeric_limits<u32>::max() ||
        desc.submeshes.Size() > std::numeric_limits<u32>::max())
    {
        return Result<Mesh, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "mesh counts exceed 32-bit index ranges"));
    }

    for (usize index = 0; index < desc.vertices.Size(); ++index)
    {
        if (!IsValid(desc.vertices[index]))
        {
            return Result<Mesh, Error>::Err(
                MakeError(ErrorCode::InvalidArgument, "mesh vertex contains invalid attributes"));
        }
    }
    for (usize offset = 0; offset < desc.indices.Size(); ++offset)
    {
        if (desc.indices[offset] >= desc.vertices.Size())
        {
            return Result<Mesh, Error>::Err(
                MakeError(ErrorCode::InvalidArgument, "mesh index is outside the vertex array"));
        }
    }

    Mesh mesh(allocator);
    if (!mesh.m_vertices.TryReserve(desc.vertices.Size()) ||
        !mesh.m_indices.TryReserve(desc.indices.Size()) ||
        !mesh.m_submeshes.TryReserve(desc.submeshes.Size()))
    {
        return Result<Mesh, Error>::Err(MakeError(ErrorCode::Internal, "mesh allocation failed"));
    }
    for (usize index = 0; index < desc.vertices.Size(); ++index)
    {
        const Vertex& vertex = desc.vertices[index];
        if (!mesh.m_vertices.TryPushBack(vertex))
        {
            return Result<Mesh, Error>::Err(
                MakeError(ErrorCode::Internal, "mesh vertex allocation failed"));
        }
        mesh.m_bounds.Expand(vertex.position);
    }
    for (usize index = 0; index < desc.indices.Size(); ++index)
    {
        const u32 vertexIndex = desc.indices[index];
        if (!mesh.m_indices.TryPushBack(vertexIndex))
        {
            return Result<Mesh, Error>::Err(
                MakeError(ErrorCode::Internal, "mesh index allocation failed"));
        }
    }

    u32 expectedFirstIndex = 0;
    for (usize submeshIndex = 0; submeshIndex < desc.submeshes.Size(); ++submeshIndex)
    {
        const SubmeshDesc& input = desc.submeshes[submeshIndex];
        if (input.indexCount == 0 || input.indexCount % 3 != 0 ||
            input.firstIndex != expectedFirstIndex ||
            input.indexCount > desc.indices.Size() - expectedFirstIndex)
        {
            return Result<Mesh, Error>::Err(MakeError(
                ErrorCode::InvalidArgument, "submesh ranges must partition indexed triangles"));
        }

        Submesh submesh{};
        submesh.firstIndex = input.firstIndex;
        submesh.indexCount = input.indexCount;
        submesh.materialId = input.materialId;
        for (u32 offset = 0; offset < input.indexCount; ++offset)
        {
            const u32 vertexIndex = desc.indices[input.firstIndex + offset];
            submesh.bounds.Expand(desc.vertices[vertexIndex].position);
        }
        if (!mesh.m_submeshes.TryPushBack(submesh))
        {
            return Result<Mesh, Error>::Err(
                MakeError(ErrorCode::Internal, "mesh submesh allocation failed"));
        }
        expectedFirstIndex += input.indexCount;
    }
    if (expectedFirstIndex != desc.indices.Size())
    {
        return Result<Mesh, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "submesh ranges do not cover all mesh indices"));
    }
    return Result<Mesh, Error>::Ok(std::move(mesh));
}

Span<const Vertex> Mesh::Vertices() const noexcept
{
    return {m_vertices.Data(), m_vertices.Size()};
}

Span<const u32> Mesh::Indices() const noexcept
{
    return {m_indices.Data(), m_indices.Size()};
}

Span<const Submesh> Mesh::Submeshes() const noexcept
{
    return {m_submeshes.Data(), m_submeshes.Size()};
}

const cyber::math::Aabb& Mesh::Bounds() const noexcept
{
    return m_bounds;
}

usize Mesh::TriangleCount() const noexcept
{
    return m_indices.Size() / 3;
}

IndexFormat SelectIndexFormat(usize vertexCount) noexcept
{
    return vertexCount <= 65536 ? IndexFormat::UInt16 : IndexFormat::UInt32;
}

Status<Error> GenerateTangents(Mesh& mesh) noexcept
{
    const Span<const Vertex> vertices = mesh.Vertices();
    const Span<const u32> indices = mesh.Indices();
    if (vertices.Empty() || indices.Empty())
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "tangent generation requires an indexed mesh"));
    }
    for (usize index = 0; index < vertices.Size(); ++index)
    {
        if (cyber::math::LengthSquared(vertices[index].normal) <= 1.0e-12f)
        {
            return Status<Error>::Err(MakeError(ErrorCode::InvalidArgument,
                                                "tangent generation requires nonzero normals"));
        }
    }

    DynamicArray<cyber::math::Vec3> tangentSums(*mesh.m_allocator);
    DynamicArray<cyber::math::Vec3> bitangentSums(*mesh.m_allocator);
    if (!tangentSums.TryResize(vertices.Size()) || !bitangentSums.TryResize(vertices.Size()))
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::Internal, "tangent scratch allocation failed"));
    }

    for (usize triangle = 0; triangle < indices.Size(); triangle += 3)
    {
        const u32 i0 = indices[triangle];
        const u32 i1 = indices[triangle + 1];
        const u32 i2 = indices[triangle + 2];
        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];
        const cyber::math::Vec3 edge1 = v1.position - v0.position;
        const cyber::math::Vec3 edge2 = v2.position - v0.position;
        const cyber::math::Vec2 uv1 = v1.texCoord0 - v0.texCoord0;
        const cyber::math::Vec2 uv2 = v2.texCoord0 - v0.texCoord0;
        const f32 determinant = uv1.x * uv2.y - uv1.y * uv2.x;
        if (!IsFinite(determinant) || std::abs(determinant) <= 1.0e-12f)
        {
            continue;
        }
        const f32 inverse = 1.0f / determinant;
        const cyber::math::Vec3 tangent = (edge1 * uv2.y - edge2 * uv1.y) * inverse;
        const cyber::math::Vec3 bitangent = (edge2 * uv1.x - edge1 * uv2.x) * inverse;
        if (!IsFinite(tangent) || !IsFinite(bitangent))
        {
            return Status<Error>::Err(
                MakeError(ErrorCode::InvalidArgument, "tangent derivative is not finite"));
        }
        tangentSums[i0] += tangent;
        tangentSums[i1] += tangent;
        tangentSums[i2] += tangent;
        bitangentSums[i0] += bitangent;
        bitangentSums[i1] += bitangent;
        bitangentSums[i2] += bitangent;
    }

    for (usize index = 0; index < vertices.Size(); ++index)
    {
        const cyber::math::Vec3 normal = cyber::math::Normalize(vertices[index].normal);
        cyber::math::Vec3 tangent =
            tangentSums[index] - normal * cyber::math::Dot(normal, tangentSums[index]);
        if (cyber::math::LengthSquared(tangent) <= 1.0e-12f)
        {
            const cyber::math::Vec3 axis =
                std::abs(normal.x) <= std::abs(normal.y) && std::abs(normal.x) <= std::abs(normal.z)
                    ? cyber::math::Vec3{1.0f, 0.0f, 0.0f}
                    : (std::abs(normal.y) <= std::abs(normal.z)
                           ? cyber::math::Vec3{0.0f, 1.0f, 0.0f}
                           : cyber::math::Vec3{0.0f, 0.0f, 1.0f});
            tangent = cyber::math::Cross(axis, normal);
        }
        tangent = cyber::math::Normalize(tangent);
        const f32 handedness = cyber::math::LengthSquared(bitangentSums[index]) > 1.0e-12f &&
                                       cyber::math::Dot(cyber::math::Cross(normal, tangent),
                                                        bitangentSums[index]) < 0.0f
                                   ? -1.0f
                                   : 1.0f;
        mesh.m_vertices[index].tangent =
            cyber::math::Vec4{tangent.x, tangent.y, tangent.z, handedness};
    }
    return Status<Error>::Ok();
}

Status<Error> PackVertices(Span<const Vertex> vertices, Span<PackedVertex> output) noexcept
{
    if ((!vertices.Empty() && vertices.Data() == nullptr) ||
        (!output.Empty() && output.Data() == nullptr))
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "vertex input or output storage is null"));
    }
    if (vertices.Size() > output.Size())
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::OutOfRange, "packed vertex output is too small"));
    }
    for (usize index = 0; index < vertices.Size(); ++index)
    {
        const Vertex& vertex = vertices[index];
        u16 halfValue = 0;
        i16 octahedral[2]{};
        if (!IsValid(vertex) || vertex.color.x < 0.0f || vertex.color.x > 1.0f ||
            vertex.color.y < 0.0f || vertex.color.y > 1.0f || vertex.color.z < 0.0f ||
            vertex.color.z > 1.0f || vertex.color.w < 0.0f || vertex.color.w > 1.0f ||
            std::abs(vertex.tangent.w) != 1.0f || !ToHalf(vertex.texCoord0.x, halfValue) ||
            !ToHalf(vertex.texCoord0.y, halfValue) || !ToHalf(vertex.texCoord1.x, halfValue) ||
            !ToHalf(vertex.texCoord1.y, halfValue) || !PackOctahedral(vertex.normal, octahedral) ||
            !PackOctahedral(cyber::math::Vec3{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z},
                            octahedral))
        {
            return Status<Error>::Err(MakeError(ErrorCode::InvalidArgument,
                                                "vertex cannot be represented in packed format"));
        }
    }

    for (usize index = 0; index < vertices.Size(); ++index)
    {
        const Vertex& vertex = vertices[index];
        PackedVertex packed{};
        packed.position[0] = vertex.position.x;
        packed.position[1] = vertex.position.y;
        packed.position[2] = vertex.position.z;
        (void)PackOctahedral(vertex.normal, packed.normalOct);
        (void)PackOctahedral(
            cyber::math::Vec3{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z},
            packed.tangentOct);
        packed.tangentSign = vertex.tangent.w < 0.0f ? -1 : 1;
        (void)ToHalf(vertex.texCoord0.x, packed.texCoord0[0]);
        (void)ToHalf(vertex.texCoord0.y, packed.texCoord0[1]);
        (void)ToHalf(vertex.texCoord1.x, packed.texCoord1[0]);
        (void)ToHalf(vertex.texCoord1.y, packed.texCoord1[1]);
        packed.color[0] = static_cast<cyber::core::u8>(std::lround(vertex.color.x * 255.0f));
        packed.color[1] = static_cast<cyber::core::u8>(std::lround(vertex.color.y * 255.0f));
        packed.color[2] = static_cast<cyber::core::u8>(std::lround(vertex.color.z * 255.0f));
        packed.color[3] = static_cast<cyber::core::u8>(std::lround(vertex.color.w * 255.0f));
        const f32 weightSum =
            vertex.weights.x + vertex.weights.y + vertex.weights.z + vertex.weights.w;
        usize largestWeight = 0;
        for (usize component = 1; component < 4; ++component)
        {
            if (vertex.weights[component] > vertex.weights[largestWeight])
            {
                largestWeight = component;
            }
        }
        u32 otherWeightSum = 0;
        for (usize component = 0; component < 4; ++component)
        {
            packed.joints[component] = vertex.joints[component];
            if (component != largestWeight)
            {
                packed.weights[component] = static_cast<u16>(
                    std::floor((vertex.weights[component] / weightSum) * 65535.0f));
                otherWeightSum += packed.weights[component];
            }
        }
        packed.weights[largestWeight] = static_cast<u16>(65535U - otherWeightSum);
        output[index] = packed;
    }
    return Status<Error>::Ok();
}

Result<usize, Error> PackIndices(Span<const u32> indices, usize vertexCount, Span<Byte> output,
                                 IndexFormat& format) noexcept
{
    if (indices.Data() == nullptr || vertexCount == 0 || indices.Empty())
    {
        return Result<usize, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "index packing requires vertices and indices"));
    }
    if (vertexCount > std::numeric_limits<u32>::max())
    {
        return Result<usize, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "vertex count exceeds 32-bit indices"));
    }
    const IndexFormat selected = SelectIndexFormat(vertexCount);
    const usize indexSize = selected == IndexFormat::UInt16 ? sizeof(u16) : sizeof(u32);
    if (indices.Size() > std::numeric_limits<usize>::max() / indexSize)
    {
        return Result<usize, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "packed index byte count overflows"));
    }
    const usize requiredBytes = indices.Size() * indexSize;
    if (!output.Empty() && output.Data() == nullptr)
    {
        return Result<usize, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "index output storage is null"));
    }
    if (output.Size() < requiredBytes)
    {
        return Result<usize, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "packed index output is too small"));
    }
    for (usize offset = 0; offset < indices.Size(); ++offset)
    {
        if (indices[offset] >= vertexCount ||
            (selected == IndexFormat::UInt16 && indices[offset] > std::numeric_limits<u16>::max()))
        {
            return Result<usize, Error>::Err(MakeError(
                ErrorCode::InvalidArgument, "index cannot be represented by selected format"));
        }
    }
    if (selected == IndexFormat::UInt16)
    {
        for (usize index = 0; index < indices.Size(); ++index)
        {
            const u16 value = static_cast<u16>(indices[index]);
            std::memcpy(output.Data() + index * sizeof(value), &value, sizeof(value));
        }
    }
    else
    {
        std::memcpy(output.Data(), indices.Data(), requiredBytes);
    }
    format = selected;
    return Result<usize, Error>::Ok(requiredBytes);
}

Result<SimplifiedMesh, Error> SimplifyMesh(const Mesh& source, u32 targetTriangleCount,
                                           Allocator& allocator) noexcept
{
    if (source.TriangleCount() == 0 || source.TriangleCount() > std::numeric_limits<u32>::max() ||
        targetTriangleCount < source.Submeshes().Size() ||
        targetTriangleCount > source.TriangleCount())
    {
        return Result<SimplifiedMesh, Error>::Err(MakeError(
            ErrorCode::InvalidArgument, "simplification target must be within the source range"));
    }

    const Span<const Vertex> sourceVertices = source.Vertices();
    const Span<const u32> sourceIndices = source.Indices();
    const Span<const Submesh> submeshes = source.Submeshes();
    DynamicArray<Vertex> workVertices(allocator);
    DynamicArray<u32> workIndices(allocator);
    DynamicArray<u32> parent(allocator);
    DynamicArray<u32> membershipCount(allocator);
    DynamicArray<u32> lastMembership(allocator);
    DynamicArray<u32> boundaryEpoch(allocator);
    DynamicArray<Vertex> outputVertices(allocator);
    DynamicArray<u32> outputIndices(allocator);
    DynamicArray<SubmeshDesc> outputSubmeshes(allocator);
    DynamicArray<u32> remap(allocator);
    DynamicArray<u32> edgeWords(allocator);
    DynamicArray<EdgeCandidate> candidates(allocator);

    if (sourceIndices.Size() > std::numeric_limits<usize>::max() / 2)
    {
        return Result<SimplifiedMesh, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "simplification edge count overflows"));
    }
    if (!workVertices.TryReserve(sourceVertices.Size()) ||
        !workIndices.TryReserve(sourceIndices.Size()) || !parent.TryResize(sourceVertices.Size()) ||
        !membershipCount.TryResize(sourceVertices.Size()) ||
        !lastMembership.TryResize(sourceVertices.Size()) ||
        !boundaryEpoch.TryResize(sourceVertices.Size()) ||
        !outputVertices.TryReserve(sourceVertices.Size()) ||
        !outputIndices.TryReserve(sourceIndices.Size()) ||
        !outputSubmeshes.TryReserve(submeshes.Size()) || !remap.TryResize(sourceVertices.Size()) ||
        !edgeWords.TryReserve(sourceIndices.Size() * 2) ||
        !candidates.TryReserve(sourceIndices.Size()))
    {
        return Result<SimplifiedMesh, Error>::Err(
            MakeError(ErrorCode::Internal, "simplification allocation failed"));
    }
    for (usize index = 0; index < sourceVertices.Size(); ++index)
    {
        if (!workVertices.TryPushBack(sourceVertices[index]))
        {
            return Result<SimplifiedMesh, Error>::Err(
                MakeError(ErrorCode::Internal, "simplification vertex allocation failed"));
        }
        parent[index] = static_cast<u32>(index);
        lastMembership[index] = std::numeric_limits<u32>::max();
        boundaryEpoch[index] = 0;
    }
    for (usize index = 0; index < sourceIndices.Size(); ++index)
    {
        if (!workIndices.TryPushBack(sourceIndices[index]))
        {
            return Result<SimplifiedMesh, Error>::Err(
                MakeError(ErrorCode::Internal, "simplification index allocation failed"));
        }
    }

    u64 totalSourceTriangles = source.TriangleCount();
    u32 remainingTarget = targetTriangleCount;
    u64 remainingSource = totalSourceTriangles;
    for (usize submeshIndex = 0; submeshIndex < submeshes.Size(); ++submeshIndex)
    {
        const Submesh& submesh = submeshes[submeshIndex];
        const u32 sourceTriangleCount = submesh.indexCount / 3;
        const u32 targetForSubmesh =
            submeshIndex + 1 == submeshes.Size()
                ? remainingTarget
                : static_cast<u32>(
                      std::max<u64>(1, (static_cast<u64>(remainingTarget) * sourceTriangleCount) /
                                           remainingSource));
        const u32 localTarget = std::min(sourceTriangleCount, targetForSubmesh);
        remainingTarget -= localTarget;
        remainingSource -= sourceTriangleCount;

        for (u32 offset = 0; offset < submesh.indexCount; ++offset)
        {
            const u32 vertexIndex = sourceIndices[submesh.firstIndex + offset];
            if (lastMembership[vertexIndex] != submeshIndex)
            {
                ++membershipCount[vertexIndex];
                lastMembership[vertexIndex] = static_cast<u32>(submeshIndex);
            }
        }

        edgeWords.Clear();
        candidates.Clear();
        for (u32 offset = 0; offset < submesh.indexCount; offset += 3)
        {
            const u32 triangle[3] = {sourceIndices[submesh.firstIndex + offset],
                                     sourceIndices[submesh.firstIndex + offset + 1],
                                     sourceIndices[submesh.firstIndex + offset + 2]};
            bool success = true;
            ExpandEdge(edgeWords, triangle[0], triangle[1], success);
            if (success)
            {
                ExpandEdge(edgeWords, triangle[1], triangle[2], success);
            }
            if (success)
            {
                ExpandEdge(edgeWords, triangle[2], triangle[0], success);
            }
            if (!success)
            {
                return Result<SimplifiedMesh, Error>::Err(
                    MakeError(ErrorCode::Internal, "simplification edge allocation failed"));
            }
        }
        DynamicArray<EdgePair> sortedEdges(allocator);
        if (!sortedEdges.TryResize(edgeWords.Size() / 2))
        {
            return Result<SimplifiedMesh, Error>::Err(
                MakeError(ErrorCode::Internal, "simplification edge allocation failed"));
        }
        for (usize index = 0; index < sortedEdges.Size(); ++index)
        {
            sortedEdges[index] = EdgePair{edgeWords[index * 2], edgeWords[index * 2 + 1]};
        }
        std::sort(sortedEdges.Data(), sortedEdges.Data() + sortedEdges.Size(),
                  [](const EdgePair& lhs, const EdgePair& rhs) noexcept {
                      return lhs.first < rhs.first ||
                             (lhs.first == rhs.first && lhs.second < rhs.second);
                  });
        for (usize edgeIndex = 0; edgeIndex < sortedEdges.Size();)
        {
            usize end = edgeIndex + 1;
            while (end < sortedEdges.Size() &&
                   sortedEdges[end].first == sortedEdges[edgeIndex].first &&
                   sortedEdges[end].second == sortedEdges[edgeIndex].second)
            {
                ++end;
            }
            const u32 first = sortedEdges[edgeIndex].first;
            const u32 second = sortedEdges[edgeIndex].second;
            const f64 deltaX = static_cast<f64>(sourceVertices[first].position.x) -
                               static_cast<f64>(sourceVertices[second].position.x);
            const f64 deltaY = static_cast<f64>(sourceVertices[first].position.y) -
                               static_cast<f64>(sourceVertices[second].position.y);
            const f64 deltaZ = static_cast<f64>(sourceVertices[first].position.z) -
                               static_cast<f64>(sourceVertices[second].position.z);
            const bool boundary = end - edgeIndex == 1;
            if (boundary)
            {
                boundaryEpoch[first] = static_cast<u32>(submeshIndex + 1);
                boundaryEpoch[second] = static_cast<u32>(submeshIndex + 1);
            }
            if (!candidates.TryPushBack(EdgeCandidate{
                    first, second, deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ, boundary}))
            {
                return Result<SimplifiedMesh, Error>::Err(
                    MakeError(ErrorCode::Internal, "simplification candidate allocation failed"));
            }
            edgeIndex = end;
        }
        std::sort(candidates.Data(), candidates.Data() + candidates.Size(),
                  [](const EdgeCandidate& lhs, const EdgeCandidate& rhs) noexcept
                  {
                      return lhs.distanceSquared < rhs.distanceSquared ||
                             (lhs.distanceSquared == rhs.distanceSquared &&
                              (lhs.first < rhs.first ||
                               (lhs.first == rhs.first && lhs.second < rhs.second)));
                  });

        u32 currentTriangleCount = sourceTriangleCount;
        for (usize candidateIndex = 0; candidateIndex < candidates.Size(); ++candidateIndex)
        {
            if (currentTriangleCount <= localTarget)
            {
                break;
            }
            const EdgeCandidate& candidate = candidates[candidateIndex];
            u32 first = FindRoot(parent, candidate.first);
            u32 second = FindRoot(parent, candidate.second);
            if (first == second || membershipCount[first] != 1 || membershipCount[second] != 1)
            {
                continue;
            }
            if (first > second)
            {
                std::swap(first, second);
            }
            const bool firstBoundary = boundaryEpoch[first] == submeshIndex + 1;
            const bool secondBoundary = boundaryEpoch[second] == submeshIndex + 1;
            if (firstBoundary != secondBoundary || (firstBoundary && !candidate.boundary) ||
                workVertices[first].joints[0] != workVertices[second].joints[0] ||
                workVertices[first].joints[1] != workVertices[second].joints[1] ||
                workVertices[first].joints[2] != workVertices[second].joints[2] ||
                workVertices[first].joints[3] != workVertices[second].joints[3])
            {
                continue;
            }
            const cyber::math::Vec3 replacement =
                workVertices[first].position * 0.5f + workVertices[second].position * 0.5f;
            if (HasInvertedFace(workVertices, workIndices, submesh.firstIndex, submesh.indexCount,
                                first, second, replacement))
            {
                continue;
            }

            const Vertex merged = MergeVertices(workVertices[first], workVertices[second]);
            if (cyber::math::LengthSquared(merged.normal) <= 1.0e-12f)
            {
                continue;
            }
            workVertices[first] = merged;
            parent[second] = first;
            for (u32 offset = 0; offset < submesh.indexCount; ++offset)
            {
                u32& index = workIndices[submesh.firstIndex + offset];
                if (index == second)
                {
                    index = first;
                }
            }
            currentTriangleCount = 0;
            for (u32 offset = 0; offset < submesh.indexCount; offset += 3)
            {
                const u32 a = workIndices[submesh.firstIndex + offset];
                const u32 b = workIndices[submesh.firstIndex + offset + 1];
                const u32 c = workIndices[submesh.firstIndex + offset + 2];
                if (a != b && b != c && c != a)
                {
                    ++currentTriangleCount;
                }
            }
        }
    }

    f32 maximumVertexError = 0.0f;
    for (usize index = 0; index < sourceIndices.Size(); ++index)
    {
        const u32 sourceIndex = sourceIndices[index];
        const u32 root = FindRoot(parent, sourceIndex);
        const cyber::math::Vec3& original = sourceVertices[sourceIndex].position;
        const cyber::math::Vec3& simplified = workVertices[root].position;
        const f64 distance = std::hypot(static_cast<f64>(original.x) - simplified.x,
                                        static_cast<f64>(original.y) - simplified.y,
                                        static_cast<f64>(original.z) - simplified.z);
        if (!std::isfinite(distance) || distance > std::numeric_limits<f32>::max())
        {
            return Result<SimplifiedMesh, Error>::Err(
                MakeError(ErrorCode::OutOfRange, "simplification error exceeds float range"));
        }
        maximumVertexError = std::max(maximumVertexError, static_cast<f32>(distance));
    }

    for (usize index = 0; index < remap.Size(); ++index)
    {
        remap[index] = std::numeric_limits<u32>::max();
    }
    u32 outputFirstIndex = 0;
    for (usize submeshIndex = 0; submeshIndex < submeshes.Size(); ++submeshIndex)
    {
        const Submesh& submesh = submeshes[submeshIndex];
        const u32 firstIndex = outputFirstIndex;
        for (u32 offset = 0; offset < submesh.indexCount; offset += 3)
        {
            const u32 sourceTriangle[3] = {workIndices[submesh.firstIndex + offset],
                                           workIndices[submesh.firstIndex + offset + 1],
                                           workIndices[submesh.firstIndex + offset + 2]};
            if (sourceTriangle[0] == sourceTriangle[1] || sourceTriangle[1] == sourceTriangle[2] ||
                sourceTriangle[2] == sourceTriangle[0])
            {
                continue;
            }
            for (u32 corner = 0; corner < 3; ++corner)
            {
                const u32 root = FindRoot(parent, sourceTriangle[corner]);
                if (remap[root] == std::numeric_limits<u32>::max())
                {
                    if (outputVertices.Size() >= std::numeric_limits<u32>::max() ||
                        !outputVertices.TryPushBack(workVertices[root]))
                    {
                        return Result<SimplifiedMesh, Error>::Err(
                            MakeError(ErrorCode::Internal, "simplified vertex allocation failed"));
                    }
                    remap[root] = static_cast<u32>(outputVertices.Size() - 1);
                }
                if (!outputIndices.TryPushBack(remap[root]))
                {
                    return Result<SimplifiedMesh, Error>::Err(
                        MakeError(ErrorCode::Internal, "simplified index allocation failed"));
                }
                ++outputFirstIndex;
            }
        }
        const u32 indexCount = outputFirstIndex - firstIndex;
        if (indexCount == 0 ||
            !outputSubmeshes.TryPushBack(SubmeshDesc{firstIndex, indexCount, submesh.materialId}))
        {
            return Result<SimplifiedMesh, Error>::Err(
                MakeError(ErrorCode::Internal, "simplified submesh allocation failed"));
        }
    }

    const MeshDesc outputDesc{{outputVertices.Data(), outputVertices.Size()},
                              {outputIndices.Data(), outputIndices.Size()},
                              {outputSubmeshes.Data(), outputSubmeshes.Size()}};
    auto outputResult = Mesh::Create(outputDesc, allocator);
    if (!outputResult)
    {
        return Result<SimplifiedMesh, Error>::Err(outputResult.Error());
    }
    Mesh output = std::move(outputResult.Value());
    const Status<Error> tangentStatus = GenerateTangents(output);
    if (!tangentStatus)
    {
        return Result<SimplifiedMesh, Error>::Err(tangentStatus.Error());
    }
    return Result<SimplifiedMesh, Error>::Ok(SimplifiedMesh{std::move(output), maximumVertexError});
}

InstanceBatchList::InstanceBatchList(Allocator& allocator) noexcept
    : m_batches(allocator), m_transforms(allocator)
{
}

Span<const InstanceBatch> InstanceBatchList::Batches() const noexcept
{
    return {m_batches.Data(), m_batches.Size()};
}

Span<const cyber::math::Mat4> InstanceBatchList::Transforms() const noexcept
{
    return {m_transforms.Data(), m_transforms.Size()};
}

Result<InstanceBatchList, Error> BuildInstanceBatches(Span<const InstanceDrawInput> instances,
                                                      Allocator& allocator) noexcept
{
    if (instances.Data() == nullptr && !instances.Empty())
    {
        return Result<InstanceBatchList, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "instance input storage is null"));
    }
    if (instances.Size() > std::numeric_limits<u32>::max())
    {
        return Result<InstanceBatchList, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "instance draw count exceeds 32-bit arguments"));
    }
    DynamicArray<InstanceDrawInput> sorted(allocator);
    if (!sorted.TryReserve(instances.Size()))
    {
        return Result<InstanceBatchList, Error>::Err(
            MakeError(ErrorCode::Internal, "instance sort allocation failed"));
    }
    for (usize index = 0; index < instances.Size(); ++index)
    {
        const InstanceDrawInput& instance = instances[index];
        if (instance.meshId == 0 || instance.indexCount == 0 || instance.indexCount % 3 != 0 ||
            instance.indexCount > std::numeric_limits<u32>::max() - instance.firstIndex ||
            !IsFinite(instance.transform) || !IsAffine(instance.transform))
        {
            return Result<InstanceBatchList, Error>::Err(
                MakeError(ErrorCode::InvalidArgument, "instance draw input is invalid"));
        }
        if (!sorted.TryPushBack(instance))
        {
            return Result<InstanceBatchList, Error>::Err(
                MakeError(ErrorCode::Internal, "instance sort allocation failed"));
        }
    }
    if (sorted.Size() > 1)
    {
        std::sort(sorted.Data(), sorted.Data() + sorted.Size(),
                  [](const InstanceDrawInput& lhs, const InstanceDrawInput& rhs) noexcept
                  {
                      if (lhs.meshId != rhs.meshId)
                      {
                          return lhs.meshId < rhs.meshId;
                      }
                      if (lhs.materialId != rhs.materialId)
                      {
                          return lhs.materialId < rhs.materialId;
                      }
                      if (lhs.lod != rhs.lod)
                      {
                          return lhs.lod < rhs.lod;
                      }
                      if (lhs.indexCount != rhs.indexCount)
                      {
                          return lhs.indexCount < rhs.indexCount;
                      }
                      if (lhs.firstIndex != rhs.firstIndex)
                      {
                          return lhs.firstIndex < rhs.firstIndex;
                      }
                      if (lhs.vertexOffset != rhs.vertexOffset)
                      {
                          return lhs.vertexOffset < rhs.vertexOffset;
                      }
                      for (usize column = 0; column < 4; ++column)
                      {
                          for (usize row = 0; row < 4; ++row)
                          {
                              if (lhs.transform[column][row] != rhs.transform[column][row])
                              {
                                  return lhs.transform[column][row] < rhs.transform[column][row];
                              }
                          }
                      }
                      return false;
                  });
    }

    InstanceBatchList output(allocator);
    if (!output.m_batches.TryReserve(instances.Size()) ||
        !output.m_transforms.TryReserve(instances.Size()))
    {
        return Result<InstanceBatchList, Error>::Err(
            MakeError(ErrorCode::Internal, "instance batch allocation failed"));
    }
    for (usize index = 0; index < sorted.Size(); ++index)
    {
        const InstanceDrawInput& instance = sorted[index];
        const bool sameBatch =
            output.m_batches.Size() > 0 && output.m_batches.Back().meshId == instance.meshId &&
            output.m_batches.Back().materialId == instance.materialId &&
            output.m_batches.Back().lod == instance.lod &&
            output.m_batches.Back().arguments.indexCount == instance.indexCount &&
            output.m_batches.Back().arguments.firstIndex == instance.firstIndex &&
            output.m_batches.Back().arguments.vertexOffset == instance.vertexOffset;
        if (!sameBatch)
        {
            InstanceBatch batch{};
            batch.meshId = instance.meshId;
            batch.materialId = instance.materialId;
            batch.lod = instance.lod;
            batch.arguments.indexCount = instance.indexCount;
            batch.arguments.firstIndex = instance.firstIndex;
            batch.arguments.vertexOffset = instance.vertexOffset;
            batch.arguments.firstInstance = static_cast<u32>(output.m_transforms.Size());
            if (!output.m_batches.TryPushBack(batch))
            {
                return Result<InstanceBatchList, Error>::Err(
                    MakeError(ErrorCode::Internal, "instance batch allocation failed"));
            }
        }
        if (output.m_batches.Back().arguments.instanceCount == std::numeric_limits<u32>::max() ||
            !output.m_transforms.TryPushBack(instance.transform))
        {
            return Result<InstanceBatchList, Error>::Err(
                MakeError(ErrorCode::OutOfRange, "instance batch capacity exceeded"));
        }
        ++output.m_batches.Back().arguments.instanceCount;
    }
    return Result<InstanceBatchList, Error>::Ok(std::move(output));
}

}  // namespace cyber::scene
