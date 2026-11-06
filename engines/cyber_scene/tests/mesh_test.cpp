#include <cyber/math/math.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/scene/mesh.h>
#include <cyber/test/test.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

using cyber::core::ErrorCode;
using cyber::core::u16;
using cyber::core::u32;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::scene::BuildInstanceBatches;
using cyber::scene::DrawIndexedIndirectArguments;
using cyber::scene::IndexFormat;
using cyber::scene::InstanceDrawInput;
using cyber::scene::Mesh;
using cyber::scene::MeshDesc;
using cyber::scene::PackIndices;
using cyber::scene::PackVertices;
using cyber::scene::SelectIndexFormat;
using cyber::scene::SimplifyMesh;
using cyber::scene::SubmeshDesc;
using cyber::scene::Vertex;

namespace
{

[[nodiscard]] std::array<Vertex, 4> MakeQuadVertices() noexcept
{
    std::array<Vertex, 4> vertices{};
    vertices[0].position = {-1.0f, -1.0f, 0.0f};
    vertices[1].position = {1.0f, -1.0f, 0.0f};
    vertices[2].position = {1.0f, 1.0f, 0.0f};
    vertices[3].position = {-1.0f, 1.0f, 0.0f};
    vertices[0].texCoord0 = {0.0f, 0.0f};
    vertices[1].texCoord0 = {1.0f, 0.0f};
    vertices[2].texCoord0 = {1.0f, 1.0f};
    vertices[3].texCoord0 = {0.0f, 1.0f};
    return vertices;
}

[[nodiscard]] cyber::core::Result<Mesh, cyber::core::Error>
MakeQuad(HeapAllocator& allocator, bool mirroredUv = false) noexcept
{
    auto vertices = MakeQuadVertices();
    if (mirroredUv)
    {
        for (auto& vertex : vertices)
        {
            vertex.texCoord0.x = 1.0f - vertex.texCoord0.x;
        }
    }
    const u32 indices[] = {0, 1, 2, 0, 2, 3};
    const SubmeshDesc submeshes[] = {{0, 6, 7}};
    return Mesh::Create(MeshDesc{{vertices.data(), vertices.size()}, indices, submeshes},
                        allocator);
}

}  // namespace

CG_TEST(scene_mesh_owns_validated_data_and_computes_submesh_bounds)
{
    HeapAllocator allocator;
    auto vertices = MakeQuadVertices();
    const u32 indices[] = {0, 1, 2, 0, 2, 3};
    const SubmeshDesc submeshes[] = {{0, 6, 42}};
    auto result =
        Mesh::Create(MeshDesc{{vertices.data(), vertices.size()}, indices, submeshes}, allocator);
    CG_REQUIRE(result);
    Mesh mesh = std::move(result.Value());

    vertices[0].position = {100.0f, 100.0f, 100.0f};
    CG_EXPECT_EQ(mesh.Vertices()[0].position.x, -1.0f);
    CG_EXPECT_EQ(mesh.TriangleCount(), cyber::memory::usize{2});
    CG_EXPECT_EQ(mesh.Submeshes()[0].materialId, u32{42});
    CG_EXPECT_TRUE(mesh.Bounds().Contains({-1.0f, -1.0f, 0.0f}));
    CG_EXPECT_TRUE(mesh.Submeshes()[0].bounds.Contains({1.0f, 1.0f, 0.0f}));

    const auto quadVertices = MakeQuadVertices();
    std::array<Vertex, 5> verticesWithUnreferenced{};
    for (cyber::memory::usize index = 0; index < quadVertices.size(); ++index)
    {
        verticesWithUnreferenced[index] = quadVertices[index];
    }
    verticesWithUnreferenced[4].position = {3.0f, 4.0f, 5.0f};
    const auto unreferencedVertexMesh = Mesh::Create(
        MeshDesc{
            {verticesWithUnreferenced.data(), verticesWithUnreferenced.size()}, indices, submeshes},
        allocator);
    CG_REQUIRE(unreferencedVertexMesh);
    CG_EXPECT_TRUE(unreferencedVertexMesh.Value().Bounds().Contains({3.0f, 4.0f, 5.0f}));
    CG_EXPECT_FALSE(
        unreferencedVertexMesh.Value().Submeshes()[0].bounds.Contains({3.0f, 4.0f, 5.0f}));

    const u32 invalidIndices[] = {0, 1, 4};
    const SubmeshDesc invalidSubmesh[] = {{0, 3, 0}};
    const auto nullVertices = Mesh::Create(MeshDesc{{nullptr, 4}, indices, submeshes}, allocator);
    CG_EXPECT_FALSE(nullVertices);
    CG_EXPECT_EQ(nullVertices.Error().code, ErrorCode::InvalidArgument);
    const auto nullIndices = Mesh::Create(
        MeshDesc{{vertices.data(), vertices.size()}, {nullptr, 6}, submeshes}, allocator);
    CG_EXPECT_FALSE(nullIndices);
    CG_EXPECT_EQ(nullIndices.Error().code, ErrorCode::InvalidArgument);
    const auto nullSubmeshes = Mesh::Create(
        MeshDesc{{vertices.data(), vertices.size()}, indices, {nullptr, 1}}, allocator);
    CG_EXPECT_FALSE(nullSubmeshes);
    CG_EXPECT_EQ(nullSubmeshes.Error().code, ErrorCode::InvalidArgument);

    const auto invalidIndex =
        Mesh::Create(MeshDesc{{vertices.data(), 3}, invalidIndices, invalidSubmesh}, allocator);
    CG_EXPECT_FALSE(invalidIndex);
    CG_EXPECT_EQ(invalidIndex.Error().code, ErrorCode::InvalidArgument);

    const u32 incompleteIndices[] = {0, 1, 2, 0, 2, 3};
    const SubmeshDesc incompleteSubmesh[] = {{0, 3, 0}};
    auto incompleteVertices = MakeQuadVertices();
    const auto incomplete =
        Mesh::Create(MeshDesc{{incompleteVertices.data(), incompleteVertices.size()},
                              incompleteIndices,
                              incompleteSubmesh},
                     allocator);
    CG_EXPECT_FALSE(incomplete);
    CG_EXPECT_EQ(incomplete.Error().code, ErrorCode::InvalidArgument);

    auto invalidVertices = MakeQuadVertices();
    invalidVertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
    const auto nonFinite = Mesh::Create(
        MeshDesc{{invalidVertices.data(), invalidVertices.size()}, indices, submeshes}, allocator);
    CG_EXPECT_FALSE(nonFinite);
    CG_EXPECT_EQ(nonFinite.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(scene_mesh_tangents_are_orthogonal_and_preserve_mirrored_handedness)
{
    HeapAllocator allocator;
    auto regularResult = MakeQuad(allocator);
    auto mirroredResult = MakeQuad(allocator, true);
    CG_REQUIRE(regularResult);
    CG_REQUIRE(mirroredResult);
    Mesh regular = std::move(regularResult.Value());
    Mesh mirrored = std::move(mirroredResult.Value());

    CG_EXPECT_TRUE(cyber::scene::GenerateTangents(regular));
    CG_EXPECT_TRUE(cyber::scene::GenerateTangents(mirrored));
    const Span<const Vertex> regularVertices = regular.Vertices();
    for (cyber::memory::usize index = 0; index < regularVertices.Size(); ++index)
    {
        const Vertex& vertex = regularVertices[index];
        CG_EXPECT_NEAR(vertex.tangent.x, 1.0f, 1.0e-5f);
        CG_EXPECT_NEAR(
            cyber::math::Dot(vertex.normal, {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z}),
            0.0f, 1.0e-5f);
        CG_EXPECT_NEAR(vertex.tangent.w, 1.0f, 1.0e-5f);
    }
    CG_EXPECT_NEAR(mirrored.Vertices()[0].tangent.w, -1.0f, 1.0e-5f);

    auto invalidVertices = MakeQuadVertices();
    invalidVertices[0].normal = cyber::math::Vec3::Zero();
    const u32 indices[] = {0, 1, 2, 0, 2, 3};
    const SubmeshDesc submeshes[] = {{0, 6, 7}};
    auto invalidResult = Mesh::Create(
        MeshDesc{{invalidVertices.data(), invalidVertices.size()}, indices, submeshes}, allocator);
    CG_REQUIRE(invalidResult);
    Mesh invalid = std::move(invalidResult.Value());
    const auto invalidTangents = cyber::scene::GenerateTangents(invalid);
    CG_EXPECT_FALSE(invalidTangents);
    CG_EXPECT_EQ(invalidTangents.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(scene_mesh_packing_selects_compact_indices_and_validates_attributes)
{
    HeapAllocator allocator;
    auto meshResult = MakeQuad(allocator);
    CG_REQUIRE(meshResult);
    Mesh mesh = std::move(meshResult.Value());

    CG_EXPECT_EQ(SelectIndexFormat(65536), IndexFormat::UInt16);
    CG_EXPECT_EQ(SelectIndexFormat(65537), IndexFormat::UInt32);
    Byte packedIndices[12]{};
    IndexFormat format = IndexFormat::UInt32;
    const auto packed = PackIndices(mesh.Indices(), mesh.Vertices().Size(), packedIndices, format);
    CG_REQUIRE(packed);
    CG_EXPECT_EQ(packed.Value(), cyber::memory::usize{12});
    CG_EXPECT_EQ(format, IndexFormat::UInt16);
    u16 firstIndex = 99;
    std::memcpy(&firstIndex, packedIndices, sizeof(firstIndex));
    CG_EXPECT_EQ(firstIndex, u16{0});

    const u32 tooWide[] = {65536};
    const auto rejected = PackIndices(tooWide, 65537, packedIndices, format);
    CG_REQUIRE(rejected);
    CG_EXPECT_EQ(format, IndexFormat::UInt32);
    CG_EXPECT_EQ(rejected.Value(), cyber::memory::usize{4});
    const auto tooSmall =
        PackIndices(mesh.Indices(), mesh.Vertices().Size(), Span<Byte>{packedIndices, 3}, format);
    CG_EXPECT_FALSE(tooSmall);
    CG_EXPECT_EQ(tooSmall.Error().code, ErrorCode::OutOfRange);

    cyber::scene::PackedVertex packedVertices[4]{};
    CG_EXPECT_TRUE(PackVertices(mesh.Vertices(), packedVertices));
    CG_EXPECT_EQ(packedVertices[0].color[0], cyber::core::u8{255});
    CG_EXPECT_EQ(packedVertices[0].weights[0], u16{65535});
    CG_EXPECT_EQ(packedVertices[1].texCoord0[0], u16{0x3c00});
    CG_EXPECT_EQ(static_cast<u32>(packedVertices[0].weights[0]) + packedVertices[0].weights[1] +
                     packedVertices[0].weights[2] + packedVertices[0].weights[3],
                 u32{65535});
    const auto insufficient = PackVertices(mesh.Vertices(), Span<cyber::scene::PackedVertex>{});
    CG_EXPECT_FALSE(insufficient);
    CG_EXPECT_EQ(insufficient.Error().code, ErrorCode::OutOfRange);
    const auto nullInput = PackVertices(Span<const Vertex>{nullptr, 4}, packedVertices);
    CG_EXPECT_FALSE(nullInput);
    CG_EXPECT_EQ(nullInput.Error().code, ErrorCode::InvalidArgument);
    const auto nullOutput = PackVertices(
        mesh.Vertices(), Span<cyber::scene::PackedVertex>{nullptr, mesh.Vertices().Size()});
    CG_EXPECT_FALSE(nullOutput);
    CG_EXPECT_EQ(nullOutput.Error().code, ErrorCode::InvalidArgument);
    const auto nullIndices = PackIndices(Span<const u32>{nullptr, 3}, 4, packedIndices, format);
    CG_EXPECT_FALSE(nullIndices);
    CG_EXPECT_EQ(nullIndices.Error().code, ErrorCode::InvalidArgument);
    const auto nullIndexOutput =
        PackIndices(mesh.Indices(), mesh.Vertices().Size(), Span<Byte>{nullptr, 12}, format);
    CG_EXPECT_FALSE(nullIndexOutput);
    CG_EXPECT_EQ(nullIndexOutput.Error().code, ErrorCode::InvalidArgument);

    auto badColor = MakeQuadVertices();
    badColor[0].color.x = 2.0f;
    const SubmeshDesc submesh[] = {{0, 6, 7}};
    const auto badColorMesh = Mesh::Create(
        MeshDesc{{badColor.data(), badColor.size()}, mesh.Indices(), submesh}, allocator);
    CG_REQUIRE(badColorMesh);
    const auto badColorPacking = PackVertices(badColorMesh.Value().Vertices(), packedVertices);
    CG_EXPECT_FALSE(badColorPacking);
    CG_EXPECT_EQ(badColorPacking.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(scene_mesh_simplification_reduces_triangles_and_reports_error)
{
    HeapAllocator allocator;
    auto sourceResult = MakeQuad(allocator);
    CG_REQUIRE(sourceResult);
    Mesh source = std::move(sourceResult.Value());
    auto simplifiedResult = SimplifyMesh(source, 1, allocator);
    CG_REQUIRE(simplifiedResult);
    const auto& simplified = simplifiedResult.Value();
    CG_EXPECT_EQ(simplified.mesh.TriangleCount(), cyber::memory::usize{1});
    CG_EXPECT_NEAR(simplified.maximumVertexError, 1.0f, 1.0e-5f);
    CG_EXPECT_EQ(simplified.mesh.Submeshes()[0].materialId, u32{7});
    CG_EXPECT_FALSE(SimplifyMesh(source, 0, allocator));

    auto vertices = MakeQuadVertices();
    std::array<Vertex, 8> twoMaterials{};
    for (cyber::memory::usize index = 0; index < 4; ++index)
    {
        twoMaterials[index] = vertices[index];
        twoMaterials[index + 4] = vertices[index];
        twoMaterials[index + 4].position.x += 4.0f;
    }
    const u32 indices[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    const SubmeshDesc submeshes[] = {{0, 6, 11}, {6, 6, 22}};
    auto twoMaterialSource = Mesh::Create(
        MeshDesc{{twoMaterials.data(), twoMaterials.size()}, indices, submeshes}, allocator);
    CG_REQUIRE(twoMaterialSource);
    auto twoMaterialLod = SimplifyMesh(twoMaterialSource.Value(), 2, allocator);
    CG_REQUIRE(twoMaterialLod);
    CG_EXPECT_EQ(twoMaterialLod.Value().mesh.TriangleCount(), cyber::memory::usize{2});
    CG_EXPECT_EQ(twoMaterialLod.Value().mesh.Submeshes()[0].materialId, u32{11});
    CG_EXPECT_EQ(twoMaterialLod.Value().mesh.Submeshes()[1].materialId, u32{22});
}

CG_TEST(scene_instance_batches_are_sorted_and_emit_indexed_indirect_arguments)
{
    HeapAllocator allocator;
    const InstanceDrawInput instances[] = {
        {2, 4, 1, 6, 3, 0, cyber::math::Mat4::Translation({3.0f, 0.0f, 0.0f})},
        {1, 7, 0, 3, 0, 0, cyber::math::Mat4::Translation({2.0f, 0.0f, 0.0f})},
        {1, 7, 0, 3, 0, 0, cyber::math::Mat4::Translation({1.0f, 0.0f, 0.0f})},
        {1, 8, 0, 3, 0, 0, cyber::math::Mat4::Identity()},
    };
    auto result = BuildInstanceBatches(Span<const InstanceDrawInput>{instances}, allocator);
    CG_REQUIRE(result);
    const auto& batches = result.Value().Batches();
    CG_EXPECT_EQ(batches.Size(), cyber::memory::usize{3});
    CG_EXPECT_EQ(batches[0].meshId, cyber::core::u64{1});
    CG_EXPECT_EQ(batches[0].arguments.instanceCount, u32{2});
    CG_EXPECT_EQ(batches[0].arguments.indexCount, u32{3});
    CG_EXPECT_EQ(batches[0].arguments.firstInstance, u32{0});
    CG_EXPECT_EQ(result.Value().Transforms()[0][3].x, 1.0f);
    static_assert(sizeof(DrawIndexedIndirectArguments) == 20);
    static_assert(!std::is_copy_constructible_v<cyber::scene::InstanceBatchList>);

    auto invalid = instances[0];
    invalid.meshId = 0;
    const auto rejected =
        BuildInstanceBatches(Span<const InstanceDrawInput>{&invalid, 1}, allocator);
    CG_EXPECT_FALSE(rejected);
    CG_EXPECT_EQ(rejected.Error().code, ErrorCode::InvalidArgument);
    const auto nullInstances =
        BuildInstanceBatches(Span<const InstanceDrawInput>{nullptr, 1}, allocator);
    CG_EXPECT_FALSE(nullInstances);
    CG_EXPECT_EQ(nullInstances.Error().code, ErrorCode::InvalidArgument);
}

CG_TEST(scene_instance_batches_cover_one_million_triangles_without_per_instance_draws)
{
    HeapAllocator allocator;
    constexpr u32 kInstanceCount = 10000;
    cyber::memory::DynamicArray<InstanceDrawInput> instances(allocator);
    CG_REQUIRE(instances.TryResize(kInstanceCount));
    for (u32 index = 0; index < kInstanceCount; ++index)
    {
        instances[index] = InstanceDrawInput{
            1,
            1,
            index < kInstanceCount / 2 ? 0U : 1U,
            300,
            0,
            0,
            cyber::math::Mat4::Translation({static_cast<float>(index), 0.0f, 0.0f})};
    }
    auto result = BuildInstanceBatches(
        Span<const InstanceDrawInput>{instances.Data(), instances.Size()}, allocator);
    CG_REQUIRE(result);
    CG_EXPECT_EQ(result.Value().Batches().Size(), cyber::memory::usize{2});
    CG_EXPECT_EQ(result.Value().Batches()[0].arguments.indexCount, u32{300});
    CG_EXPECT_EQ(result.Value().Batches()[0].arguments.instanceCount, u32{5000});
    CG_EXPECT_EQ(result.Value().Batches()[1].arguments.instanceCount, u32{5000});
    CG_EXPECT_EQ(result.Value().Batches()[1].arguments.firstInstance, u32{5000});
    CG_EXPECT_EQ(result.Value().Transforms().Size(), cyber::memory::usize{kInstanceCount});
}
