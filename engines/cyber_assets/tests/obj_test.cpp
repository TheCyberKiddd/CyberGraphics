#include <cyber/assets/obj.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/test/test.h>

#include <cmath>
#include <cstring>
#include <string>

namespace
{

using cyber::assets::AssetImportSettings;
using cyber::assets::AssetKind;
using cyber::assets::ImportObj;
using cyber::assets::ObjImport;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::memory::HeapAllocator;
using cyber::memory::StringView;

[[nodiscard]] StringView AsBytes(const char* text) noexcept
{
    return StringView(text);
}

[[nodiscard]] cyber::memory::Span<const Byte> AsSpan(StringView text) noexcept
{
    return cyber::memory::Span<const Byte>(reinterpret_cast<const Byte*>(text.Data()), text.Size());
}

CG_TEST(obj_imports_quad_with_groups_and_materials)
{
    HeapAllocator allocator;
    const StringView text = AsBytes("mtllib quad.mtl\n"
                                    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                                    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                    "vn 0 0 1\n"
                                    "g front\nusemtl checker\n"
                                    "f 1/1/1 2/2/1 3/3/1 4/4/1\n");

    auto imported = ImportObj(AsSpan(text), allocator);
    CG_REQUIRE(imported.IsOk());
    ObjImport& result = imported.Value();

    CG_EXPECT_EQ(result.mesh.vertices.Size(), static_cast<cyber::core::usize>(4));
    // A quad fans into two triangles.
    CG_EXPECT_EQ(result.mesh.indices.Size(), static_cast<cyber::core::usize>(6));
    CG_EXPECT_EQ(result.mesh.indices[0], static_cast<cyber::core::u32>(0));
    CG_EXPECT_EQ(result.mesh.indices[1], static_cast<cyber::core::u32>(1));
    CG_EXPECT_EQ(result.mesh.indices[2], static_cast<cyber::core::u32>(2));
    CG_EXPECT_EQ(result.mesh.indices[3], static_cast<cyber::core::u32>(0));
    CG_EXPECT_EQ(result.mesh.indices[4], static_cast<cyber::core::u32>(2));
    CG_EXPECT_EQ(result.mesh.indices[5], static_cast<cyber::core::u32>(3));

    CG_REQUIRE(result.mesh.submeshes.Size() == 1);
    CG_EXPECT_EQ(result.mesh.submeshes[0].firstIndex, static_cast<cyber::core::u32>(0));
    CG_EXPECT_EQ(result.mesh.submeshes[0].indexCount, static_cast<cyber::core::u32>(6));
    CG_EXPECT_EQ(result.mesh.submeshes[0].materialIndex, static_cast<cyber::core::u32>(0));

    CG_REQUIRE(result.materialNames.Size() == 1);
    CG_EXPECT_EQ(result.materialNames[0].View(), StringView("checker"));
    CG_REQUIRE(result.dependencies.Size() == 1);
    CG_EXPECT_EQ(result.dependencies[0].View(), StringView("quad.mtl"));

    CG_EXPECT_TRUE(result.mesh.Validate().IsOk());
    CG_EXPECT_NEAR(result.mesh.vertices[2].position.x, 1.0f, 1e-5f);
    CG_EXPECT_NEAR(result.mesh.vertices[2].position.y, 1.0f, 1e-5f);
    CG_EXPECT_NEAR(result.mesh.vertices[2].texCoord0.y, 1.0f, 1e-5f);
    CG_EXPECT_NEAR(result.mesh.bounds.Max().y, 1.0f, 1e-5f);
}

CG_TEST(obj_negative_indices_and_multiple_groups)
{
    HeapAllocator allocator;
    const StringView text = AsBytes("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                                    "v 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
                                    "o cube\n"
                                    "usemtl front\nf 1 2 3 4\n"
                                    "usemtl back\nf -4 -3 -2 -1\n");

    auto imported = ImportObj(AsSpan(text), allocator);
    CG_REQUIRE(imported.IsOk());
    ObjImport& result = imported.Value();

    CG_EXPECT_EQ(result.mesh.vertices.Size(), static_cast<cyber::core::usize>(8));
    CG_EXPECT_EQ(result.mesh.indices.Size(), static_cast<cyber::core::usize>(12));
    CG_EXPECT_EQ(result.mesh.submeshes.Size(), static_cast<cyber::core::usize>(2));
    CG_EXPECT_EQ(result.mesh.submeshes[1].firstIndex, static_cast<cyber::core::u32>(6));
    CG_EXPECT_EQ(result.mesh.submeshes[1].indexCount, static_cast<cyber::core::u32>(6));
    CG_EXPECT_EQ(result.materialNames.Size(), static_cast<cyber::core::usize>(2));
    CG_EXPECT_EQ(result.materialNames[1].View(), StringView("back"));
    // Negative indices are relative to the eight vertices declared so far, so
    // -4 resolves to position 4 rather than reusing the front quad.
    CG_EXPECT_EQ(result.mesh.indices[6], static_cast<cyber::core::u32>(4));
    CG_EXPECT_EQ(result.mesh.indices[8], static_cast<cyber::core::u32>(6));
    CG_EXPECT_TRUE(result.dependencies.Size() == 0);
    CG_EXPECT_TRUE(result.mesh.Validate().IsOk());
}

CG_TEST(obj_corner_map_grows_for_large_meshes)
{
    HeapAllocator allocator;
    constexpr cyber::core::u32 kTriangleCount = 100;
    std::string text;
    for (cyber::core::u32 index = 0; index < kTriangleCount * 3; ++index)
    {
        text.append("v ");
        text.append(std::to_string(index));
        text.append(" 0 0\n");
    }
    for (cyber::core::u32 index = 0; index < kTriangleCount; ++index)
    {
        const cyber::core::u32 first = index * 3 + 1;
        text.append("f ");
        text.append(std::to_string(first));
        text.push_back(' ');
        text.append(std::to_string(first + 1));
        text.push_back(' ');
        text.append(std::to_string(first + 2));
        text.push_back('\n');
    }

    auto imported = ImportObj(AsSpan(StringView(text.data(), text.size())), allocator);
    CG_REQUIRE(imported.IsOk());
    CG_EXPECT_EQ(imported.Value().mesh.vertices.Size(),
                 static_cast<cyber::core::usize>(kTriangleCount * 3));
    CG_EXPECT_EQ(imported.Value().mesh.indices.Size(),
                 static_cast<cyber::core::usize>(kTriangleCount * 3));
}

CG_TEST(obj_reports_bad_face_index_with_location)
{
    HeapAllocator allocator;
    const StringView text = AsBytes("v 0 0 0\nv 1 0 0\n\nf 1 2 9\n");

    auto imported = ImportObj(AsSpan(text), allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::ParseError);
    CG_EXPECT_EQ(imported.Error().line, static_cast<cyber::core::u32>(4));
    CG_EXPECT_EQ(imported.Error().column, static_cast<cyber::core::u32>(1));
    CG_EXPECT_TRUE(imported.Error().message != nullptr);
}

CG_TEST(obj_rejects_out_of_range_negative_index)
{
    HeapAllocator allocator;
    const StringView text = AsBytes("v 0 0 0\nf -2 -1 0\n");

    auto imported = ImportObj(AsSpan(text), allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::ParseError);
}

CG_TEST(obj_applies_flip_texcoord_v_setting)
{
    HeapAllocator allocatorA;
    HeapAllocator allocatorB;

    const StringView text = AsBytes("v 0 0 0\nv 1 0 0\nv 1 1 0\n"
                                    "vt 0 0.25\nvt 1 0.25\nvt 1 0.75\n"
                                    "vn 0 0 1\n"
                                    "f 1/1/1 2/2/1 3/3/1\n");

    AssetImportSettings flipped{};
    flipped.flipTexCoordV = true;

    auto plain = ImportObj(AsSpan(text), allocatorA);
    auto mirrored = ImportObj(AsSpan(text), allocatorB, flipped);
    CG_REQUIRE(plain.IsOk());
    CG_REQUIRE(mirrored.IsOk());

    CG_EXPECT_NEAR(plain.Value().mesh.vertices[0].texCoord0.y, 0.25f, 1e-5f);
    CG_EXPECT_NEAR(mirrored.Value().mesh.vertices[0].texCoord0.y, 0.75f, 1e-5f);
}

CG_TEST(obj_rejects_file_without_geometry)
{
    HeapAllocator allocator;
    const StringView text = AsBytes("# only a comment\n\n");

    auto imported = ImportObj(AsSpan(text), allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::ParseError);
    CG_EXPECT_TRUE(cyber::assets::SourceFormatFromPath(StringView("models/quad.obj")) ==
                   cyber::assets::SourceFormat::Obj);
    CG_EXPECT_TRUE(static_cast<int>(AssetKind::Mesh) == 1);
}

}  // namespace
