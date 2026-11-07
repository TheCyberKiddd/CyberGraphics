#include <cyber/assets/asset.h>
#include <cyber/assets/gltf.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/test/test.h>

#include <cstring>
#include <string>

namespace
{

using cyber::assets::AssetIdFromPath;
using cyber::assets::AssetImportSettings;
using cyber::assets::GltfImportOptions;
using cyber::assets::ImportedScene;
using cyber::assets::ImportGltf;
using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::f32;
using cyber::core::u32;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;

[[nodiscard]] std::string FixturePath(const char* name)
{
    return std::string(CYBER_ASSET_FIXTURES) + "/" + name;
}

[[nodiscard]] Span<const Byte> AsBytes(StringView text) noexcept
{
    return Span<const Byte>(reinterpret_cast<const Byte*>(text.Data()), text.Size());
}

/// Test-only byte sink. A failed push leaves the buffer short, which fails the
/// subsequent GLB length checks rather than silently building a broken file.
void AppendByte(DynamicArray<Byte>& out, Byte value)
{
    (void)out.TryPushBack(value);
}

void AppendU32(DynamicArray<Byte>& out, u32 value)
{
    AppendByte(out, static_cast<Byte>(value & 0xffu));
    AppendByte(out, static_cast<Byte>((value >> 8u) & 0xffu));
    AppendByte(out, static_cast<Byte>((value >> 16u) & 0xffu));
    AppendByte(out, static_cast<Byte>((value >> 24u) & 0xffu));
}

void AppendFloat(DynamicArray<Byte>& out, f32 value)
{
    Byte raw[sizeof(f32)] = {};
    std::memcpy(raw, &value, sizeof(f32));
    for (Byte byte : raw)
    {
        AppendByte(out, byte);
    }
}

/// Builds a minimal GLB: one embedded binary buffer, one JSON chunk.
[[nodiscard]] bool BuildGlb(const char* json, const DynamicArray<Byte>& binary,
                            DynamicArray<Byte>& out)
{
    const usize jsonLength = std::strlen(json);
    const usize jsonPadded = (jsonLength + 3u) & ~static_cast<usize>(3u);
    const usize binaryPadded = (binary.Size() + 3u) & ~static_cast<usize>(3u);
    const usize total = 12 + 8 + jsonPadded + (binary.Size() > 0 ? 8 + binaryPadded : 0);

    AppendU32(out, 0x46546c67u);  // "glTF"
    AppendU32(out, 2);
    AppendU32(out, static_cast<u32>(total));

    AppendU32(out, static_cast<u32>(jsonPadded));
    AppendU32(out, 0x4e4f534au);  // "JSON"
    for (usize index = 0; index < jsonPadded; ++index)
    {
        AppendByte(out, index < jsonLength ? static_cast<Byte>(json[index]) : Byte{0x20});
    }

    if (binary.Size() > 0)
    {
        AppendU32(out, static_cast<u32>(binaryPadded));
        AppendU32(out, 0x004e4942u);  // "BIN"
        for (usize index = 0; index < binaryPadded; ++index)
        {
            AppendByte(out, index < binary.Size() ? binary[index] : Byte{0});
        }
    }
    return true;
}

CG_TEST(gltf_imports_two_quads_fixture)
{
    HeapAllocator allocator;
    auto imported = cyber::assets::ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator);
    CG_REQUIRE(imported.IsOk());
    ImportedScene& scene = imported.Value();

    CG_REQUIRE(scene.meshes.Size() == 1);
    const cyber::assets::MeshAsset& mesh = scene.meshes[0];
    CG_EXPECT_EQ(mesh.vertices.Size(), static_cast<usize>(8));
    CG_EXPECT_EQ(mesh.indices.Size(), static_cast<usize>(12));
    CG_REQUIRE(mesh.submeshes.Size() == 2);
    CG_EXPECT_EQ(mesh.submeshes[0].firstIndex, static_cast<u32>(0));
    CG_EXPECT_EQ(mesh.submeshes[0].indexCount, static_cast<u32>(6));
    CG_EXPECT_EQ(mesh.submeshes[0].materialIndex, static_cast<u32>(0));
    CG_EXPECT_EQ(mesh.submeshes[1].firstIndex, static_cast<u32>(6));
    CG_EXPECT_EQ(mesh.submeshes[1].materialIndex, static_cast<u32>(1));
    CG_EXPECT_EQ(scene.TotalTriangleCount(), static_cast<usize>(4));

    // The second quad is wound the other way; its indices are rebased onto the
    // second primitive's vertex block, which starts at index 4.
    CG_EXPECT_EQ(mesh.indices[6], static_cast<u32>(4));
    CG_EXPECT_EQ(mesh.indices[7], static_cast<u32>(6));
    CG_EXPECT_EQ(mesh.indices[8], static_cast<u32>(5));
    CG_EXPECT_NEAR(mesh.vertices[5].position.z, 1.0f, 1e-5f);
    CG_EXPECT_NEAR(mesh.vertices[5].normal.z, -1.0f, 1e-5f);
    CG_EXPECT_NEAR(mesh.bounds.Max().z, 1.0f, 1e-5f);

    CG_REQUIRE(scene.materials.Size() == 2);
    const cyber::assets::MaterialAsset& checker = scene.materials[0];
    CG_EXPECT_EQ(checker.name.View(), StringView("checker"));
    CG_EXPECT_TRUE(checker.doubleSided);
    CG_EXPECT_NEAR(checker.metallicFactor, 0.0f, 1e-5f);
    CG_EXPECT_NEAR(checker.roughnessFactor, 0.8f, 1e-5f);
    CG_EXPECT_EQ(checker.baseColorTexture.image, AssetIdFromPath(StringView("png_rgba.png")));

    const cyber::assets::MaterialAsset& flat = scene.materials[1];
    CG_EXPECT_NEAR(flat.baseColorFactor.x, 0.8f, 1e-5f);
    CG_EXPECT_NEAR(flat.baseColorFactor.y, 0.2f, 1e-5f);
    CG_EXPECT_NEAR(flat.roughnessFactor, 0.4f, 1e-5f);
    CG_EXPECT_FALSE(flat.doubleSided);
    CG_EXPECT_EQ(flat.baseColorTexture.image, static_cast<cyber::assets::AssetId>(0));

    CG_REQUIRE(scene.images.Size() == 1);
    CG_EXPECT_EQ(scene.images[0].id, AssetIdFromPath(StringView("png_rgba.png")));
    CG_EXPECT_EQ(scene.images[0].uri.View(), StringView("png_rgba.png"));
    CG_EXPECT_TRUE(scene.images[0].bytes.Size() > 0);

    CG_REQUIRE(scene.nodes.Size() == 2);
    CG_EXPECT_EQ(scene.nodes[0].name.View(), StringView("left"));
    CG_EXPECT_TRUE(scene.nodes[0].mesh == 0);
    CG_EXPECT_FALSE(scene.nodes[0].hasParent);
    CG_EXPECT_NEAR(scene.nodes[0].local[3].x, -1.5f, 1e-5f);
    CG_EXPECT_EQ(scene.nodes[1].name.View(), StringView("right"));

    CG_REQUIRE(scene.instances.Size() == 2);
    CG_EXPECT_EQ(scene.instances[0].node, static_cast<u32>(0));
    CG_EXPECT_NEAR(scene.instances[0].transform[3].x, -1.5f, 1e-5f);
    CG_EXPECT_NEAR(scene.instances[1].transform[3].x, 1.5f, 1e-5f);
    // A 45° yaw about Y keeps the translation exact and rotates the basis.
    CG_EXPECT_NEAR(scene.instances[1].transform[0].z, -0.70710678f, 1e-4f);
}

CG_TEST(gltf_rejects_document_without_version)
{
    HeapAllocator allocator;
    const StringView text = "{\"asset\":{},\"meshes\":[]}";

    auto imported = ImportGltf(AsBytes(text), "", allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::Unsupported);
    CG_EXPECT_STREQ(imported.Error().message, "glTF asset version is not 2.x");
}

CG_TEST(gltf_rejects_non_triangle_primitive)
{
    HeapAllocator allocator;
    const StringView text = "{\"asset\":{\"version\":\"2.0\"},"
                            "\"meshes\":[{\"primitives\":[{\"attributes\":{},\"mode\":1}]}]}";

    auto imported = ImportGltf(AsBytes(text), "", allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::Unsupported);
}

CG_TEST(gltf_rejects_node_cycle)
{
    HeapAllocator allocator;
    const StringView text = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
                            "\"scenes\":[{\"nodes\":[0]}],"
                            "\"nodes\":[{\"children\":[1]},{\"children\":[0]}]}";

    auto imported = ImportGltf(AsBytes(text), "", allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::ParseError);
    CG_EXPECT_STREQ(imported.Error().message, "glTF node hierarchy contains a cycle");
}

CG_TEST(glb_imports_embedded_buffer)
{
    HeapAllocator allocator;

    DynamicArray<Byte> binary(allocator);
    AppendFloat(binary, 0.0f);
    AppendFloat(binary, 0.0f);
    AppendFloat(binary, 0.0f);
    AppendFloat(binary, 1.0f);
    AppendFloat(binary, 0.0f);
    AppendFloat(binary, 0.0f);
    AppendFloat(binary, 0.0f);
    AppendFloat(binary, 1.0f);
    AppendFloat(binary, 0.0f);

    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":\"triangle\",\"mesh\":0}],"
        "\"meshes\":[{\"name\":\"tri\",\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
        "\"buffers\":[{\"byteLength\":36}]}";

    DynamicArray<Byte> glb(allocator);
    CG_REQUIRE(BuildGlb(json, binary, glb));

    auto imported = ImportGltf(Span<const Byte>(glb.Data(), glb.Size()), "", allocator);
    CG_REQUIRE(imported.IsOk());
    ImportedScene& scene = imported.Value();

    CG_REQUIRE(scene.meshes.Size() == 1);
    const cyber::assets::MeshAsset& mesh = scene.meshes[0];
    CG_EXPECT_EQ(mesh.vertices.Size(), static_cast<usize>(3));
    // A non-indexed triangle list gets generated indices.
    CG_REQUIRE(mesh.indices.Size() == 3);
    CG_EXPECT_EQ(mesh.indices[2], static_cast<u32>(2));
    CG_REQUIRE(mesh.submeshes.Size() == 1);
    CG_EXPECT_EQ(mesh.submeshes[0].indexCount, static_cast<u32>(3));
    CG_EXPECT_NEAR(mesh.vertices[1].position.x, 1.0f, 1e-5f);
    CG_EXPECT_EQ(scene.nodes[0].name.View(), StringView("triangle"));
    CG_EXPECT_EQ(scene.instances.Size(), static_cast<usize>(1));
}

CG_TEST(glb_rejects_bad_magic_and_truncated_length)
{
    HeapAllocator allocator;

    DynamicArray<Byte> glb(allocator);
    AppendU32(glb, 0x46546c67u);
    AppendU32(glb, 2);
    AppendU32(glb, 4096);  // claims more bytes than exist

    auto imported = ImportGltf(Span<const Byte>(glb.Data(), glb.Size()), "", allocator);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::ParseError);
    CG_EXPECT_STREQ(imported.Error().message, "GLB length is invalid");

    DynamicArray<Byte> wrongVersion(allocator);
    AppendU32(wrongVersion, 0x46546c67u);
    AppendU32(wrongVersion, 1);
    AppendU32(wrongVersion, 20);
    auto versioned =
        ImportGltf(Span<const Byte>(wrongVersion.Data(), wrongVersion.Size()), "", allocator);
    CG_REQUIRE(versioned.IsError());
    CG_EXPECT_EQ(versioned.Error().code, ErrorCode::Unsupported);
}

CG_TEST(gltf_import_options_are_respected)
{
    HeapAllocator allocator;
    GltfImportOptions options;
    options.maxNodes = 1;
    options.settings.flipTexCoordV = true;

    auto imported =
        cyber::assets::ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator, options);
    CG_REQUIRE(imported.IsError());
    CG_EXPECT_EQ(imported.Error().code, ErrorCode::OutOfRange);
    CG_EXPECT_STREQ(imported.Error().message, "glTF has too many nodes");

    auto plain = cyber::assets::ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator,
                                               GltfImportOptions{});
    CG_REQUIRE(plain.IsOk());
    CG_EXPECT_NEAR(plain.Value().meshes[0].vertices[3].texCoord0.y, 1.0f, 1e-5f);

    GltfImportOptions flipped;
    flipped.settings.flipTexCoordV = true;
    auto mirrored =
        cyber::assets::ImportGltfFile(FixturePath("two_quads.gltf").c_str(), allocator, flipped);
    CG_REQUIRE(mirrored.IsOk());
    CG_EXPECT_NEAR(mirrored.Value().meshes[0].vertices[3].texCoord0.y, 0.0f, 1e-5f);
}

}  // namespace
