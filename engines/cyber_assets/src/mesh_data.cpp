#include <cyber/assets/mesh_data.h>

#include <cmath>

namespace cyber::assets
{
namespace
{

using cyber::core::ErrorCode;
using cyber::core::usize;
using cyber::math::Aabb;
using cyber::math::Vec2;
using cyber::math::Vec3;

[[nodiscard]] bool IsFinite(const Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFinite(const Vec2& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFinite(const PackedFloat4& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::isfinite(value.w);
}

[[nodiscard]] bool IsValidVertex(const MeshVertex& vertex) noexcept
{
    if (!IsFinite(vertex.position) || !IsFinite(vertex.normal) || !IsFinite(vertex.tangent) ||
        !IsFinite(vertex.texCoord0) || !IsFinite(vertex.texCoord1) || !IsFinite(vertex.color))
    {
        return false;
    }
    f32 weightSum = 0.0f;
    for (f32 weight : vertex.weights)
    {
        if (!std::isfinite(weight) || weight < 0.0f)
        {
            return false;
        }
        weightSum += weight;
    }
    return std::isfinite(weightSum) && weightSum > 0.0f;
}

}  // namespace

void MeshAsset::RecomputeBounds() noexcept
{
    bounds = Aabb::Empty();
    for (usize index = 0; index < indices.Size(); ++index)
    {
        bounds.Expand(vertices[indices[index]].position);
    }
    for (usize submeshIndex = 0; submeshIndex < submeshes.Size(); ++submeshIndex)
    {
        SubmeshAsset& submesh = submeshes[submeshIndex];
        Aabb submeshBounds = Aabb::Empty();
        const u32 end = submesh.firstIndex + submesh.indexCount;
        for (u32 index = submesh.firstIndex; index < end; ++index)
        {
            submeshBounds.Expand(vertices[indices[index]].position);
        }
        submesh.bounds = submeshBounds;
    }
}

ImportStatus MeshAsset::Check() const noexcept
{
    if (vertices.Empty())
    {
        return ImportStatus::Err(MakeError(ErrorCode::InvalidArgument, "mesh has no vertices"));
    }
    if (indices.Empty() || indices.Size() % 3 != 0)
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "mesh index count is not a triangle list"));
    }
    if (submeshes.Empty())
    {
        return ImportStatus::Err(MakeError(ErrorCode::InvalidArgument, "mesh has no submeshes"));
    }
    if (vertices.Size() > 0xffffffffull)
    {
        return ImportStatus::Err(MakeError(ErrorCode::Overflow, "mesh has too many vertices"));
    }

    for (usize index = 0; index < vertices.Size(); ++index)
    {
        if (!IsValidVertex(vertices[index]))
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "mesh vertex data is not finite"));
        }
    }

    u32 expectedFirst = 0;
    for (usize index = 0; index < submeshes.Size(); ++index)
    {
        const SubmeshAsset& submesh = submeshes[index];
        if (submesh.indexCount % 3 != 0 || submesh.indexCount == 0)
        {
            return ImportStatus::Err(MakeError(ErrorCode::InvalidArgument,
                                               "submesh index count is not a triangle list"));
        }
        if (submesh.firstIndex != expectedFirst)
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::InvalidArgument, "submeshes do not tile the index array"));
        }
        if (submesh.indexCount > indices.Size() - expectedFirst)
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::OutOfRange, "submesh index range exceeds the index array"));
        }
        expectedFirst += submesh.indexCount;
    }
    if (expectedFirst != indices.Size())
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "submeshes do not cover the index array"));
    }

    for (usize index = 0; index < indices.Size(); ++index)
    {
        if (indices[index] >= vertices.Size())
        {
            return ImportStatus::Err(
                MakeError(ErrorCode::OutOfRange, "mesh index references a missing vertex"));
        }
    }

    return ImportStatus::Ok();
}

ImportStatus MeshAsset::Validate() noexcept
{
    auto checked = Check();
    if (checked.IsError())
    {
        return checked;
    }
    RecomputeBounds();
    if (bounds.IsEmpty())
    {
        return ImportStatus::Err(MakeError(ErrorCode::InvalidArgument, "mesh bounds are empty"));
    }
    return ImportStatus::Ok();
}

ImportStatus GenerateTangents(MeshAsset& mesh, Allocator& scratch) noexcept
{
    const usize vertexCount = mesh.vertices.Size();
    if (vertexCount == 0 || mesh.indices.Empty())
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::InvalidArgument, "tangent generation needs a non-empty mesh"));
    }

    DynamicArray<PackedFloat4> accumulatedTangent(scratch);
    DynamicArray<PackedFloat4> accumulatedBitangent(scratch);
    if (!accumulatedTangent.TryResize(vertexCount) || !accumulatedBitangent.TryResize(vertexCount))
    {
        return ImportStatus::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while generating tangents"));
    }
    for (usize index = 0; index < vertexCount; ++index)
    {
        accumulatedTangent[index] = PackedFloat4(0.0f, 0.0f, 0.0f, 0.0f);
        accumulatedBitangent[index] = PackedFloat4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    for (usize triangle = 0; triangle + 2 < mesh.indices.Size(); triangle += 3)
    {
        const u32 i0 = mesh.indices[triangle];
        const u32 i1 = mesh.indices[triangle + 1];
        const u32 i2 = mesh.indices[triangle + 2];
        const MeshVertex& v0 = mesh.vertices[i0];
        const MeshVertex& v1 = mesh.vertices[i1];
        const MeshVertex& v2 = mesh.vertices[i2];

        const Vec3 edge1 = v1.position - v0.position;
        const Vec3 edge2 = v2.position - v0.position;
        const f32 du1 = v1.texCoord0.x - v0.texCoord0.x;
        const f32 dv1 = v1.texCoord0.y - v0.texCoord0.y;
        const f32 du2 = v2.texCoord0.x - v0.texCoord0.x;
        const f32 dv2 = v2.texCoord0.y - v0.texCoord0.y;
        const f32 determinant = du1 * dv2 - du2 * dv1;
        if (std::fabs(determinant) < 1.0e-12f)
        {
            continue;
        }
        const f32 reciprocal = 1.0f / determinant;
        const Vec3 tangent = (edge1 * dv2 - edge2 * dv1) * reciprocal;
        const Vec3 bitangent = (edge2 * du1 - edge1 * du2) * reciprocal;

        const u32 corners[3] = {i0, i1, i2};
        for (u32 corner : corners)
        {
            accumulatedTangent[corner].x += tangent.x;
            accumulatedTangent[corner].y += tangent.y;
            accumulatedTangent[corner].z += tangent.z;
            accumulatedBitangent[corner].x += bitangent.x;
            accumulatedBitangent[corner].y += bitangent.y;
            accumulatedBitangent[corner].z += bitangent.z;
        }
    }

    for (usize index = 0; index < vertexCount; ++index)
    {
        MeshVertex& vertex = mesh.vertices[index];
        const Vec3 normal = vertex.normal;
        Vec3 tangent(accumulatedTangent[index].x, accumulatedTangent[index].y,
                     accumulatedTangent[index].z);
        const Vec3 bitangent(accumulatedBitangent[index].x, accumulatedBitangent[index].y,
                             accumulatedBitangent[index].z);

        // Gram-Schmidt: remove the normal component, then normalize.
        tangent = tangent - normal * cyber::math::Dot(normal, tangent);
        f32 length = cyber::math::Length(tangent);
        if (!std::isfinite(length) || length < 1.0e-6f)
        {
            // Deterministic fallback: any vector orthogonal to the normal.
            const Vec3 axis =
                std::fabs(normal.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
            tangent = cyber::math::Normalize(cyber::math::Cross(normal, axis));
            length = cyber::math::Length(tangent);
            if (!std::isfinite(length) || length < 1.0e-6f)
            {
                tangent = Vec3(1.0f, 0.0f, 0.0f);
                length = 1.0f;
            }
        }
        tangent = tangent / length;

        const f32 handedness =
            cyber::math::Dot(cyber::math::Cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
        vertex.tangent = PackedFloat4(tangent.x, tangent.y, tangent.z, handedness);
    }
    return ImportStatus::Ok();
}

}  // namespace cyber::assets
